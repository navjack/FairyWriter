/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "snes_style.h"

#include "theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleOption>

#include <algorithm>
#include <cmath>

namespace
{

int g_virtual_pixel_pitch = 3;
int g_ui_magnification = 1;
int g_shell_mode = 0;

int controlPitch()
{
	return g_virtual_pixel_pitch * g_ui_magnification;
}

int controlPitch(const QWidget* widget)
{
	for (const QWidget* current = widget; current; current = current->parentWidget()) {
		if (current->property("fairywriter.compactControls").toBool()) {
			return std::min(controlPitch(), 2 * g_virtual_pixel_pitch);
		}
	}
	return controlPitch();
}

qreal typographyScale(int magnification)
{
	// Control magnification describes the SNES pixel pitch, not the prose
	// scale. Wide pixel fonts become unusable in a 192-source-pixel panel when
	// their point size doubles with the controls. Keep the default theme at
	// 18, 18, and 24pt for 1x, 2x, and 4x while the control grid still scales
	// by the full selected factor.
	return magnification <= 2 ? 1.0 : (4.0 / 3.0);
}

QColor snes(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
	return SnesStyle::toQColor(SnesColor::fromComponents(red, green, blue));
}

}

void SnesStyle::setVirtualPixelPitch(int pitch)
{
	g_virtual_pixel_pitch = std::clamp(pitch, 1, 8);
}

int SnesStyle::virtualPixelPitch()
{
	return g_virtual_pixel_pitch;
}

void SnesStyle::setUiMagnification(int magnification)
{
	const int snapped = magnification <= 1 ? 1 : (magnification <= 2 ? 2 : 4);
	if (g_ui_magnification == snapped) {
		return;
	}
	QFont font = QApplication::font();
	if (font.pointSizeF() > 0.0) {
		font.setPointSizeF((font.pointSizeF() / typographyScale(g_ui_magnification)) * typographyScale(snapped));
		QApplication::setFont(font);
	}
	g_ui_magnification = snapped;
}

int SnesStyle::uiMagnification()
{
	return g_ui_magnification;
}

int SnesStyle::uiPixelPitch()
{
	return controlPitch();
}

QFont SnesStyle::scaledDocumentFont(const QFont& source, int source_pixel_height, int virtual_pixel_pitch)
{
	QFont font(source);
	font.setPixelSize(std::max(1, source_pixel_height * std::max(1, virtual_pixel_pitch)));
	return font;
}

SnesStyle::SnesStyle()
	: QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
}

QColor SnesStyle::toQColor(SnesColor color)
{
	return QColor(color.red8(), color.green8(), color.blue8());
}

SnesColor SnesStyle::toSnesColor(const QColor& color, SnesColor black_replacement)
{
	return SnesColor::fromRgb888(
		static_cast<std::uint8_t>(color.red()),
		static_cast<std::uint8_t>(color.green()),
		static_cast<std::uint8_t>(color.blue()),
		black_replacement);
}

QColor SnesStyle::quantize(const QColor& color, SnesColor black_replacement)
{
	QColor result = toQColor(toSnesColor(color, black_replacement));
	result.setAlpha(color.alpha());
	return result;
}

