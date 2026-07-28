/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "presentation_geometry.h"
#include "snes_color.h"

#include <iostream>

namespace
{

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

void testSnesColor()
{
	const SnesColor replacement = SnesColor::fromComponents(4, 6, 8);
	const SnesColor imported_black = SnesColor::fromRgb888(0, 0, 0, replacement);
	expect(imported_black == replacement, "imported black uses the theme replacement");
	expect(imported_black.value() != 0, "a SnesColor can never contain pure black");

	const SnesColor color = SnesColor::fromComponents(31, 17, 5);
	expect(color.value() == static_cast<std::uint16_t>(31 | (17 << 5) | (5 << 10)),
		"BGR555 uses R | G<<5 | B<<10");
	expect(color.red() == 31 && color.green() == 17 && color.blue() == 5,
		"BGR555 components round trip");
	expect(SnesColor::quantizeChannel(255) == 31, "255 quantizes to 31");
	expect(SnesColor::quantizeChannel(0) == 0, "zero quantizes to zero before replacement");
	expect(SnesColor::expandChannel(31) == 255, "31 expands to 255");
}

void testCorrectedGeometry()
{
	expect(PresentationGeometry::correctedDocumentWidth(1) == 299,
		"scale one applies rounded 7:6 correction");
	expect(PresentationGeometry::correctedDocumentWidth(3) == 896,
		"scale three applies exact 7:6 correction");

	const PresentationLayout centered = PresentationGeometry::calculate({ 1280, 800 }, false);
	expect(centered.scale == 3, "largest whole-document scale is selected");
	expect(centered.document.width == 896 && centered.document.height == 672,
		"document uses corrected dimensions");
	expect(centered.document.x == (1280 - 896) / 2, "document is centered without a panel");
	expect(centered.fits, "normal desktop viewport fits");

	const PresentationLayout right = PresentationGeometry::calculate({ 1920, 1080 }, true, PanelSide::Right);
	expect(right.scale == 3, "panel layout drops to the largest fitting scale");
	expect(right.panel.x - (right.document.x + right.document.width) == 8 * right.scale,
		"right panel has an eight-virtual-pixel gutter");
	expect(right.document.x + right.panel.x + right.panel.width == 1920,
		"combined right-panel group is centered");

	const PresentationLayout left = PresentationGeometry::calculate({ 1920, 1080 }, true, PanelSide::Left);
	expect(left.document.x - (left.panel.x + left.panel.width) == 8 * left.scale,
		"left panel has an eight-virtual-pixel gutter");
	expect(left.document.width == right.document.width && left.document.y == right.document.y,
		"switching panel sides does not rescale or vertically move the document");

	const PresentationLayout steam_deck = PresentationGeometry::calculate({ 1280, 800 }, true, PanelSide::Right);
	expect(steam_deck.scale == 2 && steam_deck.panel.width == 384 && steam_deck.panel.height == 448,
		"Steam Deck desktop geometry fits a two-times document and 192-source-pixel panel");
	expect(steam_deck.fits, "Steam Deck side-panel group fits without overlap");

	const PresentationLayout hidden = PresentationGeometry::calculate({ 1920, 1080 }, false);
	expect(hidden.top_chrome.height == PresentationGeometry::TopChromeHeight * hidden.scale,
		"chrome geometry is stable whether chrome is painted or hidden");

	const PresentationLayout retina_fullscreen = PresentationGeometry::calculate({ 2048, 1152 }, false);
	expect(retina_fullscreen.scale == 5,
		"document takes the largest integer SNES scale independent of chrome height");
	expect(retina_fullscreen.document.height == 1120 && retina_fullscreen.document.y == 16,
		"scale-five document is centered in a 1152-pixel presentation canvas");
}

}

int main()
{
	testSnesColor();
	testCorrectedGeometry();
	if (failures == 0) {
		std::cout << "All FairyWriter core tests passed.\n";
	}
	return failures == 0 ? 0 : 1;
}
