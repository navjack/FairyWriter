/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_THEME_TONE_MAPPER_H
#define FAIRYWRITER_THEME_TONE_MAPPER_H

#include "snes_color.h"
#include "theme_palette.h"

#include <QColor>
#include <QImage>

#include <array>

class ThemeToneMapper final
{
public:
	static QColor mapColor(const QColor& color, const QColor& deep);
	static QColor mapDeepColor(const QColor& color);
	static QColor readableText(const QColor& preferred, const QColor& background, const QColor& deep);
	static ThemePalette mapPalette(const ThemePalette& palette);
	static std::array<QRgb, 32768> buildWallpaperLut(const QColor& deep);
	static void mapWallpaper(QImage& image, const QColor& deep);
};

#endif // FAIRYWRITER_THEME_TONE_MAPPER_H