qreal SnesStyle::contrastRatio(const QColor& foreground, const QColor& background)
{
	auto luminance = [](const QColor& color) {
		auto linear = [](qreal channel) {
			channel /= 255.0;
			return channel <= 0.04045
				? channel / 12.92
				: std::pow((channel + 0.055) / 1.055, 2.4);
		};
		return (0.2126 * linear(color.red()))
			+ (0.7152 * linear(color.green()))
			+ (0.0722 * linear(color.blue()));
	};
	const qreal first = luminance(foreground);
	const qreal second = luminance(background);
	return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

QColor SnesStyle::readableTextColor(
	const QColor& preferred,
	const QColor& background,
	SnesColor black_replacement)
{
	QColor selected = quantize(preferred, black_replacement);
	selected.setAlpha(255);
	if (contrastRatio(selected, background) >= 4.5) {
		return selected;
	}

	const QColor dark = toQColor(SnesColor::fromComponents(4, 3, 6));
	const QColor light = toQColor(SnesColor::fromComponents(31, 30, 27));
	return contrastRatio(dark, background) >= contrastRatio(light, background) ? dark : light;
}

QPalette SnesStyle::pastelMeadowPalette()
{
	const QColor sky = snes(23, 28, 29);
	const QColor mint = snes(24, 29, 25);
	const QColor leaf = snes(15, 23, 14);
	const QColor peach = snes(30, 22, 18);
	const QColor butter = snes(30, 27, 16);
	const QColor lavender = snes(24, 22, 29);
	const QColor cream = snes(31, 30, 27);
	const QColor pink = snes(29, 21, 24);
	const QColor plum = snes(9, 6, 11);

	QPalette palette;
	palette.setColor(QPalette::Window, sky);
	palette.setColor(QPalette::WindowText, plum);
	palette.setColor(QPalette::Base, cream);
	palette.setColor(QPalette::AlternateBase, mint);
	palette.setColor(QPalette::Text, plum);
	palette.setColor(QPalette::Button, mint);
	palette.setColor(QPalette::ButtonText, plum);
	palette.setColor(QPalette::Highlight, leaf);
	palette.setColor(QPalette::HighlightedText, cream);
	palette.setColor(QPalette::ToolTipBase, butter);
	palette.setColor(QPalette::ToolTipText, plum);
	palette.setColor(QPalette::Link, QColor(66, 108, 148));
	palette.setColor(QPalette::BrightText, peach);
	palette.setColor(QPalette::Light, cream);
	palette.setColor(QPalette::Midlight, butter);
	palette.setColor(QPalette::Mid, pink);
	palette.setColor(QPalette::Dark, leaf);
	palette.setColor(QPalette::Shadow, lavender);
	palette.setColor(QPalette::PlaceholderText, snes(15, 13, 16));
	return palette;
}

QPalette SnesStyle::themePalette(const Theme& theme)
{
	QPalette palette = pastelMeadowPalette();
	const QColor fallback = theme.backgroundColor();
	const QColor paper = theme.foregroundColor();
	const QColor primary = theme.uiPrimaryColor();
	const QColor secondary = theme.uiSecondaryColor();
	const QColor accent = theme.uiAccentColor();
	const QColor preferred_text = theme.uiTextColor();
	const SnesColor replacement = toSnesColor(theme.blackReplacement());
	const QColor window_text = readableTextColor(preferred_text, fallback, replacement);
	const QColor base_text = readableTextColor(preferred_text, paper, replacement);
	const QColor button_text = readableTextColor(preferred_text, primary, replacement);
	const QColor highlight_text = readableTextColor(preferred_text, accent, replacement);
	const QColor tooltip_text = readableTextColor(preferred_text, secondary, replacement);
	palette.setColor(QPalette::Window, fallback);
	palette.setColor(QPalette::WindowText, window_text);
	palette.setColor(QPalette::Base, paper);
	palette.setColor(QPalette::AlternateBase, secondary);
	palette.setColor(QPalette::Text, base_text);
	palette.setColor(QPalette::Button, primary);
	palette.setColor(QPalette::ButtonText, button_text);
	palette.setColor(QPalette::Highlight, accent);
	palette.setColor(QPalette::HighlightedText, highlight_text);
	palette.setColor(QPalette::ToolTipBase, secondary);
	palette.setColor(QPalette::ToolTipText, tooltip_text);
	palette.setColor(QPalette::Light, paper);
	palette.setColor(QPalette::Midlight, secondary);
	palette.setColor(QPalette::Mid, primary);
	palette.setColor(QPalette::Dark, accent);
	palette.setColor(QPalette::Shadow, theme.blackReplacement());
	palette.setColor(QPalette::PlaceholderText, base_text.darker(130));
	return palette;
}

void SnesStyle::applyTheme(const Theme& theme)
{
	QApplication::setPalette(themePalette(theme));
	g_shell_mode = theme.shellMode();
	QString family = theme.uiFontFamily();
	const QStringList families = QFontDatabase::families();
	if (!families.contains(family)) {
		family = families.contains("Blasphemous") ? QStringLiteral("Blasphemous") : QApplication::font().family();
	}
	QFont font(family);
	font.setPointSizeF(theme.uiFontSize() * typographyScale(g_ui_magnification));
	QApplication::setFont(font);
}

void SnesStyle::polish(QPalette& palette)
{
	QProxyStyle::polish(palette);
}

int SnesStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
	switch (metric) {
	case PM_DefaultFrameWidth:
	case PM_MenuPanelWidth:
	case PM_MenuBarPanelWidth:
		return controlPitch(widget);
	case PM_ButtonMargin:
	case PM_MenuBarItemSpacing:
		return 2 * controlPitch(widget);
	case PM_ToolBarIconSize:
	case PM_LargeIconSize:
		return 8 * controlPitch(widget);
	case PM_SmallIconSize:
		return 4 * controlPitch(widget);
	default:
		return QProxyStyle::pixelMetric(metric, option, widget);
	}
}

void SnesStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
	if (element == PE_Frame || element == PE_FrameMenu || element == PE_PanelMenuBar || element == PE_PanelToolBar) {
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, false);
		painter->fillRect(option->rect, option->palette.color(QPalette::Button));
		const int pitch = controlPitch(widget);
		painter->setPen(QPen(option->palette.color(QPalette::Dark), pitch));
		painter->drawRect(option->rect.adjusted(pitch / 2, pitch / 2, -pitch, -pitch));
		if (g_shell_mode == 1) {
			painter->setPen(QPen(option->palette.color(QPalette::Highlight), pitch));
			painter->drawRect(option->rect.adjusted(2 * pitch, 2 * pitch, -2 * pitch, -2 * pitch));
			painter->setPen(QPen(option->palette.color(QPalette::Midlight), pitch));
			painter->drawRect(option->rect.adjusted(4 * pitch, 4 * pitch, -4 * pitch, -4 * pitch));
		} else {
			painter->setPen(QPen(option->palette.color(QPalette::Midlight), pitch));
			painter->drawRect(option->rect.adjusted(2 * pitch, 2 * pitch, -2 * pitch, -2 * pitch));
		}
		painter->restore();
		return;
	}
	QProxyStyle::drawPrimitive(element, option, painter, widget);
}
