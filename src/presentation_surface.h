/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_PRESENTATION_SURFACE_H
#define FAIRYWRITER_PRESENTATION_SURFACE_H

#include "snes_color.h"
#include "presentation_geometry.h"
#include "theme_palette.h"

#include <QImage>
#include <QPointF>
#include <QRect>

class Theme;

class PresentationSurface final
{
public:
	enum WallpaperMode
	{
		PixelatedCover = 0,
		AmbientExtension,
		CleanCover,
		Tile,
		Stretch,
		SolidPattern,
		CherryBlossom,
		SolidColor
	};

	// Wallpaper motion is a pure function of an integer tick. Tick zero is
	// the authored still frame: every animated offset vanishes there, so the
	// static pipeline (ThemeRenderer, thumbnails, focal previews) is
	// unchanged by animation and every frame is deterministic and testable.
	static constexpr int MotionTickPeriod = 3840;

	static QImage render(
		const Theme& theme,
		const QSize& logical_size,
		QRect& document_surface,
		qreal device_pixel_ratio,
		bool panel_visible = false,
		PanelSide panel_side = PanelSide::Right,
		int tick = 0);
	// `clearing` is the writing surface's rectangle in target (virtual) pixels.
	// Generated scenes use it to compose eye-catching elements — the flowering
	// trees — into the margins that frame the document instead of behind it. An
	// empty rect falls back to a centred default for previews and thumbnails.
	static QImage renderWallpaper(const Theme& theme, const QSize& target, int tick = 0,
		const QRect& clearing = QRect());

	// Full-resolution animated frame for the wallpaper motion overlay. The
	// pixels equal render()'s output at the same tick; the overlay masks out
	// the shell and paper so the document region never repaints per tick.
	static QImage renderMotionFrame(
		const Theme& theme,
		const QSize& logical_size,
		qreal device_pixel_ratio,
		int tick,
		bool panel_visible = false,
		PanelSide panel_side = PanelSide::Right);

	// True when the theme's wallpaper source is a generated scene whose
	// content actually changes across ticks.
	static bool wallpaperAnimates(const Theme& theme);

	static QImage preprocessWallpaper(
		const QImage& source,
		const QSize& target,
		WallpaperMode mode,
		const QPointF& focal_point,
		SnesColor fallback,
		SnesColor black_replacement,
		const ThemePalette* palette = nullptr,
		int tick = 0,
		const QRect& clearing = QRect());

	static bool hasPureBlack(const QImage& image);
};

#endif // FAIRYWRITER_PRESENTATION_SURFACE_H
