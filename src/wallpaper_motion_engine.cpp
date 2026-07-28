/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "wallpaper_motion_engine.h"

#include "presentation_surface.h"

#include <QtConcurrentRun>

WallpaperMotionEngine::WallpaperMotionEngine(QObject* parent)
	: QObject(parent)
{
	m_timer.setInterval(TickIntervalMs);
	m_timer.setTimerType(Qt::CoarseTimer);
	connect(&m_timer, &QTimer::timeout, this, &WallpaperMotionEngine::advance);
	connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, [this] {
		m_rendering = false;
		if (m_render_generation == m_generation) {
			Q_EMIT frameReady(m_watcher.result(), m_render_tick, m_render_generation);
			if (m_timer.isActive() && m_render_tick != m_tick) {
				startRender();
			}
		} else if (m_timer.isActive()) {
			// The scene changed while this frame was in flight; render the
			// replacement immediately instead of waiting a full interval.
			startRender();
		}
	});
}

void WallpaperMotionEngine::setScene(const Theme& theme, const QSize& logical_size,
	qreal device_pixel_ratio, bool panel_visible, PanelSide panel_side)
{
	if (theme == m_theme
			&& logical_size == m_logical_size
			&& qFuzzyCompare(device_pixel_ratio, m_device_pixel_ratio)
			&& panel_visible == m_panel_visible
			&& panel_side == m_panel_side) {
		return;
	}
	m_theme = theme;
	m_logical_size = logical_size;
	m_device_pixel_ratio = device_pixel_ratio;
	m_panel_visible = panel_visible;
	m_panel_side = panel_side;
	++m_generation;
	if (m_timer.isActive() && !m_rendering) {
		startRender();
	}
}

void WallpaperMotionEngine::setActive(bool active)
{
	if (active == m_timer.isActive()) {
		return;
	}
	if (active) {
		m_timer.start();
		if (!m_rendering) {
			startRender();
		}
	} else {
		// Freeze on the last delivered frame; the tick resumes from here so
		// reactivation never snaps the scene back to its still frame.
		m_timer.stop();
	}
}

void WallpaperMotionEngine::advance()
{
	m_tick = (m_tick + 1) % PresentationSurface::MotionTickPeriod;
	if (!m_rendering) {
		startRender();
	}
}

void WallpaperMotionEngine::startRender()
{
	if (m_logical_size.isEmpty()) {
		return;
	}
	m_rendering = true;
	m_render_tick = m_tick;
	m_render_generation = m_generation;
	const Theme theme = m_theme;
	const QSize logical_size = m_logical_size;
	const qreal device_pixel_ratio = m_device_pixel_ratio;
	const bool panel_visible = m_panel_visible;
	const PanelSide panel_side = m_panel_side;
	const int tick = m_tick;
	m_watcher.setFuture(QtConcurrent::run([theme, logical_size, device_pixel_ratio, panel_visible, panel_side, tick] {
		return PresentationSurface::renderMotionFrame(
			theme, logical_size, device_pixel_ratio, tick, panel_visible, panel_side);
	}));
}
