/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_WALLPAPER_MOTION_ENGINE_H
#define FAIRYWRITER_WALLPAPER_MOTION_ENGINE_H

#include "presentation_geometry.h"
#include "theme.h"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QTimer>

// Paces the animated wallpaper and renders frames off the UI thread. Every
// frame is a pure function of (theme, size, tick): the timer only advances
// the tick, and a busy worker simply skips renders, so dropped frames can
// never corrupt the scene. A generation counter invalidates in-flight
// renders whenever the scene input changes, mirroring the stale-frame guard
// used by the static presentation background.
class WallpaperMotionEngine : public QObject
{
	Q_OBJECT

public:
	static constexpr int TickIntervalMs = 80;

	explicit WallpaperMotionEngine(QObject* parent = nullptr);

	int tick() const { return m_tick; }
	bool isActive() const { return m_timer.isActive(); }
	quint64 sceneGeneration() const { return m_generation; }

	void setScene(const Theme& theme, const QSize& logical_size, qreal device_pixel_ratio,
		bool panel_visible, PanelSide panel_side);
	void setActive(bool active);

Q_SIGNALS:
	void frameReady(const QImage& frame, int tick, quint64 scene_generation);

private:
	void advance();
	void startRender();

private:
	QTimer m_timer;
	QFutureWatcher<QImage> m_watcher;
	Theme m_theme;
	QSize m_logical_size;
	qreal m_device_pixel_ratio = 1.0;
	bool m_panel_visible = false;
	PanelSide m_panel_side = PanelSide::Right;
	quint64 m_generation = 0;
	quint64 m_render_generation = 0;
	int m_tick = 0;
	int m_render_tick = 0;
	bool m_rendering = false;
};

#endif // FAIRYWRITER_WALLPAPER_MOTION_ENGINE_H
