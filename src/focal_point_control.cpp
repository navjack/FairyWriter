/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "focal_point_control.h"

#include "theme.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

FocalPointControl::FocalPointControl(QWidget* parent)
	: QWidget(parent)
	, m_point(0.5, 0.5)
{
	setFixedHeight(56);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::CrossCursor);
}

void FocalPointControl::setPoint(const QPointF& point)
{
	const QPointF clamped(
		std::clamp(point.x(), 0.0, 1.0),
		std::clamp(point.y(), 0.0, 1.0));
	if (m_point == clamped) {
		return;
	}
	m_point = clamped;
	update();
}

void FocalPointControl::setTheme(const Theme& theme)
{
	// Rendering decodes image wallpapers from disk, so skip identical drafts;
	// syncControls() re-sends the theme on every editor keystroke.
	if (!m_preview.isNull() && theme == m_preview_theme) {
		return;
	}
	m_preview_theme = theme;
	QRect paper;
	m_preview = theme.render(QSize(320, 180), paper, 0, 1.0);
	update();
}

void FocalPointControl::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton) {
		setPointFromPosition(event->position().toPoint());
	}
}

void FocalPointControl::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		Q_EMIT editingStarted();
		setPointFromPosition(event->position().toPoint());
	}
}

void FocalPointControl::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		Q_EMIT editingFinished();
	}
}

void FocalPointControl::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
	painter.fillRect(rect(), palette().base());
	QRect target = rect().adjusted(2, 2, -2, -2);
	if (!m_preview.isNull()) {
		QSize scaled = m_preview.size();
		scaled.scale(target.size(), Qt::KeepAspectRatio);
		target = QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
		painter.drawImage(target, m_preview);
	}
	// The crosshair only appears while the focal point can affect rendering;
	// generated scenes keep the preview but drop the interaction affordance.
	if (isEnabled()) {
		const QPoint marker(
			target.left() + qRound(m_point.x() * std::max(0, target.width() - 1)),
			target.top() + qRound(m_point.y() * std::max(0, target.height() - 1)));
		painter.setPen(QPen(palette().highlightedText().color(), 3));
		painter.drawLine(marker.x() - 7, marker.y(), marker.x() + 7, marker.y());
		painter.drawLine(marker.x(), marker.y() - 7, marker.x(), marker.y() + 7);
	}
	painter.setPen(QPen(palette().highlight().color(), 1));
	painter.drawRect(target);
}

void FocalPointControl::setPointFromPosition(const QPoint& position)
{
	if (width() <= 1 || height() <= 1) {
		return;
	}
	setPoint(QPointF(
		std::clamp(position.x() / static_cast<qreal>(width() - 1), 0.0, 1.0),
		std::clamp(position.y() / static_cast<qreal>(height() - 1), 0.0, 1.0)));
	Q_EMIT pointChanged(m_point);
}
