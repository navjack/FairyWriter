/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_PRESENTATION_GEOMETRY_H
#define FAIRYWRITER_PRESENTATION_GEOMETRY_H

struct PresentationSize
{
	int width = 0;
	int height = 0;
};

struct PresentationRect
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

enum class PanelSide
{
	Left,
	Right
};

struct PresentationLayout
{
	int scale = 1;
	int virtual_pixel_pitch = 1;
	PresentationRect document;
	PresentationRect panel;
	PresentationRect top_chrome;
	PresentationRect bottom_chrome;
	bool panel_visible = false;
	bool fits = false;
};

class PresentationGeometry final
{
public:
	static constexpr int SourceWidth = 256;
	static constexpr int SourceHeight = 224;
	static constexpr int PanelWidth = 192;
	static constexpr int PanelGutter = 8;
	static constexpr int TopChromeHeight = 16;
	static constexpr int BottomChromeHeight = 24;

	static constexpr int correctedDocumentWidth(int scale)
	{
		// round(256 * 7 * scale / 6), using integer arithmetic.
		return ((SourceWidth * 7 * scale) + 3) / 6;
	}

	static constexpr int documentHeight(int scale)
	{
		return SourceHeight * scale;
	}

	static PresentationLayout calculate(
		PresentationSize viewport,
		bool panel_visible,
		PanelSide panel_side = PanelSide::Right);
};

#endif // FAIRYWRITER_PRESENTATION_GEOMETRY_H
