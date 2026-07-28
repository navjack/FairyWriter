/*
	SPDX-FileCopyrightText: 2026 Jack Mangano

	SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef FAIRYWRITER_SNES_COLOR_H
#define FAIRYWRITER_SNES_COLOR_H

#include <cstdint>

// SNES CGRAM stores red in bits 0..4, green in 5..9, and blue in 10..14.
// SnesColor owns the product-wide invariant that zero (pure black) is never a
// valid presentation color.
class SnesColor final
{
public:
	static constexpr std::uint16_t DefaultBlackReplacement =
		3u | (4u << 5u) | (6u << 10u);

	constexpr SnesColor()
		: m_value(DefaultBlackReplacement)
	{
	}

	static constexpr SnesColor fromBgr555(
		std::uint16_t value,
		SnesColor black_replacement = SnesColor())
	{
		value &= 0x7fffu;
		return SnesColor(value != 0u ? value : black_replacement.value());
	}

	static constexpr SnesColor fromComponents(
		std::uint8_t red,
		std::uint8_t green,
		std::uint8_t blue,
		SnesColor black_replacement = SnesColor())
	{
		const std::uint16_t value =
			(static_cast<std::uint16_t>(red) & 31u)
			| ((static_cast<std::uint16_t>(green) & 31u) << 5u)
			| ((static_cast<std::uint16_t>(blue) & 31u) << 10u);
		return fromBgr555(value, black_replacement);
	}

	static constexpr SnesColor fromRgb888(
		std::uint8_t red,
		std::uint8_t green,
		std::uint8_t blue,
		SnesColor black_replacement = SnesColor())
	{
		return fromComponents(
			quantizeChannel(red),
			quantizeChannel(green),
			quantizeChannel(blue),
			black_replacement);
	}

	constexpr std::uint16_t value() const { return m_value; }
	constexpr std::uint8_t red() const { return m_value & 31u; }
	constexpr std::uint8_t green() const { return (m_value >> 5u) & 31u; }
	constexpr std::uint8_t blue() const { return (m_value >> 10u) & 31u; }

	constexpr std::uint8_t red8() const { return expandChannel(red()); }
	constexpr std::uint8_t green8() const { return expandChannel(green()); }
	constexpr std::uint8_t blue8() const { return expandChannel(blue()); }

	static constexpr std::uint8_t quantizeChannel(std::uint8_t value)
	{
		return static_cast<std::uint8_t>((static_cast<unsigned>(value) * 31u + 127u) / 255u);
	}

	static constexpr std::uint8_t expandChannel(std::uint8_t value)
	{
		return static_cast<std::uint8_t>(((static_cast<unsigned>(value) & 31u) * 255u + 15u) / 31u);
	}

	friend constexpr bool operator==(SnesColor left, SnesColor right)
	{
		return left.value() == right.value();
	}

	friend constexpr bool operator!=(SnesColor left, SnesColor right)
	{
		return !(left == right);
	}

private:
	explicit constexpr SnesColor(std::uint16_t nonzero_value)
		: m_value(nonzero_value)
	{
	}

	std::uint16_t m_value;
};

static_assert(SnesColor().value() != 0u, "presentation black replacement must be nonzero");

#endif // FAIRYWRITER_SNES_COLOR_H
