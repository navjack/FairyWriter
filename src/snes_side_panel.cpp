/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "snes_side_panel.h"

#include <QDialog>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

class PanelToolHost final : public QWidget
{
public:
	explicit PanelToolHost(QWidget* parent = nullptr)
		: QWidget(parent)
		, m_tool(nullptr)
	{
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
		setMinimumSize(0, 0);
	}

	QSize sizeHint() const override { return QSize(0, 0); }
	QSize minimumSizeHint() const override { return QSize(0, 0); }

	void setTool(QWidget* tool)
	{
		if (m_tool && m_tool != tool) {
			m_tool->hide();
		}
		m_tool = tool;
		if (m_tool) {
			m_tool->setParent(this);
			m_tool->setGeometry(contentsRect());
		}
	}

	void releaseTool(const QWidget* tool)
	{
		if (m_tool == tool) {
			m_tool = nullptr;
		}
	}

protected:
	void resizeEvent(QResizeEvent* event) override
	{
		QWidget::resizeEvent(event);
		if (m_tool) {
			m_tool->setGeometry(contentsRect());
		}
	}

private:
	QWidget* m_tool;
};

SnesSidePanel::SnesSidePanel(QWidget* parent)
	: QWidget(parent)
	, m_current_tool(nullptr)
{
	setAutoFillBackground(true);
	setFocusPolicy(Qt::StrongFocus);

	m_title = new QLabel(this);
	m_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	QPushButton* close = new QPushButton(tr("Close"), this);
	close->setAutoDefault(false);
	connect(close, &QPushButton::clicked, this, &SnesSidePanel::closePanel);

	QHBoxLayout* title_layout = new QHBoxLayout;
	title_layout->setContentsMargins(0, 0, 0, 0);
	title_layout->addWidget(m_title, 1);
	title_layout->addWidget(close);

	m_scroll = new QScrollArea(this);
	m_scroll->setObjectName("FairyWriterToolViewport");
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_container = new PanelToolHost(m_scroll);
	m_scroll->setWidget(m_container);

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);
	layout->addLayout(title_layout);
	layout->addWidget(m_scroll, 1);
	hide();
}

void SnesSidePanel::showTool(QWidget* tool, const QString& title)
{
	if (!tool) {
		return;
	}
	if (m_current_tool && m_current_tool != tool) {
		m_current_tool->hide();
	}

	m_current_tool = tool;
	if (!tool->property("fairywriter.panelWired").toBool()) {
		tool->setProperty("fairywriter.panelWired", true);
		connect(tool, &QObject::destroyed, this, [this, tool] {
			if (m_current_tool == tool) {
				m_container->releaseTool(tool);
				m_current_tool = nullptr;
				hide();
			}
		});
	}
	if (QDialog* dialog = qobject_cast<QDialog*>(tool)) {
		dialog->setWindowFlags(Qt::Widget);
	}
	const bool fill_panel = tool->property("fairywriter.panelFill").toBool();
	// A desktop dialog's sizeHint describes its old floating-window shape. A
	// panel-fill tool must instead accept the exact SNES viewport and let its
	// selected page lay out within that rectangle; otherwise QScrollArea keeps
	// a hidden scroll range even when every visible page can fit.
	tool->setMinimumSize(0, 0);
	tool->setSizePolicy(fill_panel ? QSizePolicy::Ignored : QSizePolicy::Expanding,
		fill_panel ? QSizePolicy::Ignored : QSizePolicy::Preferred);
	if (fill_panel && tool->layout()) {
		tool->layout()->setSizeConstraint(QLayout::SetNoConstraint);
	}
	tool->setMinimumSize(0, 0);
	m_scroll->setVerticalScrollBarPolicy(fill_panel ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
	m_container->setTool(tool);
	m_title->setText(title);
	tool->show();
	show();
	raise();
	tool->setFocus();
}

void SnesSidePanel::closePanel()
{
	if (QDialog* dialog = qobject_cast<QDialog*>(m_current_tool)) {
		if (dialog->isVisible()) {
			dialog->reject();
			return;
		}
	}
	if (m_current_tool) {
		m_current_tool->hide();
	}
	hide();
}

void SnesSidePanel::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	Q_EMIT panelVisibilityChanged(true);
}

void SnesSidePanel::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	Q_EMIT panelVisibilityChanged(false);
}
