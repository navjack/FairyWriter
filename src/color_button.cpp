/*
	SPDX-FileCopyrightText: 2008 Graeme Gott <graeme@gottcode.org>

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "color_button.h"

#include "snes_color.h"
#include "snes_style.h"

#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace
{

QList<QColor> g_recent_colors;

}

//-----------------------------------------------------------------------------

ColorButton::ColorButton(QWidget* parent)
	: QPushButton(parent)
	, m_swatch_width(75)
{
	setAutoDefault(false);
	connect(this, &ColorButton::clicked, this, &ColorButton::onClicked);
}

//-----------------------------------------------------------------------------

QString ColorButton::toString() const
{
	return m_color.name();
}

//-----------------------------------------------------------------------------

void ColorButton::setColor(const QColor& color)
{
	const SnesColor quantized = SnesColor::fromRgb888(
		static_cast<std::uint8_t>(color.red()),
		static_cast<std::uint8_t>(color.green()),
		static_cast<std::uint8_t>(color.blue()));
	const QColor snapped = QColor::fromRgb(quantized.red8(), quantized.green8(), quantized.blue8());
	if (m_color == snapped) {
		return;
	}
	m_color = snapped;

	updateSwatch();

	Q_EMIT changed(m_color);
}

void ColorButton::setSwatchWidth(int width)
{
	m_swatch_width = std::max(16, width);
	updateSwatch();
}

void ColorButton::updateSwatch()
{
	if (!m_color.isValid()) {
		return;
	}
	QPixmap swatch(m_swatch_width, fontMetrics().height());
	swatch.fill(m_color);
	{
		QPainter painter(&swatch);
		painter.setPen(m_color.darker());
		painter.drawRect(QRectF(0, 0, swatch.width() - 1, swatch.height() - 1));
		painter.setPen(m_color.lighter());
		painter.drawRect(QRectF(1, 1, swatch.width() - 3, swatch.height() - 3));
	}
	setIconSize(swatch.size());
	setIcon(swatch);
}

//-----------------------------------------------------------------------------

void ColorButton::onClicked()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("SNES Color"));
	QGridLayout* grid = new QGridLayout(&dialog);
	const SnesColor initial = SnesColor::fromRgb888(m_color.red(), m_color.green(), m_color.blue());
	std::array<QSlider*, 3> sliders{};
	std::array<QLabel*, 3> values{};
	const char* names[] = { QT_TR_NOOP("Red"), QT_TR_NOOP("Green"), QT_TR_NOOP("Blue") };
	for (int i = 0; i < 3; ++i) {
		sliders[i] = new QSlider(Qt::Horizontal, &dialog);
		sliders[i]->setRange(0, 31);
		sliders[i]->setValue(i == 0 ? initial.red() : (i == 1 ? initial.green() : initial.blue()));
		values[i] = new QLabel(&dialog);
		grid->addWidget(new QLabel(tr(names[i]), &dialog), i, 0);
		grid->addWidget(sliders[i], i, 1);
		grid->addWidget(values[i], i, 2);
	}
	QLabel* preview = new QLabel(&dialog);
	preview->setMinimumHeight(28);
	preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
	QLabel* exact = new QLabel(&dialog);
	QPushButton* picker = new QPushButton(tr("Pick and snap…"), &dialog);
	grid->addWidget(picker, 3, 0, 1, 2);
	grid->addWidget(preview, 4, 0, 1, 3);
	grid->addWidget(exact, 5, 0, 1, 3);
	auto addSwatches = [&](const QString& title, const QList<QColor>& colors, int row) {
		QWidget* swatches = new QWidget(&dialog);
		QHBoxLayout* swatch_layout = new QHBoxLayout(swatches);
		swatch_layout->setContentsMargins(0, 0, 0, 0);
		swatch_layout->setSpacing(3);
		for (const QColor& color : colors) {
			auto* swatch = new QPushButton(swatches);
			swatch->setFixedSize(22, 22);
			swatch->setToolTip(color.name().toUpper());
			swatch->setStyleSheet(QStringLiteral("background:%1").arg(color.name()));
			connect(swatch, &QPushButton::clicked, &dialog, [=] {
				const SnesColor selected = SnesStyle::toSnesColor(color);
				sliders[0]->setValue(selected.red());
				sliders[1]->setValue(selected.green());
				sliders[2]->setValue(selected.blue());
			});
			swatch_layout->addWidget(swatch);
		}
		swatch_layout->addStretch();
		grid->addWidget(new QLabel(title, &dialog), row, 0);
		grid->addWidget(swatches, row, 1, 1, 2);
	};
	addSwatches(tr("Theme"), m_companion_colors, 6);
	addSwatches(tr("Recent"), g_recent_colors, 7);
	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	grid->addWidget(buttons, 8, 0, 1, 3);
	auto update = [=] {
		const SnesColor color = SnesColor::fromComponents(sliders[0]->value(), sliders[1]->value(), sliders[2]->value());
		for (int i = 0; i < 3; ++i) values[i]->setText(QString::number(sliders[i]->value()) + tr(" / 31"));
		const QColor selected = SnesStyle::toQColor(color);
		const QColor mapped = m_color_mapper ? m_color_mapper(selected) : selected;
		QPixmap swatch(preview->sizeHint().expandedTo(QSize(120, 28)));
		swatch.fill(mapped);
		preview->setPixmap(swatch);
		exact->setText(tr("RGB: %1    BGR555: 0x%2")
			.arg(mapped.name().toUpper())
			.arg(color.value(), 4, 16, QLatin1Char('0')).toUpper());
	};
	for (QSlider* slider : sliders) connect(slider, &QSlider::valueChanged, &dialog, update);
	connect(picker, &QPushButton::clicked, &dialog, [&] {
		const QColor picked = QColorDialog::getColor(m_color, &dialog, tr("Choose source color"));
		if (!picked.isValid()) return;
		const SnesColor color = SnesColor::fromRgb888(picked.red(), picked.green(), picked.blue());
		sliders[0]->setValue(color.red()); sliders[1]->setValue(color.green()); sliders[2]->setValue(color.blue());
	});
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	update();
	if (dialog.exec() == QDialog::Accepted) {
		QColor selected = SnesStyle::toQColor(SnesColor::fromComponents(sliders[0]->value(), sliders[1]->value(), sliders[2]->value()));
		if (m_color_mapper) selected = m_color_mapper(selected);
		g_recent_colors.removeAll(selected);
		g_recent_colors.prepend(selected);
		while (g_recent_colors.size() > 8) g_recent_colors.removeLast();
		setColor(selected);
	}
}

//-----------------------------------------------------------------------------
