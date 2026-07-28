/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_WALLPAPER_MOTION_LAYER_H
#define FAIRYWRITER_WALLPAPER_MOTION_LAYER_H

#include "wallpaper_motion_engine.h"

#include <QPixmap>
#include <QRegion>
#include <QWidget>

// The animated wallpaper overlay. It sits between the Stack's static
// background and the transparent document contents, masked to the wallpaper
// region (everything outside the shell surfaces), so petals pass behind the
// paper and the editor never repaints on a motion tick. Motion pauses with
// a frozen frame whenever the window or application goes inactive; the
// static background beneath guarantees no unpainted pixel can ever show.
class WallpaperMotionLayer : public QWidget
{
	Q_OBJECT

public:
	explicit WallpaperMotionLayer(QWidget* parent = nullptr);

	void setPresentationState(const Theme& theme, const QRect& document_frame,
		const QRect& panel_frame, bool panel_visible, PanelSide panel_side);
	void setMotionPreference(bool enabled);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	void applyFrame(const QImage& frame);
	void updateActivation();

private:
	WallpaperMotionEngine m_engine;
	QPixmap m_frame;
	QRegion m_wallpaper_region;
	bool m_preference = true;
	bool m_scene_animates = false;
	bool m_filter_installed = false;
};

#endif // FAIRYWRITER_WALLPAPER_MOTION_LAYER_H
