/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "presentation_geometry.h"

#include <algorithm>

namespace
{

int groupWidth(int scale, bool panel_visible)
{
	int width = PresentationGeometry::correctedDocumentWidth(scale);
	if (panel_visible) {
		width += (PresentationGeometry::PanelGutter + PresentationGeometry::PanelWidth) * scale;
	}
	return width;
}

int requiredHeight(int scale)
{
	// Chrome is a presentation layer, not part of the document's scale budget.
	// Reserving its height here made a visible interface permanently shrink the
	// 256x224 source view even though hiding chrome is forbidden from changing
	// document geometry.
	return PresentationGeometry::SourceHeight * scale;
}

}

PresentationLayout PresentationGeometry::calculate(
	PresentationSize viewport,
	bool panel_visible,
	PanelSide panel_side)
{
	PresentationLayout result;
	result.panel_visible = panel_visible;

	const int width = std::max(0, viewport.width);
	const int height = std::max(0, viewport.height);

	int scale = 1;
	while (groupWidth(scale + 1, panel_visible) <= width
		&& requiredHeight(scale + 1) <= height) {
		++scale;
	}

	result.scale = scale;
	result.virtual_pixel_pitch = scale;
	result.fits = groupWidth(scale, panel_visible) <= width
		&& requiredHeight(scale) <= height;

	const int top_height = TopChromeHeight * scale;
	const int bottom_height = BottomChromeHeight * scale;
	const int document_width = correctedDocumentWidth(scale);
	const int document_height = documentHeight(scale);
	const int group_width = groupWidth(scale, panel_visible);
	const int group_x = (width - group_width) / 2;
	const int document_y = (height - document_height) / 2;

	result.top_chrome = { 0, 0, width, top_height };
	result.bottom_chrome = { 0, height - bottom_height, width, bottom_height };

	if (panel_visible && panel_side == PanelSide::Left) {
		result.panel = {
			group_x,
			document_y,
			PanelWidth * scale,
			document_height
		};
		result.document = {
			group_x + ((PanelWidth + PanelGutter) * scale),
			document_y,
			document_width,
			document_height
		};
	} else {
		result.document = {
			group_x,
			document_y,
			document_width,
			document_height
		};
		if (panel_visible) {
			result.panel = {
				group_x + document_width + (PanelGutter * scale),
				document_y,
				PanelWidth * scale,
				document_height
			};
		}
	}

	return result;
}
