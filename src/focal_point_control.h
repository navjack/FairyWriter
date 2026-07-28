/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_FOCAL_POINT_CONTROL_H
#define FAIRYWRITER_FOCAL_POINT_CONTROL_H

#include "theme.h"

#include <QImage>
#include <QPointF>
#include <QWidget>

class FocalPointControl final : public QWidget
{
	Q_OBJECT

public:
	explicit FocalPointControl(QWidget* parent = nullptr);
	QPointF point() const { return m_point; }
	void setPoint(const QPointF& point);
	void setTheme(const Theme& theme);

Q_SIGNALS:
	void editingStarted();
	void editingFinished();
	void pointChanged(const QPointF& point);

protected:
	void mouseMoveEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void setPointFromPosition(const QPoint& position);

private:
	QImage m_preview;
	Theme m_preview_theme;
	QPointF m_point;
};

#endif // FAIRYWRITER_FOCAL_POINT_CONTROL_H
