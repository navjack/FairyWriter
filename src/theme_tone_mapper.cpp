/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "theme_tone_mapper.h"

#include "snes_style.h"

#include <algorithm>
#include <cmath>

namespace
{

QColor quantizedHsl(const QColor& source, qreal minimum_lightness, qreal maximum_lightness)
{
	QColor color = source.toHsl();
	qreal hue = color.hslHueF();
	qreal saturation = color.hslSaturationF();
	qreal lightness = color.lightnessF();
	if (hue < 0.0) {
		hue = 0.0;
	}
	if (saturation >= 0.02) {
		saturation = std::clamp(saturation, 0.22, 0.86);
	}
	lightness = std::clamp(lightness, minimum_lightness, maximum_lightness);
	QColor result = QColor::fromHslF(hue, saturation, lightness, 1.0);
	return SnesStyle::quantize(result);
}

}

QColor ThemeToneMapper::mapDeepColor(const QColor& color)
{
	QColor source = color;
	if (!source.isValid() || source.rgb() == QColor(Qt::black).rgb()) {
		source = QColor(QStringLiteral("#493158"));
	}
	return quantizedHsl(source, 0.10, 0.28);
}

QColor ThemeToneMapper::mapColor(const QColor& color, const QColor& deep)
{
	if (!color.isValid() || (color.red() == 0 && color.green() == 0 && color.blue() == 0)) {
		return mapDeepColor(deep);
	}
	QColor result = quantizedHsl(color, 0.12, 0.90);
	if (result.red() == 0 && result.green() == 0 && result.blue() == 0) {
		result = mapDeepColor(deep);
	}
	result.setAlpha(color.alpha());
	return result;
}

QColor ThemeToneMapper::readableText(const QColor& preferred, const QColor& background, const QColor& deep)
{
	const QColor mapped_background = mapColor(background, deep);
	QColor mapped = mapColor(preferred, deep);
	mapped.setAlpha(255);
	if (SnesStyle::contrastRatio(mapped, mapped_background) >= 4.5) {
		return mapped;
	}

	const QColor hsl = mapped.toHsl();
	QColor best = mapped;
	qreal best_distance = 2.0;
	for (int step = 1; step < 31; ++step) {
		const qreal lightness = step / 31.0;
		QColor candidate = QColor::fromHslF(
			hsl.hslHueF() < 0.0 ? 0.0 : hsl.hslHueF(),
			hsl.hslSaturationF(), lightness);
		candidate = SnesStyle::quantize(candidate, SnesStyle::toSnesColor(mapDeepColor(deep)));
		const qreal contrast = SnesStyle::contrastRatio(candidate, mapped_background);
		const qreal distance = std::abs(lightness - hsl.lightnessF());
		if (contrast >= 4.5 && distance < best_distance) {
			best = candidate;
			best_distance = distance;
		}
	}
	if (best_distance < 2.0) {
		return best;
	}
	return SnesStyle::readableTextColor(mapped, mapped_background, SnesStyle::toSnesColor(mapDeepColor(deep)));
}

ThemePalette ThemeToneMapper::mapPalette(const ThemePalette& source)
{
	ThemePalette result;
	result.deep = mapDeepColor(source.deep);
	result.background = mapColor(source.background, result.deep);
	result.paper = mapColor(source.paper, result.deep);
	result.primary = mapColor(source.primary, result.deep);
	result.secondary = mapColor(source.secondary, result.deep);
	result.accent = mapColor(source.accent, result.deep);
	result.ui_text = readableText(source.ui_text, result.background, result.deep);
	result.document_text = readableText(source.document_text, result.paper, result.deep);
	result.misspelled = mapColor(source.misspelled, result.deep);
	return result;
}

std::array<QRgb, 32768> ThemeToneMapper::buildWallpaperLut(const QColor& deep)
{
	std::array<QRgb, 32768> lut{};
	lut[0] = mapDeepColor(deep).rgb();
	for (std::uint16_t value = 1; value < 32768; ++value) {
		const QColor source = SnesStyle::toQColor(SnesColor::fromBgr555(value));
		lut[value] = mapColor(source, deep).rgb();
	}
	return lut;
}

void ThemeToneMapper::mapWallpaper(QImage& image, const QColor& deep)
{
	if (image.isNull()) {
		return;
	}
	thread_local int cached_deep = -1;
	thread_local std::array<QRgb, 32768> cached_lut{};
	const int deep_value = SnesStyle::toSnesColor(mapDeepColor(deep)).value();
	if (cached_deep != deep_value) {
		cached_lut = buildWallpaperLut(deep);
		cached_deep = deep_value;
	}
	image = image.convertToFormat(QImage::Format_RGB32);
	const SnesColor replacement = SnesStyle::toSnesColor(mapDeepColor(deep));
	for (int y = 0; y < image.height(); ++y) {
		QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			const SnesColor color = SnesColor::fromRgb888(
				static_cast<std::uint8_t>(qRed(line[x])),
				static_cast<std::uint8_t>(qGreen(line[x])),
				static_cast<std::uint8_t>(qBlue(line[x])), replacement);
			line[x] = cached_lut[color.value()];
		}
	}
}
