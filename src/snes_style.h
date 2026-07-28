/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_SNES_STYLE_H
#define FAIRYWRITER_SNES_STYLE_H

#include "snes_color.h"

#include <QColor>
#include <QProxyStyle>

class QPainter;
class QPalette;
class QFont;
class QStyleOption;
class QWidget;
class Theme;

class SnesStyle final : public QProxyStyle
{
public:
	SnesStyle();

	static QColor toQColor(SnesColor color);
	static SnesColor toSnesColor(const QColor& color, SnesColor black_replacement = SnesColor());
	static QColor quantize(const QColor& color, SnesColor black_replacement = SnesColor());
	static qreal contrastRatio(const QColor& foreground, const QColor& background);
	static QColor readableTextColor(const QColor& preferred, const QColor& background, SnesColor black_replacement);
	static QPalette pastelMeadowPalette();
	static QPalette themePalette(const Theme& theme);
	static void applyTheme(const Theme& theme);
	static void setVirtualPixelPitch(int pitch);
	static int virtualPixelPitch();
	static void setUiMagnification(int magnification);
	static int uiMagnification();
	static int uiPixelPitch();
	static QFont scaledDocumentFont(const QFont& font, int source_pixel_height, int virtual_pixel_pitch);

	void polish(QPalette& palette) override;
	int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
	void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
};

#endif // FAIRYWRITER_SNES_STYLE_H
