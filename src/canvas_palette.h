/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_CANVAS_PALETTE_H
#define FAIRYWRITER_CANVAS_PALETTE_H

#include "snes_color.h"

#include <cstddef>
#include <cstdint>

namespace FairyWriter {

// The canvas is the area of the window the 256x224 image does not cover. It is
// host chrome -- the guest cannot see it and has no register for it -- but the
// colour is still the user's choice, so the cartridge owns the *index* and the
// host owns what that index looks like. The index crosses as the fourth byte of
// CommandSetPersistenceSettings and comes back as the sixth byte of
// EventPersistenceSettings.
//
// Colours are built from SnesColor so the surround lives in the same
// five-bit-per-channel space as the screen it frames rather than in a wider one
// the SNES could never produce. That is also why there is no pure black here:
// SnesColor holds the product-wide invariant that zero is not a presentation
// colour, so the darkest entry is the black replacement itself, named INK.
//
// `names` is mirrored by canvasNames in tools/fairywriter-rom/main.go, which is
// what the settings plane actually draws. The two tables must stay in step, the
// same way soundFields there mirrors the host's SoundSettings payload; a name
// added on one side and not the other is a row that renders the wrong word.
struct CanvasPalette final {
	static constexpr std::size_t Count = 7;

	static constexpr SnesColor color(std::uint8_t index)
	{
		return entries[index < Count ? index : 0];
	}

	static constexpr std::uint8_t clamp(std::uint8_t index)
	{
		return index < Count ? index : 0;
	}

	static constexpr const char* name(std::uint8_t index)
	{
		return names[index < Count ? index : 0];
	}

	static constexpr SnesColor entries[Count] = {
		SnesColor::fromComponents(20, 28, 19), // PASTEL GREEN -- the default
		SnesColor::fromComponents(19, 25, 30), // MIST BLUE
		SnesColor::fromComponents(31, 29, 19), // BUTTER
		SnesColor::fromComponents(31, 22, 22), // BLUSH
		SnesColor::fromComponents(24, 21, 30), // LILAC
		SnesColor::fromComponents(10, 11, 14), // SLATE
		SnesColor()                            // INK -- the black replacement
	};

	static constexpr const char* names[Count] = {
		"PASTEL GREEN", "MIST BLUE", "BUTTER", "BLUSH", "LILAC", "SLATE", "INK"
	};
};

static_assert(CanvasPalette::color(0) != SnesColor(),
	"the default canvas must be the pastel green, not the black replacement");

} // namespace FairyWriter

#endif // FAIRYWRITER_CANVAS_PALETTE_H
