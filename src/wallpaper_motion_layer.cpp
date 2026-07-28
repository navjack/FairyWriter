/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "wallpaper_motion_layer.h"

#include "presentation_surface.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPainter>

WallpaperMotionLayer::WallpaperMotionLayer(QWidget* parent)
	: QWidget(parent)
{
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAutoFillBackground(false);
	setFocusPolicy(Qt::NoFocus);
	connect(&m_engine, &WallpaperMotionEngine::frameReady, this,
		[this](const QImage& frame, int tick, quint64 generation) {
			Q_UNUSED(tick)
			Q_UNUSED(generation)
			applyFrame(frame);
		});
	connect(qApp, &QGuiApplication::applicationStateChanged, this,
		[this](Qt::ApplicationState) { updateActivation(); });
}

void WallpaperMotionLayer::setPresentationState(const Theme& theme, const QRect& document_frame,
	const QRect& panel_frame, bool panel_visible, PanelSide panel_side)
{
	// Translucent legacy paper blends the static wallpaper into the shell
	// surface itself; animating around a frozen see-through paper would
	// visibly tear, so motion requires fully opaque paper.
	m_scene_animates = PresentationSurface::wallpaperAnimates(theme)
		&& theme.foregroundOpacity() == 100;
	QRegion region(rect());
	region -= document_frame;
	if (panel_visible && !panel_frame.isEmpty()) {
		region -= panel_frame;
	}
	if (region != m_wallpaper_region) {
		m_wallpaper_region = region;
		setMask(m_wallpaper_region);
	}
	m_engine.setScene(theme, size(), devicePixelRatioF(), panel_visible, panel_side);
	updateActivation();
}

void WallpaperMotionLayer::setMotionPreference(bool enabled)
{
	if (m_preference == enabled) {
		return;
	}
	m_preference = enabled;
	updateActivation();
	if (!enabled) {
		// Drop the frozen frame so the still tick-zero background shows
		// through instead of a paused mid-motion frame.
		m_frame = QPixmap();
		update();
	}
}

bool WallpaperMotionLayer::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == window()
			&& (event->type() == QEvent::ActivationChange
				|| event->type() == QEvent::WindowStateChange)) {
		updateActivation();
	}
	return QWidget::eventFilter(watched, event);
}

void WallpaperMotionLayer::paintEvent(QPaintEvent* event)
{
	Q_UNUSED(event)
	// A frame from a stale geometry is discarded rather than stretched; the
	// static background beneath keeps every pixel painted until the engine
	// delivers a frame that matches the current size.
	if (m_frame.isNull() || m_frame.size() != size() * devicePixelRatioF()) {
		return;
	}
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
	painter.drawPixmap(0, 0, m_frame);
}

void WallpaperMotionLayer::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	if (!m_filter_installed && window()) {
		window()->installEventFilter(this);
		m_filter_installed = true;
	}
	updateActivation();
}

void WallpaperMotionLayer::applyFrame(const QImage& frame)
{
	if (frame.size() != size() * devicePixelRatioF()) {
		return;
	}
	m_frame = QPixmap::fromImage(frame, Qt::AutoColor | Qt::AvoidDither);
	m_frame.setDevicePixelRatio(devicePixelRatioF());
	update();
}

void WallpaperMotionLayer::updateActivation()
{
	const QWidget* top = window();
	const bool window_ready = top
		&& top->isActiveWindow()
		&& !(top->windowState() & Qt::WindowMinimized);
	const bool application_active = QGuiApplication::applicationState() == Qt::ApplicationActive;
	m_engine.setActive(m_scene_animates
		&& m_preference
		&& isVisible()
		&& window_ready
		&& application_active);
}
