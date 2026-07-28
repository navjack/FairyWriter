/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_SNES_SIDE_PANEL_H
#define FAIRYWRITER_SNES_SIDE_PANEL_H

#include <QWidget>

class QLabel;
class PanelToolHost;
class QScrollArea;

class SnesSidePanel final : public QWidget
{
	Q_OBJECT

public:
	explicit SnesSidePanel(QWidget* parent = nullptr);

	QWidget* currentTool() const { return m_current_tool; }
	void showTool(QWidget* tool, const QString& title);

public Q_SLOTS:
	void closePanel();

Q_SIGNALS:
	void panelVisibilityChanged(bool visible);

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	QLabel* m_title;
	QScrollArea* m_scroll;
	PanelToolHost* m_container;
	QWidget* m_current_tool;
};

#endif // FAIRYWRITER_SNES_SIDE_PANEL_H
