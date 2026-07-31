#include "snes_machine.h"
#include "cartridge_image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::uint32_t ViewportSram = 0x704100;
constexpr std::uint32_t ViewportTextSram = ViewportSram + 0x0200;

std::uint8_t scanForAscii(char ch)
{
	switch (ch) {
	case 'a': return 0x1c; case 'b': return 0x32; case 'c': return 0x21;
	case 'd': return 0x23; case 'e': return 0x24; case 'f': return 0x2b;
	case 'g': return 0x34; case 'h': return 0x33; case 'i': return 0x43;
	case 'j': return 0x3b; case 'k': return 0x42; case 'l': return 0x4b;
	case 'm': return 0x3a; case 'n': return 0x31; case 'o': return 0x44;
	case 'p': return 0x4d; case 'q': return 0x15; case 'r': return 0x2d;
	case 's': return 0x1b; case 't': return 0x2c; case 'u': return 0x3c;
	case 'v': return 0x2a; case 'w': return 0x1d; case 'x': return 0x22;
	case 'y': return 0x35; case 'z': return 0x1a; case ' ': return 0x29;
	case '-': return 0x4e; case '.': return 0x49; case ',': return 0x41;
	case '/': return 0x4a; case '\'': return 0x52; case ';': return 0x4c;
	case ':': return 0x6c;
	default: return 0;
	}
}

bool runFrames(FairySnesMachine* machine, int count = 4)
{
	for (int i = 0; i < count; ++i) if (!fairy_snes_run_frame(machine)) return false;
	return true;
}

std::uint64_t blockHash(const FairySnesMachine* machine, int ox, int oy, int w, int h)
{
	const auto* fb = fairy_snes_framebuffer(machine);
	std::uint64_t hash = 1469598103934665603ull;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const std::uint32_t pixel = fb[(oy + y) * 256 + (ox + x)];
			hash ^= static_cast<std::uint64_t>(pixel);
			hash *= 1099511628211ull;
		}
	}
	return hash;
}

bool typeAscii(FairySnesMachine* machine, std::string_view text)
{
	for (const char ch : text) {
		const std::uint8_t scan = scanForAscii(ch);
		if (!scan || !fairy_snes_key_event(machine, scan, true, false) || !runFrames(machine)) return false;
	}
	return true;
}

bool wramEquals(FairySnesMachine* machine, std::uint32_t address, std::string_view expected)
{
	for (std::size_t i = 0; i < expected.size(); ++i) {
		if (fairy_snes_debug_wram(machine, address + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(expected[i])) return false;
	}
	return true;
}

std::string wramString(FairySnesMachine* machine, std::uint32_t address, std::size_t length)
{
	std::string value(length, '\0');
	for (std::size_t i = 0; i < length; ++i) {
		value[i] = static_cast<char>(
			fairy_snes_debug_wram(machine, address + static_cast<std::uint32_t>(i)));
	}
	return value;
}

void append16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
	bytes.push_back(static_cast<std::uint8_t>(value));
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void append32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
	append16(bytes, static_cast<std::uint16_t>(value));
	append16(bytes, static_cast<std::uint16_t>(value >> 16));
}

void append64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
	append32(bytes, static_cast<std::uint32_t>(value));
	append32(bytes, static_cast<std::uint32_t>(value >> 32));
}

void appendRecord(std::vector<std::uint8_t>& bytes, std::uint16_t kind,
	const std::vector<std::uint8_t>& payload)
{
	append16(bytes, 1);
	append16(bytes, kind);
	append16(bytes, static_cast<std::uint16_t>(payload.size()));
	append16(bytes, 0);
	append32(bytes, 0);
	append64(bytes, 0);
	bytes.insert(bytes.end(), payload.cbegin(), payload.cend());
}
}

int main(int argc, char** argv)
{
	if (argc < 2 || argc > 3) return 2;
	std::ifstream file(argv[1], std::ios::binary);
	std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>(file), {}};
	const auto* embedded = FairyWriter::cartridgeImage();
	const std::size_t embedded_size = FairyWriter::cartridgeImageSize();
	if (rom.size() != embedded_size ||
		!std::equal(rom.cbegin(), rom.cend(), embedded)) {
		std::fputs("production embedded cartridge differs from generated cartridge\n", stderr);
		return 54;
	}
	FairySnesMachine* machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine) return 3;
	// Port 1 is a real 32-bit SNES Mouse report, independent from the port-2
	// XBAND keyboard. Verify latch, MSB-first serialization, sign/magnitude
	// motion, button bits, and consumption of motion after a report.
	fairy_snes_mouse_event(machine, 5, -3, true, false);
	fairy_snes_debug_bus_write(machine, 0x004016, 1);
	fairy_snes_debug_bus_write(machine, 0x004016, 0);
	std::uint32_t mouse_report = 0;
	for (int bit = 0; bit < 32; ++bit) {
		mouse_report = (mouse_report << 1) | (fairy_snes_debug_bus_read(machine, 0x004016) & 1);
	}
	if (mouse_report != 0x00418305u || fairy_snes_debug_bus_read(machine, 0x004016) != 1) {
		std::fprintf(stderr, "SNES mouse report mismatch: %08x\n", mouse_report);
		return 97;
	}
	fairy_snes_debug_bus_write(machine, 0x004016, 1);
	fairy_snes_debug_bus_write(machine, 0x004016, 0);
	std::uint32_t mouse_stationary = 0;
	for (int bit = 0; bit < 32; ++bit) {
		mouse_stationary = (mouse_stationary << 1) | (fairy_snes_debug_bus_read(machine, 0x004016) & 1);
	}
	if (mouse_stationary != 0x00410000u) {
		std::fprintf(stderr, "SNES mouse did not consume latched motion: %08x\n", mouse_stationary);
		return 98;
	}
	// A desktop click can press and release between adjacent 60 Hz guest polls.
	// The peripheral boundary must retain that rising edge for one report rather
	// than letting the release overwrite it before the cartridge can latch.
	fairy_snes_mouse_event(machine, 0, 0, false, false);
	fairy_snes_mouse_event(machine, 0, 0, true, false);
	fairy_snes_mouse_event(machine, 0, 0, false, false);
	fairy_snes_debug_bus_write(machine, 0x004016, 1);
	fairy_snes_debug_bus_write(machine, 0x004016, 0);
	std::uint32_t quick_click = 0;
	for (int bit = 0; bit < 32; ++bit) {
		quick_click = (quick_click << 1) | (fairy_snes_debug_bus_read(machine, 0x004016) & 1);
	}
	fairy_snes_debug_bus_write(machine, 0x004016, 1);
	fairy_snes_debug_bus_write(machine, 0x004016, 0);
	std::uint32_t after_quick_click = 0;
	for (int bit = 0; bit < 32; ++bit) {
		after_quick_click = (after_quick_click << 1) | (fairy_snes_debug_bus_read(machine, 0x004016) & 1);
	}
	if (quick_click != 0x00410000u || after_quick_click != 0x00010000u) {
		std::fprintf(stderr, "SNES mouse lost or stuck a sub-frame click: press=%08x release=%08x\n",
			quick_click, after_quick_click);
		return 99;
	}
	for (int i = 0; i < 3; ++i) {
		if (!fairy_snes_run_frame(machine)) return 4;
	}
	if (fairy_snes_debug_bus_read(machine, 0x700000) != 1 ||
		fairy_snes_debug_bus_read(machine, 0x700001) != 0 ||
		fairy_snes_debug_bus_read(machine, 0x700002) != 0 ||
		fairy_snes_debug_bus_read(machine, 0x700004) != 0 ||
		fairy_snes_debug_bus_read(machine, 0x700006) != 0 ||
		fairy_snes_debug_bus_read(machine, 0x700008) != 0) {
		std::fprintf(stderr, "mailbox SRAM header was not initialized: %u %u %u %u %u %u\n",
			fairy_snes_debug_bus_read(machine, 0x700000), fairy_snes_debug_bus_read(machine, 0x700001),
			fairy_snes_debug_bus_read(machine, 0x700002), fairy_snes_debug_bus_read(machine, 0x700004),
			fairy_snes_debug_bus_read(machine, 0x700006), fairy_snes_debug_bus_read(machine, 0x700008));
		return 31;
	}
	// A non-empty host viewport replaces the guest demo buffer and drives the
	// same cartridge renderer used for local editing.
	if (!machine || !runFrames(machine, 3)) return 11;

	fairy_snes_debug_bus_write(machine, ViewportSram + 8, 7); // global cursor
	fairy_snes_debug_bus_write(machine, ViewportSram + 20, 5); // viewport text offset
	fairy_snes_debug_bus_write(machine, ViewportSram + 28, 42); // live word count
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, 3); // UTF-8 byte count
	fairy_snes_debug_bus_write(machine, ViewportSram + 34, 7); // chapter
	fairy_snes_debug_bus_write(machine, ViewportSram + 124, 2); // format run count low
	fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0); // format run count high
	// Run 0: offset 1, length 1, spelling flag (bit 3)
	fairy_snes_debug_bus_write(machine, ViewportSram + 384, 1);
	fairy_snes_debug_bus_write(machine, ViewportSram + 385, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 386, 1);
	fairy_snes_debug_bus_write(machine, ViewportSram + 387, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 388, 0x08);
	// Run 1: offset 2, length 1, grammar flag (bit 4)
	fairy_snes_debug_bus_write(machine, ViewportSram + 392, 2);
	fairy_snes_debug_bus_write(machine, ViewportSram + 393, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 394, 1);
	fairy_snes_debug_bus_write(machine, ViewportSram + 395, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 396, 0x10);
	constexpr std::string_view title = "STORY";
	fairy_snes_debug_bus_write(machine, ViewportSram + 36, title.size());
	for (std::size_t i = 0; i < title.size(); ++i) {
		fairy_snes_debug_bus_write(machine, ViewportSram + 37 + static_cast<std::uint32_t>(i), title[i]);
	}
	fairy_snes_debug_bus_write(machine, ViewportTextSram + 0, 'v');
	fairy_snes_debug_bus_write(machine, ViewportTextSram + 1, 'i');
	fairy_snes_debug_bus_write(machine, ViewportTextSram + 2, 'e');
	fairy_snes_debug_bus_write(machine, 0x70000a, 2); // commit the alternate slot generation
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x08) != 3 ||
		fairy_snes_debug_wram(machine, 0x00) != 2 || fairy_snes_debug_wram(machine, 0x0500) != 'v' ||
		fairy_snes_debug_wram(machine, 0x0501) != 'i' || fairy_snes_debug_wram(machine, 0x0502) != 'e') {
		return 39;
	}
	if (!wramEquals(machine, 0x1400, "STORY")
		|| !wramEquals(machine, 0x1480, "CHAPTER 07          WORDS 0042")
		|| fairy_snes_debug_wram(machine, 0x1500) != 0x08
		|| fairy_snes_debug_wram(machine, 0x1580) != 0x0c
		|| fairy_snes_debug_wram(machine, 0x1594) != 0x00
		|| fairy_snes_debug_wram(machine, 0x0b00) != 0x00
		|| fairy_snes_debug_wram(machine, 0x0b01) != 0x08
		|| fairy_snes_debug_wram(machine, 0x0b02) != 0x10
		|| fairy_snes_debug_wram(machine, 0x1200) != 0x08
		|| fairy_snes_debug_wram(machine, 0x1201) != 0x0c
		|| fairy_snes_debug_wram(machine, 0x1202) != 0x04) {
		std::fprintf(stderr,
			"viewport metadata did not drive cartridge header staging: title0=%02x status0=%02x status20=%02x proof=%02x %02x %02x attr=%02x %02x %02x\\n",
			fairy_snes_debug_wram(machine, 0x1400), fairy_snes_debug_wram(machine, 0x1500),
			fairy_snes_debug_wram(machine, 0x1580), fairy_snes_debug_wram(machine, 0x0b00),
			fairy_snes_debug_wram(machine, 0x0b01), fairy_snes_debug_wram(machine, 0x0b02),
			fairy_snes_debug_wram(machine, 0x1200), fairy_snes_debug_wram(machine, 0x1201),
			fairy_snes_debug_wram(machine, 0x1202));
		return 60;
	}
	// The wire remains UTF-8, but the bounded resident plane must consume one
	// scalar per cell. Typographic punctuation receives readable ASCII
	// fallbacks and continuation bytes never become independent tiles.
	const std::uint8_t quoted[] = {0xe2, 0x80, 0x9c, 'h', 'i', 0xe2, 0x80, 0x9d, '\n'};
	fairy_snes_debug_bus_write(machine, ViewportSram + 8, 5); // UTF-16 cursor after \u201chi\u201d and newline
	fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, sizeof(quoted));
	fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0); // clear format run count
	fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
	for (std::size_t i = 0; i < sizeof(quoted); ++i) {
		fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), quoted[i]);
	}
	fairy_snes_debug_bus_write(machine, 0x70000a, 0);
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x08) != 5 ||
		fairy_snes_debug_wram(machine, 0x00) != 5 || fairy_snes_debug_wram(machine, 0x0500) != '"' ||
		fairy_snes_debug_wram(machine, 0x0501) != 'h' || fairy_snes_debug_wram(machine, 0x0502) != 'i' ||
		fairy_snes_debug_wram(machine, 0x0503) != '\"' || fairy_snes_debug_wram(machine, 0x0504) != 0x0d ||
		fairy_snes_debug_wram(machine, 0x0b00) != 0x00 || fairy_snes_debug_wram(machine, 0x0b01) != 0x00 ||
		fairy_snes_debug_wram(machine, 0x0b02) != 0x00 || fairy_snes_debug_wram(machine, 0x0b03) != 0x00) {
		std::fprintf(stderr,
			"UTF-8 punctuation fallback failed: len=%u cursor=%u text=%02x %02x %02x %02x %02x proof=%02x %02x %02x %02x\\n",
			fairy_snes_debug_wram(machine, 0x08), fairy_snes_debug_wram(machine, 0x00),
			fairy_snes_debug_wram(machine, 0x0500), fairy_snes_debug_wram(machine, 0x0501),
			fairy_snes_debug_wram(machine, 0x0502), fairy_snes_debug_wram(machine, 0x0503), fairy_snes_debug_wram(machine, 0x0504),
			fairy_snes_debug_wram(machine, 0x0b00), fairy_snes_debug_wram(machine, 0x0b01),
			fairy_snes_debug_wram(machine, 0x0b02), fairy_snes_debug_wram(machine, 0x0b03));
		return 58;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 47;
	if (!fairy_snes_key_event(machine, 0x12, true, false) || !fairy_snes_key_event(machine, 0x4e, true, false) || !runFrames(machine)) return 48;
	if (!fairy_snes_key_event(machine, 0x12, false, false) || fairy_snes_debug_wram(machine, 0x08) != 1 || fairy_snes_debug_wram(machine, 0x0500) != '_') {
		std::fprintf(stderr, "SHIFT TEST: 0x08=%u 0x0500=0x%02x('%c')\n",
			fairy_snes_debug_wram(machine, 0x08), fairy_snes_debug_wram(machine, 0x0500), fairy_snes_debug_wram(machine, 0x0500));
		return 49;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 62;
	{
		// Visual regression guard: typing into an empty document must change the
		// first document cell's rendered pixel block. If this hash remains stable,
		// the keyboard/mailbox path may work while body text remains invisible.
		// This specifically catches render paths that update WRAM stage buffers
		// but fail to commit BG map uploads into VRAM/PPU output.
		const auto* beforeFrame = fairy_snes_framebuffer(machine);
		std::array<std::uint32_t, 256 * 224> beforePixels{};
		if (beforeFrame) std::copy(beforeFrame, beforeFrame + beforePixels.size(), beforePixels.begin());
		const std::uint8_t stage0_before = fairy_snes_debug_wram(machine, 0x1000);
		const std::uint8_t attr0_before = fairy_snes_debug_wram(machine, 0x1200);
		const std::uint16_t vram0_before = fairy_snes_debug_vram_word(machine, 0x0141);
		const std::uint64_t before = blockHash(machine, 8, 80, 8, 8);
		if (!typeAscii(machine, "a") || !runFrames(machine, 2)) return 91;
		const std::uint8_t stage0_after = fairy_snes_debug_wram(machine, 0x1000);
		const std::uint8_t attr0_after = fairy_snes_debug_wram(machine, 0x1200);
		const std::uint16_t vram0_after = fairy_snes_debug_vram_word(machine, 0x0141);
		const std::uint64_t after = blockHash(machine, 8, 80, 8, 8);
		if (fairy_snes_debug_wram(machine, 0x1000) != 'a' || fairy_snes_debug_wram(machine, 0x1001) != 0x7f) {
			std::fprintf(stderr, "document glyph/caret staging mismatch after typing: cell0=%02x cell1=%02x\\n",
				fairy_snes_debug_wram(machine, 0x1000), fairy_snes_debug_wram(machine, 0x1001));
			return 166;
		}
		for (int col = 0; col < 30; ++col) {
			if (fairy_snes_debug_wram(machine, 0x1200 + col) != 0x08) {
				std::fprintf(stderr, "document row-0 attribute drift at col=%d attr=%02x\\n", col,
					fairy_snes_debug_wram(machine, 0x1200 + col));
				return 167;
			}
		}
		std::size_t changed = 0;
		const auto* afterFrame = fairy_snes_framebuffer(machine);
		if (afterFrame) {
			for (std::size_t i = 0; i < beforePixels.size(); ++i) {
				if (beforePixels[i] != afterFrame[i]) ++changed;
			}
		}
		if (before == after) {
			std::fprintf(stderr,
				"typed glyph did not alter first document cell pixels (stage=%u->%u attr=%u->%u vram=%04x->%04x framebuffer-diff=%zu)\\n",
				stage0_before, stage0_after, attr0_before, attr0_after, vram0_before, vram0_after,
				changed);
			return 92;
		}
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 47;
	if (!fairy_snes_key_event(machine, 0x12, true, false) || !runFrames(machine)) return 63;
	const struct { std::uint8_t scan; std::uint16_t command; } shifted_navigation[] = {
		{0x6b, 12}, {0x72, 40}, {0x74, 13}, {0x75, 39}, {0x6c, 41}, {0x69, 42}
	};
	constexpr std::uint32_t navigation_command = 0x700100;
	for (std::size_t i = 0; i < std::size(shifted_navigation); ++i) {
		if (!fairy_snes_key_event(machine, shifted_navigation[i].scan, true, true) || !runFrames(machine, 2)
			|| fairy_snes_debug_bus_read(machine, navigation_command + static_cast<std::uint32_t>(i * 20) + 2)
				!= static_cast<std::uint8_t>(shifted_navigation[i].command)) {
			std::fprintf(stderr, "shifted navigation command %zu was not serialized\n", i);
			return 64;
		}
	}
	if (fairy_snes_debug_bus_read(machine, 0x700002) != sizeof(shifted_navigation) / sizeof(shifted_navigation[0]) * 20) return 65;
	// F2 opens the cartridge-owned help card (mode $0f) from the document, and
	// F1 closes it back to the document. The help plane is a whole screen like
	// the menu, not an overlay on the browser-ready plane, so mode $0f has to be
	// dispatched before the "mode >= 5" browser branch swallows it.
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 30;
	if (fairy_snes_debug_wram(machine, 0x031d) != 0) return 30;
	if (!fairy_snes_key_event(machine, 0x06, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0x0f
		|| !wramEquals(machine, 0x1008, "FAIRYWRITER HELP")
		|| !wramEquals(machine, 0x1000 + 30 * 7 + 2, "F1 OR BACK RETURNS")) {
		std::fputs("F2 did not open the cartridge-owned help card\n", stderr);
		return 30;
	}
	// No selected row: help must not inherit the menu's highlight caret.
	if (fairy_snes_debug_wram(machine, 0x1000 + 30) == '>') {
		std::fputs("help card drew a menu selection caret\n", stderr);
		return 43;
	}
	if (!fairy_snes_key_event(machine, 0x06, false, false) || !runFrames(machine, 1)) return 43;
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0) {
		std::fputs("F1 did not close the help card back to the document\n", stderr);
		return 50;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 66;
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 1
		|| fairy_snes_debug_wram(machine, 0x031e) != 0
		// The title row also carries the F2 help hint, which is what makes the
		// help card discoverable at all, so assert the whole row.
		|| !wramEquals(machine, 0x1003, "FAIRYWRITER MENU   F2 HELP")
		|| fairy_snes_debug_wram(machine, 0x1000 + 30) != '>') {
		std::fputs("F1 did not open the cartridge-owned main menu\n", stderr);
		return 66;
	}
	// Help is not allowed to flatten every origin back to the document. Preserve
	// both the menu mode and its current selection, then exercise Backspace as
	// the second advertised dismissal path.
	if (!fairy_snes_key_event(machine, 0x05, false, false) || !runFrames(machine)) return 84;
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031e) != 1) {
		std::fputs("menu selection did not advance before help-origin test\n", stderr);
		return 84;
	}
	if (!fairy_snes_key_event(machine, 0x72, false, true) || !runFrames(machine)) return 85;
	if (!fairy_snes_key_event(machine, 0x06, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0x0f
		|| fairy_snes_debug_wram(machine, 0x0313) != 1
		|| fairy_snes_debug_wram(machine, 0x031e) != 1) {
		std::fputs("F2 did not preserve the menu as the help return mode\n", stderr);
		return 85;
	}
	if (!fairy_snes_key_event(machine, 0x06, false, false) || !runFrames(machine)) return 86;
	if (!fairy_snes_key_event(machine, 0x66, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 1
		|| fairy_snes_debug_wram(machine, 0x031e) != 1
		|| !wramEquals(machine, 0x1003, "FAIRYWRITER MENU   F2 HELP")
		|| fairy_snes_debug_wram(machine, 0x1000 + 30 * 2) != '>') {
		std::fputs("Backspace did not return help to the preserved menu state\n", stderr);
		return 86;
	}
	if (!fairy_snes_key_event(machine, 0x66, false, false) || !runFrames(machine)) return 87;
	// A real port-1 packet moves from the centered pointer to menu row 1 and
	// its left-button edge follows the same cartridge menu path as Enter.
	fairy_snes_mouse_event(machine, 0, 0, false, false);
	if (!runFrames(machine, 1)) return 67;
	fairy_snes_mouse_event(machine, 0, -96, true, false);
	if (!runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_wram(machine, 0x031e) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 0) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 2) != 0x07
		|| fairy_snes_debug_bus_read(machine, navigation_command + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, navigation_command + 4) != 0
		|| !wramEquals(machine, 0x1009, "FILE BROWSER")) {
		std::fputs("SNES mouse click did not activate the cartridge Open menu item\n", stderr);
		return 68;
	}
	constexpr std::string_view root_id = "0123456789abcdef01234567";
	constexpr std::string_view root_name = "HOME";
	std::vector<std::uint8_t> file_payload;
	file_payload.push_back(static_cast<std::uint8_t>(root_id.size()));
	append16(file_payload, static_cast<std::uint16_t>(root_name.size()));
	file_payload.push_back(3); // directory and writable
	append64(file_payload, 0);
	append64(file_payload, 0);
	file_payload.insert(file_payload.end(), root_id.cbegin(), root_id.cend());
	file_payload.insert(file_payload.end(), root_name.cbegin(), root_name.cend());
	std::vector<std::uint8_t> event_wire;
	appendRecord(event_wire, 0x8200, file_payload);
	std::vector<std::uint8_t> complete_payload;
	append32(complete_payload, 1);
	append32(complete_payload, 0);
	complete_payload.push_back(1);
	complete_payload.push_back(2);
	complete_payload.push_back(0);
	appendRecord(event_wire, 0x820f, complete_payload);
	for (std::size_t i = 0; i < event_wire.size(); ++i) {
		fairy_snes_debug_bus_write(machine, 0x702100 + static_cast<std::uint32_t>(i), event_wire[i]);
	}
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(event_wire.size()));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(event_wire.size() >> 8));
	if (!runFrames(machine, 3)
		|| fairy_snes_debug_bus_read(machine, 0x700008) != event_wire.size()
		|| fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x031f) != 1
		|| !wramEquals(machine, 0x1600, root_id)
		|| !wramEquals(machine, 0x1700, root_name)
		|| fairy_snes_debug_wram(machine, 0x17e0) != 3
		|| fairy_snes_debug_wram(machine, 0x1000 + 30) != '>'
		|| fairy_snes_debug_wram(machine, 0x1000 + 31) != '/'
		|| !wramEquals(machine, 0x1000 + 32, root_name)) {
		std::fprintf(stderr,
			"host file page did not become a cartridge browser: consumer=%u mode=%u count=%u row=%c%c%c%c%c%c\n",
			fairy_snes_debug_bus_read(machine, 0x700008),
			fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_wram(machine, 0x031f),
			fairy_snes_debug_wram(machine, 0x1000 + 30),
			fairy_snes_debug_wram(machine, 0x1000 + 31),
			fairy_snes_debug_wram(machine, 0x1000 + 32),
			fairy_snes_debug_wram(machine, 0x1000 + 33),
			fairy_snes_debug_wram(machine, 0x1000 + 34),
			fairy_snes_debug_wram(machine, 0x1000 + 35));
		return 70;
	}
	// Selecting the directory must preserve the opaque catalog ID byte-for-byte
	// in a second command record, then return the cartridge to a loading state.
	// This exercises the real keyboard -> guest menu -> SRAM -> host boundary;
	// a rendered row by itself is not proof that navigation can work.
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_bus_read(machine, navigation_command + 20) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 22) != 0
		|| fairy_snes_debug_bus_read(machine, navigation_command + 23) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 24) != root_id.size()
		|| !wramEquals(machine, 0x1800, root_id)) {
		std::fprintf(stderr,
			"directory selection did not emit a typed list-files command: key=%u mode=%u producer=%u kind=%u/%u count=%u staged=%c%c%c%c\n",
			fairy_snes_debug_wram(machine, 0x0001),
			fairy_snes_debug_wram(machine, 0x031d), fairy_snes_debug_bus_read(machine, 0x700002),
			fairy_snes_debug_bus_read(machine, navigation_command + 22), fairy_snes_debug_bus_read(machine, navigation_command + 23),
			fairy_snes_debug_bus_read(machine, navigation_command + 24),
			fairy_snes_debug_wram(machine, 0x1800), fairy_snes_debug_wram(machine, 0x1801),
			fairy_snes_debug_wram(machine, 0x1802), fairy_snes_debug_wram(machine, 0x1803));
		return 71;
	}
	for (std::size_t i = 0; i < root_id.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, navigation_command + 40 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(root_id[i])) {
			std::fputs("directory command did not preserve its opaque catalog ID\n", stderr);
			return 72;
		}
	}
	if (fairy_snes_debug_bus_read(machine, 0x700002) != 64) {
		std::fputs("directory command producer index did not include its payload\n", stderr);
		return 73;
	}
	// Simulate the host's response to that directory request, then select a
	// regular file. This proves that the browser emits CommandOpenFile rather
	// than merely changing its local mode for the happy-path UI.
	constexpr std::string_view story_id = "story-opaque-id";
	constexpr std::string_view story_name = "MARIA.ODT";
	std::vector<std::uint8_t> story_payload;
	story_payload.push_back(static_cast<std::uint8_t>(story_id.size()));
	append16(story_payload, static_cast<std::uint16_t>(story_name.size()));
	story_payload.push_back(2); // writable regular file
	append64(story_payload, 0);
	append64(story_payload, 0);
	story_payload.insert(story_payload.end(), story_id.cbegin(), story_id.cend());
	story_payload.insert(story_payload.end(), story_name.cbegin(), story_name.cend());
	std::vector<std::uint8_t> nested_events;
	appendRecord(nested_events, 0x8200, story_payload);
	complete_payload[9] = 1;
	appendRecord(nested_events, 0x820f, complete_payload);
	for (std::size_t i = 0; i < nested_events.size(); ++i) {
		fairy_snes_debug_bus_write(machine,
			0x702100 + static_cast<std::uint32_t>(event_wire.size() + i), nested_events[i]);
	}
	const std::size_t nested_producer = event_wire.size() + nested_events.size();
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(nested_producer));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(nested_producer >> 8));
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x031f) != 1 || !wramEquals(machine, 0x1600, story_id)) {
		std::fputs("nested host page did not restore the cartridge browser\n", stderr);
		return 74;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0
		|| fairy_snes_debug_bus_read(machine, navigation_command + 64) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 66) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 67) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 68) != story_id.size()) {
		std::fputs("file selection did not emit a typed open-file command\n", stderr);
		return 75;
	}
	for (std::size_t i = 0; i < story_id.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, navigation_command + 84 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(story_id[i])) {
			std::fputs("open-file command did not preserve its opaque catalog ID\n", stderr);
			return 76;
		}
	}
	// A host-side open failure must become a cartridge-owned outcome screen,
	// not disappear or turn into a native alert. Enter returns to the exact
	// document mode that issued the request.
	std::vector<std::uint8_t> open_failed;
	appendRecord(open_failed, 0x8209, {});
	for (std::size_t i = 0; i < open_failed.size(); ++i) {
		fairy_snes_debug_bus_write(machine,
			0x702100 + static_cast<std::uint32_t>(nested_producer + i), open_failed[i]);
	}
	const std::size_t outcome_producer = nested_producer + open_failed.size();
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(outcome_producer));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(outcome_producer >> 8));
	if (!runFrames(machine, 2) || fairy_snes_debug_wram(machine, 0x031d) != 11
		|| fairy_snes_debug_wram(machine, 0x0337) != 9
		|| !wramEquals(machine, 0x1096, "OPERATION FAILED")
		|| !wramEquals(machine, 0x10b4, "CANNOT OPEN FILE")
		|| !wramEquals(machine, 0x10d2, "ENTER OR BACK")) {
		std::fputs("open failure did not render a cartridge-owned outcome dialog\n", stderr);
		return 99;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0) {
		std::fputs("outcome dialog did not restore its cartridge return mode\n", stderr);
		return 100;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 40;
	// Save As is a two-party transaction: selecting an existing file first asks
	// the host, then only an explicit cartridge confirmation may set overwrite.
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)) return 77;
	for (int i = 0; i < 3; ++i) {
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 78;
	}
	if (fairy_snes_debug_wram(machine, 0x031d) != 1 || fairy_snes_debug_wram(machine, 0x031e) != 3
		|| !fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0x11
		|| fairy_snes_debug_wram(machine, 0x032f) != 0
		|| !wramEquals(machine, 0x1008, "SAVE AS FORMAT")
		|| !fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 3) {
		std::fputs("Save As did not select ODT before entering its cartridge loading state\n", stderr);
		return 79;
	}
	constexpr std::string_view overwrite_id = "existing-save-target";
	constexpr std::string_view overwrite_name = "EXISTING.ODT";
	std::vector<std::uint8_t> overwrite_file_payload;
	overwrite_file_payload.push_back(static_cast<std::uint8_t>(overwrite_id.size()));
	append16(overwrite_file_payload, static_cast<std::uint16_t>(overwrite_name.size()));
	overwrite_file_payload.push_back(2); // writable regular file
	append64(overwrite_file_payload, 0);
	append64(overwrite_file_payload, 0);
	overwrite_file_payload.insert(overwrite_file_payload.end(), overwrite_id.cbegin(), overwrite_id.cend());
	overwrite_file_payload.insert(overwrite_file_payload.end(), overwrite_name.cbegin(), overwrite_name.cend());
	std::vector<std::uint8_t> save_as_page;
	appendRecord(save_as_page, 0x8200, overwrite_file_payload);
	std::vector<std::uint8_t> save_as_complete;
	append32(save_as_complete, 1);
	append32(save_as_complete, 0);
	save_as_complete.push_back(1);
	save_as_complete.push_back(2);
	save_as_complete.push_back(0);
	appendRecord(save_as_page, 0x820f, save_as_complete);
	for (std::size_t i = 0; i < save_as_page.size(); ++i) {
		fairy_snes_debug_bus_write(machine, 0x702100 + static_cast<std::uint32_t>(i), save_as_page[i]);
	}
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(save_as_page.size()));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(save_as_page.size() >> 8));
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 6
		|| !wramEquals(machine, 0x1600, overwrite_id)) {
		std::fputs("Save As host page did not become a cartridge browser\n", stderr);
		return 80;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 8
		|| fairy_snes_debug_bus_read(machine, navigation_command + 22) != 2
		|| fairy_snes_debug_bus_read(machine, navigation_command + 23) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + 26) != 0) {
		std::fputs("Save As selection did not issue an unconfirmed overwrite request\n", stderr);
		return 81;
	}
	std::vector<std::uint8_t> overwrite_event;
	overwrite_event.insert(overwrite_event.end(), overwrite_id.cbegin(), overwrite_id.cend());
	std::vector<std::uint8_t> overwrite_wire;
	appendRecord(overwrite_wire, 0x8206, overwrite_event);
	for (std::size_t i = 0; i < overwrite_wire.size(); ++i) {
		fairy_snes_debug_bus_write(machine,
			0x702100 + static_cast<std::uint32_t>(save_as_page.size() + i), overwrite_wire[i]);
	}
	const std::size_t overwrite_producer = save_as_page.size() + overwrite_wire.size();
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(overwrite_producer));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(overwrite_producer >> 8));
	if (!runFrames(machine, 2) || fairy_snes_debug_wram(machine, 0x031d) != 9
		|| !wramEquals(machine, 0x1096, "OVERWRITE FILE?")
		|| !wramEquals(machine, 0x10b4, "ENTER YES")
		|| !wramEquals(machine, 0x10d2, "BACK CANCEL")) {
		std::fputs("host overwrite event did not produce a cartridge confirmation\n", stderr);
		return 82;
	}
	const std::size_t confirmed_offset = 40 + overwrite_id.size();
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0
		|| fairy_snes_debug_bus_read(machine, navigation_command + confirmed_offset + 2) != 2
		|| fairy_snes_debug_bus_read(machine, navigation_command + confirmed_offset + 3) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + confirmed_offset + 4) != overwrite_id.size()
		|| fairy_snes_debug_bus_read(machine, navigation_command + confirmed_offset + 6) != 1) {
		std::fputs("explicit confirmation did not emit a flagged Save As command\n", stderr);
		return 83;
	}
	for (std::size_t i = 0; i < overwrite_id.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, navigation_command + confirmed_offset + 20 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(overwrite_id[i])) {
			std::fputs("confirmed Save As command did not preserve its opaque ID\n", stderr);
			return 84;
		}
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 85;
	// New-file Save As uses the current browser parent as the sole destination
	// authority. Back must clear/change that parent rather than retaining the
	// last directory entered, and the cartridge must expose the otherwise
	// non-obvious N action before it accepts a guest-owned filename.
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)) return 86;
	for (int i = 0; i < 3; ++i) {
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 87;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0x11
		|| !fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 3) return 88;
	constexpr std::string_view parent_id = "save-parent-directory";
	constexpr std::string_view parent_name = "WRITING";
	std::vector<std::uint8_t> parent_payload;
	parent_payload.push_back(static_cast<std::uint8_t>(parent_id.size()));
	append16(parent_payload, static_cast<std::uint16_t>(parent_name.size()));
	parent_payload.push_back(3); // directory plus writable
	append64(parent_payload, 0);
	append64(parent_payload, 0);
	parent_payload.insert(parent_payload.end(), parent_id.cbegin(), parent_id.cend());
	parent_payload.insert(parent_payload.end(), parent_name.cbegin(), parent_name.cend());
	std::vector<std::uint8_t> parent_page;
	appendRecord(parent_page, 0x8200, parent_payload);
	appendRecord(parent_page, 0x820f, save_as_complete);
	for (std::size_t i = 0; i < parent_page.size(); ++i) {
		fairy_snes_debug_bus_write(machine, 0x702100 + static_cast<std::uint32_t>(i), parent_page[i]);
	}
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(parent_page.size()));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(parent_page.size() >> 8));
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 6
		|| fairy_snes_debug_wram(machine, 0x0348) != 0
		|| !wramEquals(machine, 0x1000, " CHOOSE A FOLDER FOR NEW FILE ")
		|| !fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 3
		|| fairy_snes_debug_wram(machine, 0x0348) != parent_id.size()
		|| !wramEquals(machine, 0x1900, parent_id)) {
		std::fputs("Save As root did not become the current browser parent\n", stderr);
		return 89;
	}
	std::vector<std::uint8_t> empty_save_page;
	appendRecord(empty_save_page, 0x820f, save_as_complete);
	for (std::size_t i = 0; i < empty_save_page.size(); ++i) {
		fairy_snes_debug_bus_write(machine,
			0x702100 + static_cast<std::uint32_t>(parent_page.size() + i), empty_save_page[i]);
	}
	const std::size_t empty_save_producer = parent_page.size() + empty_save_page.size();
	fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(empty_save_producer));
	fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(empty_save_producer >> 8));
	if (!runFrames(machine, 2) || fairy_snes_debug_wram(machine, 0x031d) != 6
		|| !wramEquals(machine, 0x1000, "  PRESS N: NEW FILE IN FOLDER ")
		|| !fairy_snes_key_event(machine, 0x66, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 3
		|| fairy_snes_debug_wram(machine, 0x0348) != 0) {
		std::fputs("Save As Back did not restore the root destination context\n", stderr);
		return 89;
	}
	for (std::size_t i = 0; i < parent_page.size(); ++i) {
		fairy_snes_debug_bus_write(machine,
			0x702100 + static_cast<std::uint32_t>(empty_save_producer + i), parent_page[i]);
	}
	const std::size_t restored_root_producer = empty_save_producer + parent_page.size();
	fairy_snes_debug_bus_write(machine, 0x700006,
		static_cast<std::uint8_t>(restored_root_producer));
	fairy_snes_debug_bus_write(machine, 0x700007,
		static_cast<std::uint8_t>(restored_root_producer >> 8));
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 6
		|| !wramEquals(machine, 0x1000, " CHOOSE A FOLDER FOR NEW FILE ")
		|| !fairy_snes_key_event(machine, 0x31, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 6
		|| !fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 3) {
		std::fputs("Save As root accepted N or failed to re-enter its selected folder\n", stderr);
		return 89;
	}
	for (std::size_t i = 0; i < empty_save_page.size(); ++i) {
		fairy_snes_debug_bus_write(machine,
			0x702100 + static_cast<std::uint32_t>(restored_root_producer + i),
			empty_save_page[i]);
	}
	const std::size_t restored_folder_producer =
		restored_root_producer + empty_save_page.size();
	fairy_snes_debug_bus_write(machine, 0x700006,
		static_cast<std::uint8_t>(restored_folder_producer));
	fairy_snes_debug_bus_write(machine, 0x700007,
		static_cast<std::uint8_t>(restored_folder_producer >> 8));
	if (!runFrames(machine, 2) || fairy_snes_debug_wram(machine, 0x031d) != 6
		|| fairy_snes_debug_wram(machine, 0x0348) != parent_id.size()
		|| !wramEquals(machine, 0x1900, parent_id)
		|| !wramEquals(machine, 0x1000, "  PRESS N: NEW FILE IN FOLDER ")
		|| !fairy_snes_key_event(machine, 0x31, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 10) {
		std::fputs("Save As New did not enter the cartridge filename screen\n", stderr);
		return 90;
	}
	constexpr std::string_view new_name = "new";
	constexpr std::string_view committed_name = "new.odt";
	// Filename entry is a complete modal, not an overlay on stale browser rows.
	// It owns the title card, replaces formatting with Save/Cancel, exposes the
	// keyboard focus ring, and renders a field exactly as wide as its buffer.
	if (!wramEquals(machine, 0x1007, "SAVE NEW DOCUMENT")
		|| !wramEquals(machine, 0x1000 + 30, "  ENTER A NAME FOR THE FILE")
		|| !wramEquals(machine, 0x1000 + 3 * 30 + 2, "+------------------------+")
		|| !wramEquals(machine, 0x1400, "SAVE NEW FILE")
		|| !wramEquals(machine, 0x1480, "TAB/ARROWS MOVE  ENTER SELECTS")
		|| !wramEquals(machine, 0x1c01, "[  SAVE  ]")
		|| !wramEquals(machine, 0x1c0c, "[ CANCEL ]")
		|| fairy_snes_debug_wram(machine, 0x036d) != 0
		|| fairy_snes_debug_wram(machine, 0x125c) != 0x04
		|| fairy_snes_debug_wram(machine, 0x1c20) != 0x08) {
		std::fprintf(stderr,
			"filename mode did not own a fresh modal screen with field focus: mode=%u focus=%u "
			"field=%02x save=%02x lower='%.30s' title='%.18s' toolbar='%.22s'\n",
			fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_wram(machine, 0x036d),
			fairy_snes_debug_wram(machine, 0x125c),
			fairy_snes_debug_wram(machine, 0x1c20),
			wramString(machine, 0x1000, 30).c_str(),
			wramString(machine, 0x1400, 18).c_str(),
			wramString(machine, 0x1c00, 22).c_str());
		return 91;
	}
	// Down/Up plus physical Tab traverse the same name/save/cancel ring and the
	// selected control receives the ordinary menu highlight palette.
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x036d) != 1
		|| fairy_snes_debug_wram(machine, 0x1c20) != 0x04
		|| fairy_snes_debug_wram(machine, 0x125c) != 0x08
		|| !fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x036d) != 2
		|| fairy_snes_debug_wram(machine, 0x1c2b) != 0x04
		|| !fairy_snes_key_event(machine, 0x75, true, true) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x036d) != 1
		|| !fairy_snes_key_event(machine, 0x0d, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x036d) != 2
		|| !fairy_snes_key_event(machine, 0x74, true, true) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x036d) != 0) {
		std::fputs("filename keyboard navigation did not move/highlight the focus ring\n", stderr);
		return 91;
	}
	// An empty mouse Save remains in the dialog and visibly selects the clicked
	// button. Clicking the field then returns focus to typing.
	fairy_snes_mouse_event(machine, 72, -100, false, false); // center -> Save button (200,12)
	if (!runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x0334) != 200
		|| fairy_snes_debug_wram(machine, 0x0335) != 12) return 91;
	fairy_snes_mouse_event(machine, 0, 0, true, false);
	if (!runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 10
		|| fairy_snes_debug_wram(machine, 0x036d) != 1
		|| fairy_snes_debug_wram(machine, 0x1c20) != 0x04) {
		std::fputs("empty filename Save click did not focus the real button\n", stderr);
		return 91;
	}
	fairy_snes_mouse_event(machine, 0, 0, false, false);
	if (!runFrames(machine)) return 91;
	fairy_snes_mouse_event(machine, -72, 100, false, false); // Save -> name field (128,112)
	if (!runFrames(machine, 2)) return 91;
	fairy_snes_mouse_event(machine, 0, 0, true, false);
	if (!runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x036d) != 0
		|| fairy_snes_debug_wram(machine, 0x125c) != 0x04) {
		std::fputs("filename field click did not restore typing focus\n", stderr);
		return 91;
	}
	fairy_snes_mouse_event(machine, 0, 0, false, false);
	if (!runFrames(machine) || !typeAscii(machine, new_name)
		|| !wramEquals(machine, 0x1880, new_name)
		|| !wramEquals(machine, 0x107b, new_name)
		|| !fairy_snes_key_event(machine, 0x66, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x0331) != 2
		|| fairy_snes_debug_wram(machine, 0x107d) != '_'
		|| !typeAscii(machine, "w")
		|| !wramEquals(machine, 0x1880, new_name)) {
		std::fputs("cartridge filename field did not retain/render/edit typed text\n", stderr);
		return 91;
	}
	// Cancel must be an action, not a synthetic Backspace: clicking it with a
	// non-empty name exits immediately and preserves the selected parent.
	fairy_snes_mouse_event(machine, 72, -92, false, false); // field -> Cancel (200,20)
	if (!runFrames(machine, 2)) return 91;
	fairy_snes_mouse_event(machine, 0, 0, true, false);
	if (!runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 6
		|| fairy_snes_debug_wram(machine, 0x0348) != parent_id.size()
		|| !wramEquals(machine, 0x1900, parent_id)) {
		std::fputs("filename Cancel click did not dismiss a non-empty modal\n", stderr);
		return 91;
	}
	fairy_snes_mouse_event(machine, 0, 0, false, false);
	if (!runFrames(machine)
		|| !fairy_snes_key_event(machine, 0x31, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 10
		|| !typeAscii(machine, new_name)) return 91;
	const std::size_t new_file_offset =
		fairy_snes_debug_bus_read(machine, 0x700002)
		| (static_cast<std::size_t>(fairy_snes_debug_bus_read(machine, 0x700003)) << 8);
	fairy_snes_mouse_event(machine, 0, -8, false, false); // Cancel -> Save (200,12)
	if (!runFrames(machine, 2)) return 92;
	fairy_snes_mouse_event(machine, 0, 0, true, false);
	if (!runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0
		|| fairy_snes_debug_bus_read(machine, navigation_command + new_file_offset + 2) != 0x0a
		|| fairy_snes_debug_bus_read(machine, navigation_command + new_file_offset + 3) != 1
		|| fairy_snes_debug_bus_read(machine, navigation_command + new_file_offset + 4) != parent_id.size() + 1 + committed_name.size()) {
		std::fputs("filename Save click did not emit CommandSaveAsNew\n", stderr);
		return 92;
	}
	for (std::size_t i = 0; i < parent_id.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, navigation_command + new_file_offset + 20 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(parent_id[i])) return 93;
	}
	if (fairy_snes_debug_bus_read(machine, navigation_command + new_file_offset + 20 + parent_id.size()) != 0) return 94;
	for (std::size_t i = 0; i < committed_name.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, navigation_command + new_file_offset + 21 + parent_id.size() + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(committed_name[i])) return 95;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 96;
	// A physical key must produce a complete cartridge-to-host command record.
	// The consumer remains host-owned and advances only after host application.
	const std::uint32_t command = 0x700100;
	if (!fairy_snes_key_event(machine, 0x22, true, false) || !runFrames(machine, 2)) return 37;
	if (fairy_snes_debug_bus_read(machine, 0x700002) != 21 ||
		fairy_snes_debug_bus_read(machine, 0x700003) != 0 ||
		fairy_snes_debug_bus_read(machine, 0x700004) != 0 ||
		fairy_snes_debug_bus_read(machine, command + 0) != 1 ||
		fairy_snes_debug_bus_read(machine, command + 2) != 1 ||
		fairy_snes_debug_bus_read(machine, command + 4) != 1 ||
		fairy_snes_debug_bus_read(machine, command + 20) != 'x') {
		std::fprintf(stderr, "cartridge command was not committed: producer=%u/%u consumer=%u kind=%u count=%u payload=%02x\n",
			fairy_snes_debug_bus_read(machine, 0x700002), fairy_snes_debug_bus_read(machine, 0x700003),
			fairy_snes_debug_bus_read(machine, 0x700004), fairy_snes_debug_bus_read(machine, command + 2),
			fairy_snes_debug_bus_read(machine, command + 4), fairy_snes_debug_bus_read(machine, command + 20));
		return 37;
	}
	if (fairy_snes_debug_wram(machine, 0x0310) != 0) {
		std::fprintf(stderr, "viewport generation corrupted after command production: %u\n",
			fairy_snes_debug_wram(machine, 0x0310));
		return 56;
	}
	fairy_snes_debug_bus_write(machine, 0x700004, 21); // host commits the consumed command
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 1);  // authoritative revision
	fairy_snes_debug_bus_write(machine, ViewportSram + 8, 1);  // cursor
	fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);  // text offset
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, 1);  // UTF-8 byte count
	fairy_snes_debug_bus_write(machine, ViewportTextSram, 'x');
	fairy_snes_debug_bus_write(machine, 0x70000a, 1); // commit host viewport
	if (!runFrames(machine, 2) || fairy_snes_debug_wram(machine, 0x08) != 1 ||
		fairy_snes_debug_wram(machine, 0x00) != 1 || fairy_snes_debug_wram(machine, 0x0500) != 'x') return 41;
	if (fairy_snes_debug_wram(machine, 0x0310) != 1) {
		std::fprintf(stderr, "non-empty viewport generation was not retained: %u\n",
			fairy_snes_debug_wram(machine, 0x0310));
		return 57;
	}
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 2);
	fairy_snes_debug_bus_write(machine, ViewportSram + 8, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, 0);
	fairy_snes_debug_bus_write(machine, 0x70000a, 0); // committed empty viewport
	std::array<std::uint8_t, 4> emptySeen{};
	std::array<std::uint8_t, 4> emptyLength{};
	std::array<std::uint8_t, 4> emptyCursor{};
	bool emptyCleared = false;
	for (std::size_t frame = 0; frame < emptySeen.size(); ++frame) {
		if (!runFrames(machine, 1)) return 55;
		emptySeen[frame] = fairy_snes_debug_wram(machine, 0x0310);
		emptyLength[frame] = fairy_snes_debug_wram(machine, 0x08);
		emptyCursor[frame] = fairy_snes_debug_wram(machine, 0x00);
		emptyCleared = emptySeen[frame] == 0 && emptyLength[frame] == 0 && emptyCursor[frame] == 0;
		if (emptyCleared) break;
	}
	if (!emptyCleared) {
		std::fprintf(stderr,
			"empty viewport did not clear guest: active=%u count=%u frames=[%u/%u/%u %u/%u/%u %u/%u/%u %u/%u/%u]\n",
			fairy_snes_debug_bus_read(machine, 0x70000a), fairy_snes_debug_bus_read(machine, 0x704120),
			emptySeen[0], emptyLength[0], emptyCursor[0], emptySeen[1], emptyLength[1], emptyCursor[1],
			emptySeen[2], emptyLength[2], emptyCursor[2], emptySeen[3], emptyLength[3], emptyCursor[3]);
		return 55;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 38;
	const auto* before_pixels = fairy_snes_framebuffer(machine);
	std::vector<std::uint32_t> before(before_pixels, before_pixels + 256 * 224);

	// XBAND set-2 make code $1C is unshifted "a". This crosses host queue -> falling IOBIT
	// transaction -> $4017 DATA pair reads -> guest scancode table -> VBlank
	// VRAM write -> PPU output.
	if (!fairy_snes_key_event(machine, 0x1c, true, false)) return 5;
	for (int i = 0; i < 2; ++i) {
		if (!fairy_snes_run_frame(machine)) return 6;
	}
	if (fairy_snes_debug_wram(machine, 0x08) != 1 ||
		fairy_snes_debug_wram(machine, 0x00) != 1 ||
		fairy_snes_debug_wram(machine, 0x0500) != 'a') {
		std::fprintf(stderr, "lowercase: len=%u cursor=%u key=%02x shift=%u queue=%u/%u char=%02x\n",
			fairy_snes_debug_wram(machine, 0x08), fairy_snes_debug_wram(machine, 0x00),
			fairy_snes_debug_wram(machine, 0x01), fairy_snes_debug_wram(machine, 0x04), fairy_snes_debug_wram(machine, 0x05),
			fairy_snes_debug_wram(machine, 0x06), fairy_snes_debug_wram(machine, 0x0500));
		return 7;
	}
	if (!fairy_snes_key_event(machine, 0x1c, false, false)) return 22;
	for (int i = 0; i < 2; ++i) if (!fairy_snes_run_frame(machine)) return 23;
	if (fairy_snes_debug_wram(machine, 0x08) != 1 ||
		fairy_snes_debug_wram(machine, 0x00) != 1) return 24;
	if (!fairy_snes_key_event(machine, 0x32, true, false)) return 8; // B
	for (int i = 0; i < 2; ++i) if (!fairy_snes_run_frame(machine)) return 9;
	if (!fairy_snes_key_event(machine, 0x6b, true, true)) return 10; // Left
	for (int i = 0; i < 2; ++i) if (!fairy_snes_run_frame(machine)) return 11;
	if (!fairy_snes_key_event(machine, 0x21, true, false)) return 12; // C
	for (int i = 0; i < 2; ++i) if (!fairy_snes_run_frame(machine)) return 13;
	if (fairy_snes_debug_wram(machine, 0x08) != 3 ||
		fairy_snes_debug_wram(machine, 0x00) != 2 ||
		fairy_snes_debug_wram(machine, 0x0500) != 'a' ||
		fairy_snes_debug_wram(machine, 0x0501) != 'c' ||
		fairy_snes_debug_wram(machine, 0x0502) != 'b') {
		std::fprintf(stderr, "insert: len=%u cursor=%u prefix=%u release=%u queue=%u/%u text=%02x %02x %02x\n",
			fairy_snes_debug_wram(machine, 0x08), fairy_snes_debug_wram(machine, 0x00),
			fairy_snes_debug_wram(machine, 0x02), fairy_snes_debug_wram(machine, 0x03),
			fairy_snes_debug_wram(machine, 0x05), fairy_snes_debug_wram(machine, 0x06),
			fairy_snes_debug_wram(machine, 0x0500), fairy_snes_debug_wram(machine, 0x0501),
			fairy_snes_debug_wram(machine, 0x0502));
		return 14;
	}
	if (!fairy_snes_key_event(machine, 0x66, true, false)) return 15; // Backspace
	for (int i = 0; i < 2; ++i) if (!fairy_snes_run_frame(machine)) return 16;
	if (fairy_snes_debug_wram(machine, 0x08) != 2 ||
		fairy_snes_debug_wram(machine, 0x00) != 1 ||
		fairy_snes_debug_wram(machine, 0x0500) != 'a' ||
		fairy_snes_debug_wram(machine, 0x0501) != 'b') return 17;
	if (!fairy_snes_key_event(machine, 0x5a, true, false)) return 18; // Enter
	for (int i = 0; i < 2; ++i) if (!fairy_snes_run_frame(machine)) return 19;
	if (fairy_snes_debug_wram(machine, 0x08) != 3 ||
		fairy_snes_debug_wram(machine, 0x00) != 2 ||
		fairy_snes_debug_wram(machine, 0x0500) != 'a' ||
		fairy_snes_debug_wram(machine, 0x0501) != 0x0d ||
		fairy_snes_debug_wram(machine, 0x0502) != 'b') return 20;
	// Shift is guest-owned state: make changes subsequent letters, and the
	// F0 break sequence restores lowercase input.
	if (!fairy_snes_key_event(machine, 0x12, true, false) || !runFrames(machine)) return 25;
	if (!fairy_snes_key_event(machine, 0x32, true, false) || !runFrames(machine)) return 26;
	if (fairy_snes_debug_wram(machine, 0x0502) != 'B') return 27;
	if (!fairy_snes_key_event(machine, 0x12, false, false) || !runFrames(machine)) return 28;
	if (!fairy_snes_key_event(machine, 0x21, true, false) || !runFrames(machine)) return 29;
	if (fairy_snes_debug_wram(machine, 0x0503) != 'c') return 51;
	// Caps Lock is persistent guest-owned state, independent of the host UI.
	if (!fairy_snes_key_event(machine, 0x58, true, false) || !runFrames(machine)) return 47;
	if (!fairy_snes_key_event(machine, 0x1c, true, false) || !runFrames(machine) || fairy_snes_debug_wram(machine, 0x0504) != 'A') return 48;
	if (!fairy_snes_key_event(machine, 0x70, true, false) || !runFrames(machine) || fairy_snes_debug_wram(machine, 0x0505) != '!') return 52;
	const std::uint8_t symbol_scans[] = {
		0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x76,
		0x75, 0x74, 0x73, 0x72, 0x71, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x69
	};
	const char symbol_chars[] = "@#$%^&*()_+<>?\"{}:|~";
	for (std::size_t i = 0; i < sizeof(symbol_scans); ++i) {
		if (!fairy_snes_key_event(machine, symbol_scans[i], true, false) || !runFrames(machine, 4)) return 53;
		const std::uint8_t got = fairy_snes_debug_wram(machine, 0x0506 + i);
		const std::uint8_t want = static_cast<std::uint8_t>(symbol_chars[i]);
		if (got != want) {
			return 53;
		}
	}
	const auto* after = fairy_snes_framebuffer(machine);
	bool changed = false;
	for (std::size_t i = 0; i < before.size(); ++i) {
		if (before[i] != after[i]) { changed = true; break; }
	}
	if (argc == 3) {
		std::ofstream ppm(argv[2], std::ios::binary);
		ppm << "P6\n256 224\n255\n";
		for (std::size_t i = 0; i < 256 * 224; ++i) {
			const char rgb[] = {static_cast<char>(after[i] >> 16),
				static_cast<char>(after[i] >> 8), static_cast<char>(after[i])};
			ppm.write(rgb, sizeof(rgb));
		}
	}
	if (!changed) {
		std::fputs("XBAND make code did not reach the PPU framebuffer\n", stderr);
		return 21;
	}
	fairy_snes_destroy(machine);

	// Layout is cartridge behavior, not host preformatting. A word that would
	// cross column 30 moves intact to the next visual row, while the document
	// buffer preserves its ordinary single spaces.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 31;
	constexpr std::string_view phrase = "one two three four five six seven";
	if (!typeAscii(machine, phrase)) return 32;
	// The per-character render loop redraws the whole document every frame,
	// so a 34-character phrase needs more than a couple of frames to fully
	// settle after the last keystroke; 40 was already close to the minimum
	// needed even before the word-wrap overflow guard added a per-character
	// comparison, so give it a comfortable margin rather than the tightest
	// value that happens to pass today.
	if (!runFrames(machine, 80)) return 32;
	for (std::size_t i = 0; i < phrase.size(); ++i) {
		const std::uint8_t got = fairy_snes_debug_wram(machine, 0x0500 + i);
		const std::uint8_t want = static_cast<std::uint8_t>(phrase[i]);
		if (got != want) {
			std::fprintf(stderr, "phrase mismatch at %zu got=%02x('%c') want=%02x('%c') len=%u cursor=%u\\n",
				i, got, got, want, want,
				fairy_snes_debug_wram(machine, 0x0008), fairy_snes_debug_wram(machine, 0x0000));
			return 33;
		}
	}
	constexpr std::string_view firstLine = "one two three four five six";
	for (std::size_t i = 0; i < firstLine.size(); ++i) {
		if (fairy_snes_debug_wram(machine, 0x1000 + i) != static_cast<std::uint8_t>(firstLine[i])) {
			std::fprintf(stderr, "wrap mismatch at %zu: got=%02x want=%02x\n", i,
				fairy_snes_debug_wram(machine, 0x1000 + i), static_cast<unsigned char>(firstLine[i]));
			for (int j = 0; j < 60; ++j) std::fprintf(stderr, "%02x%c", fairy_snes_debug_wram(machine, 0x1000 + j), j == 59 ? '\n' : ' ');
			return 34;
		}
	}
	const bool wrapsWholeWord =
		fairy_snes_debug_wram(machine, 0x1000 + 28) == ' ' &&
		fairy_snes_debug_wram(machine, 0x1000 + 29) == ' ';
	if (wrapsWholeWord) {
		for (std::size_t i = firstLine.size(); i < 30; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + i) != ' ') {
				std::fprintf(stderr, "whole-word wrap left non-space at first-row cell %zu: got=%02x\n",
					i, fairy_snes_debug_wram(machine, 0x1000 + i));
				return 35;
			}
		}
		constexpr std::string_view secondLine = "seven";
		for (std::size_t i = 0; i < secondLine.size(); ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + 30 + i) != static_cast<std::uint8_t>(secondLine[i])) return 36;
		}
	} else {
		constexpr std::string_view hardWrapFirstRemainder = " se";
		for (std::size_t i = 0; i < hardWrapFirstRemainder.size(); ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + 27 + i) != static_cast<std::uint8_t>(hardWrapFirstRemainder[i])) {
				std::fprintf(stderr,
					"hard-wrap first-row remainder mismatch at cell %zu: got=%02x want=%02x\n",
					27 + i, fairy_snes_debug_wram(machine, 0x1000 + 27 + i),
					static_cast<std::uint8_t>(hardWrapFirstRemainder[i]));
				for (int cell = 24; cell < 40; ++cell) {
					std::fprintf(stderr, "%02x%c", fairy_snes_debug_wram(machine, 0x1000 + cell),
						cell == 39 ? '\n' : ' ');
				}
				std::fprintf(stderr, "draw state: col=%u caretCell=%u wordLen=%u wordStart=%u len=%u cursor=%u found=%u\n",
					fairy_snes_debug_wram(machine, 0x000a), fairy_snes_debug_wram(machine, 0x000b),
					fairy_snes_debug_wram(machine, 0x000c), fairy_snes_debug_wram(machine, 0x0058),
					fairy_snes_debug_wram(machine, 0x0008), fairy_snes_debug_wram(machine, 0x0000),
					fairy_snes_debug_wram(machine, 0x0056));
				return 35;
			}
		}
		constexpr std::string_view secondLine = "ven";
		for (std::size_t i = 0; i < secondLine.size(); ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + 30 + i) != static_cast<std::uint8_t>(secondLine[i])) return 36;
		}
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 37;
	constexpr std::string_view punctuation = "a-b,c./':";
	if (!typeAscii(machine, punctuation)) return 38;
	for (std::size_t i = 0; i < punctuation.size(); ++i) {
		if (fairy_snes_debug_wram(machine, 0x0500 + i) != static_cast<std::uint8_t>(punctuation[i])) return 39;
	}
	fairy_snes_destroy(machine);
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3) || !typeAscii(machine, "abc")) return 40;
	if (fairy_snes_debug_wram(machine, 0x1000) != 'a'
		|| fairy_snes_debug_wram(machine, 0x1001) != 'b'
		|| fairy_snes_debug_wram(machine, 0x1002) != 'c'
		|| fairy_snes_debug_wram(machine, 0x1003) != 0x7f) {
		std::fprintf(stderr, "caret baseline did not render after typing abc: cells=%02x %02x %02x %02x\\n",
			fairy_snes_debug_wram(machine, 0x1000), fairy_snes_debug_wram(machine, 0x1001),
			fairy_snes_debug_wram(machine, 0x1002), fairy_snes_debug_wram(machine, 0x1003));
		return 163;
	}
	if (!fairy_snes_key_event(machine, 0x6b, true, true) || !runFrames(machine)) return 41;
	if (!fairy_snes_key_event(machine, 0x71, true, true) || !runFrames(machine)) return 42;
	if (fairy_snes_debug_wram(machine, 0x08) != 2 || fairy_snes_debug_wram(machine, 0x0500) != 'a' || fairy_snes_debug_wram(machine, 0x0501) != 'b'
		|| fairy_snes_debug_wram(machine, 0x1000) != 'a' || fairy_snes_debug_wram(machine, 0x1001) != 'b'
		|| fairy_snes_debug_wram(machine, 0x1002) != 0x7f) {
		std::fprintf(stderr, "caret glyph did not stabilize at post-delete cursor: len=%u cells=%02x %02x %02x\\n",
			fairy_snes_debug_wram(machine, 0x08), fairy_snes_debug_wram(machine, 0x1000),
			fairy_snes_debug_wram(machine, 0x1001), fairy_snes_debug_wram(machine, 0x1002));
		return 164;
	}
	if (!fairy_snes_key_event(machine, 0x6c, true, true) || !runFrames(machine) || !typeAscii(machine, "z")) return 44;
	if (!fairy_snes_key_event(machine, 0x69, true, true) || !runFrames(machine) || !typeAscii(machine, "q")) return 45;
	if (fairy_snes_debug_wram(machine, 0x1000) != 'z'
		|| fairy_snes_debug_wram(machine, 0x1001) != 'a'
		|| fairy_snes_debug_wram(machine, 0x1002) != 'b'
		|| fairy_snes_debug_wram(machine, 0x1003) != 'q'
		|| fairy_snes_debug_wram(machine, 0x1004) != 0x7f) {
		std::fprintf(stderr, "caret glyph did not advance after home/end edits: cells=%02x %02x %02x %02x %02x\\n",
			fairy_snes_debug_wram(machine, 0x1000), fairy_snes_debug_wram(machine, 0x1001),
			fairy_snes_debug_wram(machine, 0x1002), fairy_snes_debug_wram(machine, 0x1003),
			fairy_snes_debug_wram(machine, 0x1004));
		return 165;
	}
	if (fairy_snes_debug_wram(machine, 0x08) != 4 || fairy_snes_debug_wram(machine, 0x0500) != 'z' || fairy_snes_debug_wram(machine, 0x0501) != 'a' || fairy_snes_debug_wram(machine, 0x0502) != 'b' || fairy_snes_debug_wram(machine, 0x0503) != 'q') return 46;
	fairy_snes_destroy(machine);

	// The resident SNES mouse pointer is a real OBJ sprite driven from the guest
	// pointer coordinates at $0334/$0335. It must render in front of the opaque
	// document field and follow port-1 motion, even while the document is idle
	// and no viewport render is scheduled. Anything less is not a usable pointer.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 4)) return 101;
	const auto pointerBox = [&](int ox, int oy, std::uint32_t field) {
		int n = 0;
		const auto* fb = fairy_snes_framebuffer(machine);
		for (int y = 0; y < 8 && oy + y < 224; ++y)
			for (int x = 0; x < 8 && ox + x < 256; ++x)
				if ((fb[(oy + y) * 256 + (ox + x)] & 0xffffffu) != field) ++n;
		return n;
	};
	const std::uint32_t field = fairy_snes_framebuffer(machine)[150 * 256 + 20] & 0xffffffu;
	const int px0 = fairy_snes_debug_wram(machine, 0x0334);
	const int py0 = fairy_snes_debug_wram(machine, 0x0335);
	const int at_boot = pointerBox(px0, py0, field);
	if (px0 != 128 || py0 != 112 || at_boot < 20) {
		std::fprintf(stderr, "pointer sprite absent at boot: pos=(%d,%d) arrow_px=%d\n", px0, py0, at_boot);
		return 102;
	}
	fairy_snes_mouse_event(machine, -40, -40, false, false);
	if (!runFrames(machine, 4)) return 103;
	const int px1 = fairy_snes_debug_wram(machine, 0x0334);
	const int py1 = fairy_snes_debug_wram(machine, 0x0335);
	const int at_new = pointerBox(px1, py1, field);
	const int at_old = pointerBox(px0, py0, field);
	if (px1 == px0 || py1 == py0 || at_new < 20) {
		std::fprintf(stderr, "pointer sprite did not track motion: pos=(%d,%d) new_px=%d old_px=%d\n",
			px1, py1, at_new, at_old);
		return 104;
	}
	fairy_snes_destroy(machine);

	// A pointer click inside the document body resolves to a real caret command.
	// The cartridge owns pixel->cell hit-testing and publishes the clicked cell's
	// viewport-relative UTF-16 offset as CommandPointerSetCursor (0x0104); the
	// host owns the absolute position. The document grid is 30x8 cells from pixel
	// (8,80). "hello world" places 'w' at cell 6, pixel column (56..63).
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 105;
	constexpr std::string_view click_text = "hello world";
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 1);  // revision
	fairy_snes_debug_bus_write(machine, ViewportSram + 8, 0);  // cursor
	fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0); // text offset
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(click_text.size()));
	for (std::size_t i = 0; i < click_text.size(); ++i)
		fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i),
			static_cast<std::uint8_t>(click_text[i]));
	fairy_snes_debug_bus_write(machine, 0x70000a, 1); // commit viewport
	if (!runFrames(machine, 3)) return 106;
	fairy_snes_mouse_event(machine, -70, -30, false, false); // move to (58,82) = col 6, row 0
	if (!runFrames(machine, 4)) return 107;
	if (fairy_snes_debug_wram(machine, 0x0334) != 58 || fairy_snes_debug_wram(machine, 0x0335) != 82) {
		std::fprintf(stderr, "pointer did not reach the click target: (%u,%u)\n",
			fairy_snes_debug_wram(machine, 0x0334), fairy_snes_debug_wram(machine, 0x0335));
		return 108;
	}
	fairy_snes_mouse_event(machine, 0, 0, true, false); // left button rising edge
	if (!runFrames(machine, 20)) return 109;
	const std::uint32_t click_cmd = 0x700100;
	const int click_producer = fairy_snes_debug_bus_read(machine, 0x700002)
		| (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
	if (click_producer != 22
		|| fairy_snes_debug_bus_read(machine, click_cmd + 2) != 0x04
		|| fairy_snes_debug_bus_read(machine, click_cmd + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, click_cmd + 4) != 2
		|| fairy_snes_debug_bus_read(machine, click_cmd + 21) != 0
		|| (fairy_snes_debug_bus_read(machine, click_cmd + 20) != 0
			&& fairy_snes_debug_bus_read(machine, click_cmd + 20) != 6)) {
		std::fprintf(stderr,
			"document click did not emit CommandPointerSetCursor: producer=%d kind=%02x%02x count=%u payload=%u,%u\n",
			click_producer, fairy_snes_debug_bus_read(machine, click_cmd + 3),
			fairy_snes_debug_bus_read(machine, click_cmd + 2), fairy_snes_debug_bus_read(machine, click_cmd + 4),
			fairy_snes_debug_bus_read(machine, click_cmd + 20), fairy_snes_debug_bus_read(machine, click_cmd + 21));
		return 110;
	}
	// A click outside the document grid resolves to nothing: no command is
	// produced, so the ring producer index is unchanged.
	fairy_snes_mouse_event(machine, 0, 0, false, false); // release
	if (!runFrames(machine, 1)) return 111;
	fairy_snes_mouse_event(machine, 0, -42, false, false); // move up to y=40, above the grid
	if (!runFrames(machine, 4)) return 112;
	fairy_snes_mouse_event(machine, 0, 0, true, false); // click outside
	if (!runFrames(machine, 20)) return 113;
	const int after_outside = fairy_snes_debug_bus_read(machine, 0x700002)
		| (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
	if (after_outside != 22) {
		std::fprintf(stderr, "click outside the document grid emitted a command: producer=%d\n", after_outside);
		return 114;
	}
	fairy_snes_destroy(machine);

	// A drag is a press then a held-button move. The press emits
	// CommandPointerSetCursor (the anchor); moving to a new cell while held emits
	// CommandPointerExtendCursor (0x010f); a still-held button over the same cell
	// emits nothing. "hello world": press at cell 2 ('l'), drag to cell 6 ('w').
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 115;
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 1);
	fairy_snes_debug_bus_write(machine, ViewportSram + 8, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(click_text.size()));
	for (std::size_t i = 0; i < click_text.size(); ++i)
		fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i),
			static_cast<std::uint8_t>(click_text[i]));
	fairy_snes_debug_bus_write(machine, 0x70000a, 1);
	if (!runFrames(machine, 3)) return 116;
	fairy_snes_mouse_event(machine, -102, -30, false, false); // move to (26,82) = cell 2
	if (!runFrames(machine, 2)) return 117;
	fairy_snes_mouse_event(machine, 0, 0, true, false); // press
	if (!runFrames(machine, 20)) return 118;
	const std::uint32_t drag_cmd = 0x700100;
	if ((fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8)) != 22
		|| fairy_snes_debug_bus_read(machine, drag_cmd + 2) != 0x04
		|| fairy_snes_debug_bus_read(machine, drag_cmd + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, drag_cmd + 4) != 2) {
		std::fputs("drag press did not emit CommandPointerSetCursor at the anchor cell\n", stderr);
		return 119;
	}
	fairy_snes_mouse_event(machine, 32, 0, true, false); // drag to (58,82) = cell 6, still held
	if (!runFrames(machine, 20)) return 120;
	const std::uint32_t ext = drag_cmd + 22; // second 22-byte record
	if ((fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8)) != 44
		|| fairy_snes_debug_bus_read(machine, ext + 2) != 0x0f
		|| fairy_snes_debug_bus_read(machine, ext + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, ext + 4) != 2) {
		std::fprintf(stderr, "drag move did not emit CommandPointerExtendCursor: producer=%d kind=%02x%02x off=%u\n",
			fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8),
			fairy_snes_debug_bus_read(machine, ext + 3), fairy_snes_debug_bus_read(machine, ext + 2),
			fairy_snes_debug_bus_read(machine, ext + 20));
		return 121;
	}
	fairy_snes_mouse_event(machine, 0, 0, true, false); // still held, no move -> no command
	if (!runFrames(machine, 2)) return 122;
	if ((fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8)) != 44) {
		std::fputs("a still-held drag over the same cell emitted a redundant command\n", stderr);
		return 123;
	}
	fairy_snes_destroy(machine);

	// Browser pagination: a directory with more than seven entries must page.
	// Open lists roots; the host reports has-more; pressing Down past the last
	// row re-issues the list command carrying the next page in flags-high, and
	// Up at the top of a later page re-requests the previous page.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 124;
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)) return 125; // F1
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 126;  // Down -> Open
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)) return 127; // Enter
	if (fairy_snes_debug_wram(machine, 0x031d) != 2) { std::fputs("Open did not enter roots loading\n", stderr); return 128; }
	const auto injectPage = [&](std::size_t base, int entries, std::uint32_t total, std::uint32_t begin, std::uint8_t hasMore) {
		std::vector<std::uint8_t> ev;
		for (int i = 0; i < entries; ++i) {
			const std::string id = ("rootid" + std::to_string(int(begin) + i) + "-padpadpadpadpadpad").substr(0, 24);
			const std::string nm = "DIR" + std::to_string(int(begin) + i);
			std::vector<std::uint8_t> pl;
			pl.push_back(static_cast<std::uint8_t>(id.size()));
			append16(pl, static_cast<std::uint16_t>(nm.size()));
			pl.push_back(3);
			append64(pl, 0); append64(pl, 0);
			pl.insert(pl.end(), id.begin(), id.end());
			pl.insert(pl.end(), nm.begin(), nm.end());
			appendRecord(ev, 0x8200, pl);
		}
		std::vector<std::uint8_t> comp; append32(comp, total); append32(comp, begin);
		comp.push_back(static_cast<std::uint8_t>(entries)); comp.push_back(2); comp.push_back(hasMore);
		appendRecord(ev, 0x820f, comp);
		for (std::size_t i = 0; i < ev.size(); ++i) fairy_snes_debug_bus_write(machine, 0x702100 + static_cast<std::uint32_t>(base + i), ev[i]);
		const std::size_t producer = base + ev.size();
		fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(producer));
		fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(producer >> 8));
		return producer;
	};
	std::size_t evbase = injectPage(0, 7, 8, 0, 1); // page 0: 7 of 8, more
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x031f) != 7 || fairy_snes_debug_wram(machine, 0x0349) != 1) {
		std::fputs("root page 0 did not become a ready browser with has-more set\n", stderr);
		return 129;
	}
	for (int i = 0; i < 7; ++i) { if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 130; } // Down past end
	int producer = fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
	int base = 0x700100 + (producer - 20);
	if (fairy_snes_debug_wram(machine, 0x034a) != 1 || fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_bus_read(machine, base + 2) != 0x07 || fairy_snes_debug_bus_read(machine, base + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, base + 7) != 1) {
		std::fprintf(stderr, "page-down did not re-request roots at page 1: page=%u mode=%u kind=%02x%02x flagshi=%u\n",
			fairy_snes_debug_wram(machine, 0x034a), fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_bus_read(machine, base + 3), fairy_snes_debug_bus_read(machine, base + 2),
			fairy_snes_debug_bus_read(machine, base + 7));
		return 131;
	}
	evbase = injectPage(evbase, 1, 8, 7, 0); // page 1: last entry, no more
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x031f) != 1 || fairy_snes_debug_wram(machine, 0x0349) != 0) {
		std::fputs("root page 1 did not become a ready browser\n", stderr);
		return 132;
	}
	if (!fairy_snes_key_event(machine, 0x75, true, true) || !runFrames(machine, 2)) return 133; // Up at top -> prev page
	producer = fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
	base = 0x700100 + (producer - 20);
	if (fairy_snes_debug_wram(machine, 0x034a) != 0 || fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_bus_read(machine, base + 2) != 0x07 || fairy_snes_debug_bus_read(machine, base + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, base + 7) != 0) {
		std::fprintf(stderr, "page-up did not re-request roots at page 0: page=%u mode=%u flagshi=%u\n",
			fairy_snes_debug_wram(machine, 0x034a), fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_bus_read(machine, base + 7));
		return 134;
	}
	// Directory-back: entering a directory pushes the previous listing; Back
	// pops it and re-requests the parent (the root source here) instead of
	// closing the browser. Back at the root listing still closes it.
	evbase = injectPage(evbase, 1, 1, 0, 0); // respond to the page-up request: one HOME dir
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x034d) != 0) return 135;
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)) return 136; // Enter the directory
	producer = fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
	base = 0x700100 + (producer - 20 - 24); // listFiles record: 20-byte header + 24-byte opaque id
	if (fairy_snes_debug_wram(machine, 0x034d) != 1 || fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_bus_read(machine, base + 2) != 0x00 || fairy_snes_debug_bus_read(machine, base + 3) != 0x01) {
		std::fprintf(stderr, "directory entry did not push the back stack: depth=%u mode=%u\n",
			fairy_snes_debug_wram(machine, 0x034d), fairy_snes_debug_wram(machine, 0x031d));
		return 137;
	}
	evbase = injectPage(evbase, 1, 1, 0, 0); // the directory's own single-entry page
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5) return 138;
	if (!fairy_snes_key_event(machine, 0x66, true, false) || !runFrames(machine, 2)) return 139; // Back
	producer = fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
	base = 0x700100 + (producer - 20); // roots re-request: empty payload
	if (fairy_snes_debug_wram(machine, 0x034d) != 0 || fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_bus_read(machine, base + 2) != 0x07 || fairy_snes_debug_bus_read(machine, base + 3) != 0x01) {
		std::fprintf(stderr, "Back did not pop to the parent listing: depth=%u mode=%u kind=%02x%02x\n",
			fairy_snes_debug_wram(machine, 0x034d), fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_bus_read(machine, base + 3), fairy_snes_debug_bus_read(machine, base + 2));
		return 140;
	}
	evbase = injectPage(evbase, 1, 1, 0, 0); // the restored root listing
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5) return 141;
	if (!fairy_snes_key_event(machine, 0x66, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0) {
		std::fputs("Back at the root listing did not close the browser\n", stderr);
		return 142;
	}
	fairy_snes_destroy(machine);

	// Selecting a row below the first one. Every real Open browse ends on a row
	// the host did not list first: directories sort ahead of documents, so the
	// file the user wants is almost never row 0. The per-entry flags, opaque-ID
	// length and name-length tables are one byte per row, and the selection must
	// index them that way while indexing the 32-byte-per-row ID table by stride.
	// Getting that wrong reads another row's bytes: Enter then emits an empty or
	// truncated ID, the host cannot resolve it, and the browser silently returns
	// to the unchanged document.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 190;
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)) return 191; // F1
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 192;  // Down -> Open
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)) return 193; // Enter
	if (fairy_snes_debug_wram(machine, 0x031d) != 2) return 194;
	struct RowSpec final { std::string id; std::string name; bool directory; };
	const auto injectRows = [&](std::size_t base, const std::vector<RowSpec>& rows,
		std::uint8_t source) {
		std::vector<std::uint8_t> ev;
		for (const RowSpec& row : rows) {
			std::vector<std::uint8_t> pl;
			pl.push_back(static_cast<std::uint8_t>(row.id.size()));
			append16(pl, static_cast<std::uint16_t>(row.name.size()));
			pl.push_back(static_cast<std::uint8_t>(row.directory ? 3 : 2));
			append64(pl, 0);
			append64(pl, 0);
			pl.insert(pl.end(), row.id.begin(), row.id.end());
			pl.insert(pl.end(), row.name.begin(), row.name.end());
			appendRecord(ev, 0x8200, pl);
		}
		std::vector<std::uint8_t> comp;
		append32(comp, static_cast<std::uint32_t>(rows.size()));
		append32(comp, 0);
		comp.push_back(static_cast<std::uint8_t>(rows.size()));
		comp.push_back(source);
		comp.push_back(0);
		appendRecord(ev, 0x820f, comp);
		for (std::size_t i = 0; i < ev.size(); ++i) {
			fairy_snes_debug_bus_write(machine,
				0x702100 + static_cast<std::uint32_t>(base + i), ev[i]);
		}
		const std::size_t producer = base + ev.size();
		fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(producer));
		fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(producer >> 8));
		return producer;
	};
	// The last emitted command record, located from the producer index.
	const auto lastCommand = [&](std::size_t payload_bytes) {
		const std::size_t producer = fairy_snes_debug_bus_read(machine, 0x700002)
			| (static_cast<std::size_t>(fairy_snes_debug_bus_read(machine, 0x700003)) << 8);
		return 0x700100u + static_cast<std::uint32_t>(producer - 20 - payload_bytes);
	};
	const std::string first_root = "aaaaaaaaaaaaaaaaaaaaaaaa";
	const std::string second_root = "bbbbbbbbbbbbbbbbbbbbbbbb";
	std::size_t rowbase = injectRows(0, {
		{first_root, "DOCUMENTS", true},
		{second_root, "STORIES", true},
		{"cccccccccccccccccccccccc", "NOTES.ODT", false}
	}, 2);
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x031f) != 3) {
		std::fputs("multi-row root page did not become a ready browser\n", stderr);
		return 195;
	}
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x0320) != 1) {
		std::fputs("Down did not move the browser selection to the second row\n", stderr);
		return 196;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)) return 197;
	std::uint32_t record = lastCommand(second_root.size());
	if (fairy_snes_debug_wram(machine, 0x031d) != 2
		|| fairy_snes_debug_bus_read(machine, record + 2) != 0x00
		|| fairy_snes_debug_bus_read(machine, record + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, record + 4) != second_root.size()
		|| wramString(machine, 0x1800, second_root.size()) != second_root) {
		std::fprintf(stderr,
			"the second directory row did not list its own opaque ID: mode=%u kind=%02x%02x count=%u staged=%s\n",
			fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_bus_read(machine, record + 3),
			fairy_snes_debug_bus_read(machine, record + 2),
			fairy_snes_debug_bus_read(machine, record + 4),
			wramString(machine, 0x1800, second_root.size()).c_str());
		return 198;
	}
	for (std::size_t i = 0; i < second_root.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, record + 20 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(second_root[i])) {
			std::fputs("the second directory row's list command carried another row's ID\n", stderr);
			return 199;
		}
	}
	// Inside that folder, the document sits below a subfolder and another file:
	// the exact third-row case a user reaches after saving into a home folder.
	const std::string wanted = "0123456789abcdef01234567";
	rowbase = injectRows(rowbase, {
		{"dddddddddddddddddddddddd", "DRAFTS", true},
		{"eeeeeeeeeeeeeeeeeeeeeeee", "LETTER.ODT", false},
		{wanted, "MARIA.ODT", false}
	}, 1);
	if (!runFrames(machine, 4) || fairy_snes_debug_wram(machine, 0x031d) != 5
		|| fairy_snes_debug_wram(machine, 0x031f) != 3) {
		std::fputs("multi-row directory page did not become a ready browser\n", stderr);
		return 200;
	}
	for (int i = 0; i < 2; ++i) {
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 201;
	}
	if (fairy_snes_debug_wram(machine, 0x0320) != 2) {
		std::fputs("Down did not move the browser selection to the third row\n", stderr);
		return 202;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)) return 203;
	record = lastCommand(wanted.size());
	if (fairy_snes_debug_wram(machine, 0x031d) != 0
		|| fairy_snes_debug_bus_read(machine, record + 2) != 0x01
		|| fairy_snes_debug_bus_read(machine, record + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, record + 4) != wanted.size()) {
		std::fprintf(stderr,
			"Enter on the third row did not emit CommandOpenFile: mode=%u kind=%02x%02x count=%u\n",
			fairy_snes_debug_wram(machine, 0x031d),
			fairy_snes_debug_bus_read(machine, record + 3),
			fairy_snes_debug_bus_read(machine, record + 2),
			fairy_snes_debug_bus_read(machine, record + 4));
		return 204;
	}
	for (std::size_t i = 0; i < wanted.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, record + 20 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(wanted[i])) {
			std::fprintf(stderr,
				"open-file from the third row carried the wrong opaque ID: staged=%s\n",
				wramString(machine, 0x1800, wanted.size()).c_str());
			return 205;
		}
	}
	fairy_snes_destroy(machine);

	// Recovery and save-progress are cartridge-owned screens. A host
	// EventRecoveryAvailable renders a restore dialog whose Enter emits
	// CommandRecover with the opaque "current" token; EventRecoveryRestored
	// confirms; menu Save shows a transient SAVING screen cleared by the next
	// committed viewport; and EventSaveConflict (whose dialog routing was
	// previously dead) renders its failure dialog.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 143;
	const auto pushEvent = [&](std::size_t at, std::uint16_t kind, const std::vector<std::uint8_t>& payload) {
		std::vector<std::uint8_t> wire;
		appendRecord(wire, kind, payload);
		for (std::size_t i = 0; i < wire.size(); ++i)
			fairy_snes_debug_bus_write(machine, 0x702100 + static_cast<std::uint32_t>(at + i), wire[i]);
		const std::size_t producer = at + wire.size();
		fairy_snes_debug_bus_write(machine, 0x700006, static_cast<std::uint8_t>(producer));
		fairy_snes_debug_bus_write(machine, 0x700007, static_cast<std::uint8_t>(producer >> 8));
		return producer;
	};
	constexpr std::string_view recovery_token = "current";
	std::vector<std::uint8_t> available{7, 3};
	available.insert(available.end(), recovery_token.cbegin(), recovery_token.cend());
	available.push_back('o'); available.push_back('d'); available.push_back('t');
	std::size_t recovery_base = pushEvent(0, 0x8204, available);
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 11
		|| fairy_snes_debug_wram(machine, 0x0337) != 4
		|| !wramEquals(machine, 0x1096, "RECOVERY AVAILABLE")
		|| !wramEquals(machine, 0x10b4, "ENTER RESTORES")
		|| !wramEquals(machine, 0x10d2, "BACK IGNORES")) {
		std::fputs("recovery availability did not render its cartridge dialog\n", stderr);
		return 144;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0
		|| fairy_snes_debug_bus_read(machine, 0x700100 + 2) != 0x09
		|| fairy_snes_debug_bus_read(machine, 0x700100 + 3) != 0x01
		|| fairy_snes_debug_bus_read(machine, 0x700100 + 4) != recovery_token.size()) {
		std::fputs("recovery dialog Enter did not emit CommandRecover\n", stderr);
		return 145;
	}
	for (std::size_t i = 0; i < recovery_token.size(); ++i) {
		if (fairy_snes_debug_bus_read(machine, 0x700100 + 20 + static_cast<std::uint32_t>(i))
			!= static_cast<std::uint8_t>(recovery_token[i])) return 146;
	}
	recovery_base = pushEvent(recovery_base, 0x8205, {});
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 11
		|| fairy_snes_debug_wram(machine, 0x0337) != 5
		|| !wramEquals(machine, 0x1096, "RECOVERY COMPLETE")
		|| !wramEquals(machine, 0x10b4, "DOCUMENT RESTORED")) {
		std::fputs("recovery restoration did not render its cartridge dialog\n", stderr);
		return 147;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0) return 148;
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)) return 149; // F1
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 150;  // Down
	if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 151;  // Down -> Save
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0x0c
		|| !wramEquals(machine, 0x1096, "SAVING DOCUMENT")
		|| !wramEquals(machine, 0x10b4, "PLEASE WAIT")) {
		std::fprintf(stderr, "menu Save did not enter the SAVING screen: mode=%u\n",
			fairy_snes_debug_wram(machine, 0x031d));
		return 152;
	}
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 1);
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, 0);
	fairy_snes_debug_bus_write(machine, 0x70000a, 1); // committed viewport acknowledges the save
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 0) {
		std::fputs("committed viewport did not clear the SAVING screen\n", stderr);
		return 153;
	}
	recovery_base = pushEvent(recovery_base, 0x8207, {});
	if (!runFrames(machine, 3) || fairy_snes_debug_wram(machine, 0x031d) != 11
		|| fairy_snes_debug_wram(machine, 0x0337) != 7
		|| !wramEquals(machine, 0x1096, "OPERATION FAILED")
		|| !wramEquals(machine, 0x10b4, "SAVE CONFLICT")) {
		std::fputs("save conflict did not render its cartridge dialog\n", stderr);
		return 154;
	}
	fairy_snes_destroy(machine);

	// Viewport status flags reach the visible header: bit 0 renders the
	// modified marker and bit 1 the read-only marker on the title card's
	// fourth row; a clean writable viewport renders neither.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 155;
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 1);
	fairy_snes_debug_bus_write(machine, ViewportSram + 32, 1);
	fairy_snes_debug_bus_write(machine, ViewportTextSram, 'x');
	fairy_snes_debug_bus_write(machine, ViewportSram + 91, 3); // dirty + read-only
	fairy_snes_debug_bus_write(machine, 0x70000a, 1);
	if (!runFrames(machine, 3)
		|| fairy_snes_debug_wram(machine, 0x1436) != '*'
		|| fairy_snes_debug_wram(machine, 0x1438) != 'R') {
		std::fprintf(stderr, "status flags did not render title markers: %02x %02x\n",
			fairy_snes_debug_wram(machine, 0x1436), fairy_snes_debug_wram(machine, 0x1438));
		return 156;
	}
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 2);
	fairy_snes_debug_bus_write(machine, ViewportSram + 91, 0); // clean and writable
	fairy_snes_debug_bus_write(machine, 0x70000a, 0);
	if (!runFrames(machine, 3)
		|| fairy_snes_debug_wram(machine, 0x1436) != ' '
		|| fairy_snes_debug_wram(machine, 0x1438) != ' ') {
		std::fputs("clean viewport did not clear the title status markers\n", stderr);
		return 157;
	}
	// The Statistics menu entry renders words, characters, and lines from the
	// same committed viewport, then Enter returns to the document.
	fairy_snes_debug_bus_write(machine, ViewportSram + 0, 3);
	fairy_snes_debug_bus_write(machine, ViewportSram + 24, 0x37); // characters = 567
	fairy_snes_debug_bus_write(machine, ViewportSram + 25, 0x02);
	fairy_snes_debug_bus_write(machine, ViewportSram + 28, 0xd2); // words = 1234
	fairy_snes_debug_bus_write(machine, ViewportSram + 29, 0x04);
	fairy_snes_debug_bus_write(machine, ViewportSram + 92, 89); // lines = 89
	fairy_snes_debug_bus_write(machine, 0x70000a, 1);
	if (!runFrames(machine, 3)) return 158;
	if (!fairy_snes_key_event(machine, 0x05, true, false) || !runFrames(machine, 2)) return 159; // F1
	for (int i = 0; i < 6; ++i) {
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 2)) return 160; // Down to Statistics
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0x0d
		|| !wramEquals(machine, 0x1078, "STATISTICS")
		|| !wramEquals(machine, 0x1096, "WORDS")
		|| !wramEquals(machine, 0x109d, "1234")
		|| !wramEquals(machine, 0x10b4, "CHARS")
		|| !wramEquals(machine, 0x10bb, "0567")
		|| !wramEquals(machine, 0x10d2, "LINES")
		|| !wramEquals(machine, 0x10d9, "0089")) {
		std::fprintf(stderr, "statistics dialog did not render viewport metadata: mode=%u\n",
			fairy_snes_debug_wram(machine, 0x031d));
		return 161;
	}
	if (!fairy_snes_key_event(machine, 0x5a, true, false) || !runFrames(machine, 2)
		|| fairy_snes_debug_wram(machine, 0x031d) != 0) {
		std::fputs("statistics dialog did not dismiss to the document\n", stderr);
		return 162;
	}
	fairy_snes_destroy(machine);

	// Selection highlighting: a committed viewport with a non-empty selection
	// range must render selected cells with the spelling-issue visual style
	// (attribute 0x0c), taking priority over a grammar flag on the same cell,
	// while leaving unselected/unflagged cells at the plain document attribute
	// (0x08) and unselected grammar-flagged cells at the grammar attribute
	// (0x04). Selection bounds are cartridge-owned WRAM ($50 start-inclusive,
	// $52 end-exclusive) distinct from the draw loop's own measurement
	// scratch and title-render's byte-count scratch.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 163;
	{
		constexpr std::string_view selection_text = "abcdefgh";
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, 0);  // cursor
		fairy_snes_debug_bus_write(machine, ViewportSram + 12, 2); // selection start units
		fairy_snes_debug_bus_write(machine, ViewportSram + 16, 5); // selection end units
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0); // viewport text offset
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 1); // word count
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(selection_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1); // chapter
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 1); // format run count low
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		// Run 0: offset 3, length 1, grammar flag -- cell 3 is inside the
		// [2,5) selection range, so selection must win over grammar there.
		fairy_snes_debug_bus_write(machine, ViewportSram + 384, 3);
		fairy_snes_debug_bus_write(machine, ViewportSram + 385, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 386, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 387, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 388, 0x10);
		for (std::size_t i = 0; i < selection_text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), selection_text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2); // commit
		if (!runFrames(machine, 6)) return 164;
		const std::uint8_t want[8] = {0x08, 0x08, 0x0c, 0x0c, 0x0c, 0x08, 0x08, 0x08};
		for (int i = 0; i < 8; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1200 + i) != want[i]) {
				std::fprintf(stderr,
					"selection highlight mismatch at cell %d: got=%02x want=%02x attrs=%02x %02x %02x %02x %02x %02x %02x %02x\n",
					i, fairy_snes_debug_wram(machine, 0x1200 + i), want[i],
					fairy_snes_debug_wram(machine, 0x1200), fairy_snes_debug_wram(machine, 0x1201),
					fairy_snes_debug_wram(machine, 0x1202), fairy_snes_debug_wram(machine, 0x1203),
					fairy_snes_debug_wram(machine, 0x1204), fairy_snes_debug_wram(machine, 0x1205),
					fairy_snes_debug_wram(machine, 0x1206), fairy_snes_debug_wram(machine, 0x1207));
				return 165;
			}
		}
	}
	fairy_snes_destroy(machine);

	// An empty/collapsed selection (start == end, e.g. a plain cursor with no
	// drag) must not highlight anything -- only a real range highlights.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 166;
	{
		constexpr std::string_view collapsed_text = "abcdefgh";
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 12, 2); // selection start == end
		fairy_snes_debug_bus_write(machine, ViewportSram + 16, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(collapsed_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < collapsed_text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), collapsed_text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 6)) return 167;
		for (int i = 0; i < 8; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1200 + i) != 0x08) {
				std::fprintf(stderr, "collapsed selection unexpectedly highlighted cell %d: attr=%02x\n",
					i, fairy_snes_debug_wram(machine, 0x1200 + i));
				return 168;
			}
		}
	}
	fairy_snes_destroy(machine);

	// A local edit or cursor move must not strip styling, proofing or the
	// selection highlight.
	//
	// The cartridge draws an immediate optimistic frame on every keystroke, and
	// the host's authoritative viewport only lands about two frames later. Any
	// disagreement between the two is visible as a flash, and the local render
	// used to discard exactly the state that is hardest to miss: `STZ $1f`
	// cleared the draw loop's styling flag, so bold/italic/underline, spelling
	// and grammar colours, and the selection highlight all blinked off on every
	// key and snapped back when the host caught up.
	//
	// This harness has no host, so nothing ever republishes the viewport:
	// whatever the local render leaves on screen stays there. That turns the
	// defect from a two-frame transient into a permanent state, which is what
	// makes it assertable at all.
	{
		// Cells 0-1 bold+italic (attribute 0x09, character +128), cell 6 a
		// spelling issue (attribute 0x0c), cells 2-4 selected (0x0c, which wins
		// over anything underneath it). Assertions are on the attribute plane
		// plus the two styled character bytes; the caret glyph rewrites the
		// character byte of whichever cell it sits on but deliberately leaves
		// that cell's attribute alone, so the attribute plane is stable under
		// cursor movement and the character bytes are only checked for cells the
		// caret is kept away from.
		constexpr std::string_view styled_text = "abcdefgh";
		const std::uint8_t wantAttr[8] = {0x09, 0x09, 0x0c, 0x0c, 0x0c, 0x08, 0x0c, 0x08};

		const auto commitStyled = [&](FairySnesMachine* m, std::uint8_t cursor,
			bool selected) {
			fairy_snes_debug_bus_write(m, ViewportSram + 8, cursor);
			fairy_snes_debug_bus_write(m, ViewportSram + 12, selected ? 2 : 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 16, selected ? 5 : 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 20, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 28, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 32,
				static_cast<std::uint8_t>(styled_text.size()));
			fairy_snes_debug_bus_write(m, ViewportSram + 34, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 124, 2); // two format runs
			fairy_snes_debug_bus_write(m, ViewportSram + 125, 0);
			// Run 0: cells [0,2) bold+italic. Run stride is eight bytes.
			fairy_snes_debug_bus_write(m, ViewportSram + 384, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 385, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 386, 2);
			fairy_snes_debug_bus_write(m, ViewportSram + 387, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 388, 0x03);
			// Run 1: cell 6 flagged by the spell checker.
			fairy_snes_debug_bus_write(m, ViewportSram + 392, 6);
			fairy_snes_debug_bus_write(m, ViewportSram + 393, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 394, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 395, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 396, 0x08);
			for (std::size_t i = 0; i < styled_text.size(); ++i) {
				fairy_snes_debug_bus_write(m,
					ViewportTextSram + static_cast<std::uint32_t>(i), styled_text[i]);
			}
			fairy_snes_debug_bus_write(m, 0x70000a, 2);
			return runFrames(m, 6);
		};

		const auto checkStyled = [&](FairySnesMachine* m, const char* after,
			int cells) {
			for (int i = 0; i < cells; ++i) {
				if (fairy_snes_debug_wram(m, 0x1200 + i) != wantAttr[i]) {
					std::fprintf(stderr,
						"styling lost after %s: cell %d attr got=%02x want=%02x "
						"(row %02x %02x %02x %02x %02x %02x %02x %02x)\n",
						after, i, fairy_snes_debug_wram(m, 0x1200 + i), wantAttr[i],
						fairy_snes_debug_wram(m, 0x1200), fairy_snes_debug_wram(m, 0x1201),
						fairy_snes_debug_wram(m, 0x1202), fairy_snes_debug_wram(m, 0x1203),
						fairy_snes_debug_wram(m, 0x1204), fairy_snes_debug_wram(m, 0x1205),
						fairy_snes_debug_wram(m, 0x1206), fairy_snes_debug_wram(m, 0x1207));
					return false;
				}
			}
			// The bold+italic glyph page is carried by bit 7 of the character
			// byte, which the attribute plane cannot show on its own.
			if (fairy_snes_debug_wram(m, 0x1000) != ('a' | 0x80)
				|| fairy_snes_debug_wram(m, 0x1001) != ('b' | 0x80)) {
				std::fprintf(stderr,
					"bold glyph page lost after %s: chars got=%02x %02x want=%02x %02x\n",
					after, fairy_snes_debug_wram(m, 0x1000),
					fairy_snes_debug_wram(m, 0x1001),
					static_cast<unsigned>('a' | 0x80), static_cast<unsigned>('b' | 0x80));
				return false;
			}
			return true;
		};

		// Cursor keys shift no cells at all, so the per-cell style map, the
		// alignment map and the selection bounds are all still exactly right
		// after one: there was never a reason to drop them.
		const struct { std::uint8_t code; bool extended; const char* name; } navKeys[] = {
			{0x74, true, "Right"}, {0x6b, true, "Left"},
			{0x75, true, "Up"}, {0x72, true, "Down"},
		};
		for (const auto& key : navKeys) {
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 174;
			if (!commitStyled(m, 5, true)) return 175;
			if (!checkStyled(m, "the initial commit", 8)) return 176;
			if (!fairy_snes_key_event(m, key.code, true, key.extended)
				|| !runFrames(m, 6)) return 177;
			if (!checkStyled(m, key.name, 8)) return 178;
			fairy_snes_destroy(m);
		}

		// Typing past the end of the styled text shifts nothing before the
		// caret, so every styled cell must survive the insert untouched.
		{
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 179;
			if (!commitStyled(m, static_cast<std::uint8_t>(styled_text.size()), false)) return 180;
			if (!fairy_snes_key_event(m, 0x1a, true, false) || !runFrames(m, 6)) return 181; // 'z'
			// No selection in this commit, so cells 2-4 are plain.
			for (int i = 0; i < 8; ++i) {
				const std::uint8_t want = (i < 2) ? 0x09 : (i == 6 ? 0x0c : 0x08);
				if (fairy_snes_debug_wram(m, 0x1200 + i) != want) {
					std::fprintf(stderr,
						"styling lost after typing at the end: cell %d attr got=%02x want=%02x\n",
						i, fairy_snes_debug_wram(m, 0x1200 + i), want);
					return 182;
				}
			}
			fairy_snes_destroy(m);
		}

		// Typing at the start shifts every cell right by one, so the style map
		// has to move with the text rather than being discarded: the two styled
		// cells belong to the characters, not to the positions.
		{
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 183;
			if (!commitStyled(m, 0, false)) return 184;
			if (!fairy_snes_key_event(m, 0x1a, true, false) || !runFrames(m, 6)) return 185; // 'z'
			// The inserted cell has no preceding character to inherit from, so
			// it takes the plain baseline; everything else moves up one.
			const std::uint8_t want[9] = {0x08, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x0c, 0x08};
			for (int i = 0; i < 9; ++i) {
				if (fairy_snes_debug_wram(m, 0x1200 + i) != want[i]) {
					std::fprintf(stderr,
						"style map did not shift with an insert at the start: cell %d attr "
						"got=%02x want=%02x (row %02x %02x %02x %02x %02x %02x %02x %02x %02x)\n",
						i, fairy_snes_debug_wram(m, 0x1200 + i), want[i],
						fairy_snes_debug_wram(m, 0x1200), fairy_snes_debug_wram(m, 0x1201),
						fairy_snes_debug_wram(m, 0x1202), fairy_snes_debug_wram(m, 0x1203),
						fairy_snes_debug_wram(m, 0x1204), fairy_snes_debug_wram(m, 0x1205),
						fairy_snes_debug_wram(m, 0x1206), fairy_snes_debug_wram(m, 0x1207),
						fairy_snes_debug_wram(m, 0x1208));
					return 186;
				}
			}
			// 'b' has moved to cell 2 and must still carry its bold+italic page.
			// Cell 1 holds 'a' but cannot be checked here: the cursor is at 1
			// after the insert, and the caret deliberately replaces the character
			// byte of the cell it sits on while leaving that cell's attribute
			// (asserted above) alone.
			if (fairy_snes_debug_wram(m, 0x1002) != ('b' | 0x80)) {
				std::fprintf(stderr,
					"styled glyph did not follow its character: got=%02x want=%02x\n",
					fairy_snes_debug_wram(m, 0x1002), static_cast<unsigned>('b' | 0x80));
				return 187;
			}
			fairy_snes_destroy(m);
		}

		// Typing replaces a selection in DocumentEngine, so the optimistic frame
		// must drop the highlight immediately rather than holding a stale one up
		// for two frames -- that would be a new disagreement, not a fixed one.
		{
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 188;
			if (!commitStyled(m, 5, true)) return 189;
			if (!fairy_snes_key_event(m, 0x1a, true, false) || !runFrames(m, 6)) return 190; // 'z'
			for (int i = 0; i < 9; ++i) {
				if (fairy_snes_debug_wram(m, 0x1200 + i) == 0x0c
					&& i != 7) { // cell 7 is the spelling flag, shifted up by one
					std::fprintf(stderr,
						"selection highlight survived an edit at cell %d\n", i);
					return 191;
				}
			}
			fairy_snes_destroy(m);
		}

		// Shift+arrow publishes an Extend command and the host grows the
		// selection, but the local frame has to grow it too or the highlight
		// blinks off and returns one cell wider.
		{
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 192;
			if (!commitStyled(m, 5, true)) return 193;
			if (!fairy_snes_key_event(m, 0x12, true, false) || !runFrames(m, 2)) return 194; // Shift
			if (!fairy_snes_key_event(m, 0x74, true, true) || !runFrames(m, 6)) return 195; // Right
			// The cursor was the selection's active edge at 5, so [2,5) becomes
			// [2,6) and cell 5 joins the highlight.
			const std::uint8_t want[8] = {0x09, 0x09, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x08};
			for (int i = 0; i < 8; ++i) {
				if (fairy_snes_debug_wram(m, 0x1200 + i) != want[i]) {
					std::fprintf(stderr,
						"Shift+Right did not extend the local selection: cell %d attr "
						"got=%02x want=%02x (row %02x %02x %02x %02x %02x %02x %02x %02x)\n",
						i, fairy_snes_debug_wram(m, 0x1200 + i), want[i],
						fairy_snes_debug_wram(m, 0x1200), fairy_snes_debug_wram(m, 0x1201),
						fairy_snes_debug_wram(m, 0x1202), fairy_snes_debug_wram(m, 0x1203),
						fairy_snes_debug_wram(m, 0x1204), fairy_snes_debug_wram(m, 0x1205),
						fairy_snes_debug_wram(m, 0x1206), fairy_snes_debug_wram(m, 0x1207));
					return 196;
				}
			}
			fairy_snes_destroy(m);
		}
	}

	// A selection that runs to the end of the viewport text must still highlight.
	// The decode loop matches a position only on a character it actually visits,
	// and it never visits the offset one past the last one, so the end bound was
	// captured nowhere and stayed at whatever the previous viewport left -- zero on
	// a fresh document, which makes the range empty and the highlight vanish.
	// Shift+End and select-all land exactly here.
	machine = fairy_snes_create(rom.data(), rom.size());
	if (!machine || !runFrames(machine, 3)) return 197;
	{
		constexpr std::string_view tail_text = "abcdefgh";
		fairy_snes_debug_bus_write(machine, ViewportSram + 8,
			static_cast<std::uint8_t>(tail_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 12, 5); // selection [5, 8)
		fairy_snes_debug_bus_write(machine, ViewportSram + 16,
			static_cast<std::uint8_t>(tail_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32,
			static_cast<std::uint8_t>(tail_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < tail_text.size(); ++i) {
			fairy_snes_debug_bus_write(machine,
				ViewportTextSram + static_cast<std::uint32_t>(i), tail_text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 6)) return 198;
		const std::uint8_t want[8] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x0c, 0x0c, 0x0c};
		for (int i = 0; i < 8; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1200 + i) != want[i]) {
				std::fprintf(stderr,
					"selection to the end of the text did not highlight: cell %d attr "
					"got=%02x want=%02x (row %02x %02x %02x %02x %02x %02x %02x %02x)\n",
					i, fairy_snes_debug_wram(machine, 0x1200 + i), want[i],
					fairy_snes_debug_wram(machine, 0x1200), fairy_snes_debug_wram(machine, 0x1201),
					fairy_snes_debug_wram(machine, 0x1202), fairy_snes_debug_wram(machine, 0x1203),
					fairy_snes_debug_wram(machine, 0x1204), fairy_snes_debug_wram(machine, 0x1205),
					fairy_snes_debug_wram(machine, 0x1206), fairy_snes_debug_wram(machine, 0x1207));
				return 199;
			}
		}
	}
	fairy_snes_destroy(machine);

	// Rich style (bold/italic/underline): the three flags are independent, so
	// every combination has to reach the screen as its own glyph page. The page
	// index is the flag mask itself; its bit 0 lands in the character byte
	// (+128) and its remaining two bits in the attribute byte's tile-id bits
	// 8-9. Proofing keeps its own axis in the palette bits, so a styled cell can
	// also be flagged. Held together, these cases pin the whole mapping: no
	// combination may collapse onto another one's page.
	{
		struct StyleCase {
			std::uint8_t flags;
			std::uint8_t wantAttr;
			std::uint8_t wantTileLow;
		};
		const StyleCase cases[] = {
			{0x01, 0x08, 0xc1}, // bold: page 1
			{0x02, 0x09, 0x41}, // italic: page 2
			{0x03, 0x09, 0xc1}, // bold+italic: page 3
			{0x04, 0x0a, 0x41}, // underline: page 4
			{0x05, 0x0a, 0xc1}, // bold+underline: page 5
			{0x06, 0x0b, 0x41}, // italic+underline: page 6
			{0x07, 0x0b, 0xc1}, // bold+italic+underline: page 7
			{0x09, 0x0c, 0xc1}, // bold+spelling: spelling palette, page 1
			{0x0c, 0x0e, 0x41}, // underline+spelling: spelling palette, page 4
			{0x12, 0x05, 0x41}, // italic+grammar: grammar palette, page 2
		};
		for (const auto& tc : cases) {
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 169;
			constexpr std::string_view text = "ABCDE";
			// Cursor parked at the end of the text (past cell 0, which this
			// test inspects) so the caret glyph -- correctly drawn at the
			// true cursor cell now that the caret-position bug is fixed --
			// doesn't overwrite the tile-low byte this test is checking.
			fairy_snes_debug_bus_write(m, ViewportSram + 8, static_cast<std::uint8_t>(text.size()));
			fairy_snes_debug_bus_write(m, ViewportSram + 20, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 28, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
			fairy_snes_debug_bus_write(m, ViewportSram + 34, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 124, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 125, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 384, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 385, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 386, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 387, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 388, tc.flags);
			for (std::size_t i = 0; i < text.size(); ++i) {
				fairy_snes_debug_bus_write(m, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
			}
			fairy_snes_debug_bus_write(m, 0x70000a, 2);
			if (!runFrames(m, 6)) return 170;
			const std::uint8_t gotAttr = fairy_snes_debug_wram(m, 0x1200);
			const std::uint8_t gotTileLow = fairy_snes_debug_wram(m, 0x1000);
			if (gotAttr != tc.wantAttr || gotTileLow != tc.wantTileLow) {
				std::fprintf(stderr,
					"rich style mismatch for flags=%02x: attr got=%02x want=%02x, tileLow got=%02x want=%02x\n",
					tc.flags, gotAttr, tc.wantAttr, gotTileLow, tc.wantTileLow);
				return 171;
			}
			fairy_snes_destroy(m);
		}
		// The tilemap bytes above are the mapping; these are the pixels. Every
		// one of the eight style combinations must draw a visibly different
		// first character, which is what proves the styled glyph pages exist in
		// VRAM at the strided ids the attribute bytes point at -- a correct
		// tile id into an unuploaded page renders as garbage or as the plain
		// glyph, and the byte assertions alone cannot tell the difference.
		std::uint64_t rendered[8] = {};
		for (std::uint8_t style = 0; style < 8; ++style) {
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 172;
			constexpr std::string_view text = "ABCDE";
			fairy_snes_debug_bus_write(m, ViewportSram + 8, static_cast<std::uint8_t>(text.size()));
			fairy_snes_debug_bus_write(m, ViewportSram + 20, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 28, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
			fairy_snes_debug_bus_write(m, ViewportSram + 34, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 124, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 125, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 384, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 385, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 386, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 387, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 388, style);
			for (std::size_t i = 0; i < text.size(); ++i) {
				fairy_snes_debug_bus_write(m, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
			}
			fairy_snes_debug_bus_write(m, 0x70000a, 2);
			if (!runFrames(m, 6)) return 172;
			// The document plane's first cell sits at screen (8,80).
			rendered[style] = blockHash(m, 8, 80, 8, 8);
			fairy_snes_destroy(m);
		}
		static const char* const style_names[8] = {
			"plain", "bold", "italic", "bold+italic", "underline",
			"bold+underline", "italic+underline", "bold+italic+underline"
		};
		for (int a = 0; a < 8; ++a) {
			for (int b = a + 1; b < 8; ++b) {
				if (rendered[a] == rendered[b]) {
					std::fprintf(stderr,
						"style combinations %s and %s render identical pixels\n",
						style_names[a], style_names[b]);
					return 173;
				}
			}
		}
	}

	// Paragraph alignment. The host publishes it per format run; the cartridge
	// owns the 30-column layout, so centring and right alignment are the
	// cartridge translating each finished row inside its own width. What is being
	// pinned here is the whole chain: a run's alignment byte reaching the
	// per-character plane, each visual line of a wrapped paragraph being placed
	// on its own width rather than the paragraph's, and the caret and the mouse
	// staying on the characters they belong to once a row has moved.
	{
		// "LEFT" (4), "CENTRE" (6), "RIGHT" (5), then a centred paragraph long
		// enough to wrap. Each entry is one paragraph and one plane row, except
		// the last, which is three.
		struct AlignPara final {
			std::string text;
			std::uint8_t alignment;
		};
		const AlignPara paragraphs[] = {
			{"LEFT", 0},
			{"CENTRE", 1},
			{"RIGHT", 2},
			{"WRAPPED CENTRED TEXT THAT RUNS PAST ONE ROW OF THE PLANE", 1}
		};
		std::string text;
		std::vector<std::pair<std::uint16_t, std::uint16_t>> spans;
		for (const AlignPara& para : paragraphs) {
			spans.emplace_back(static_cast<std::uint16_t>(text.size()),
				static_cast<std::uint16_t>(para.text.size()));
			text += para.text;
			text += "\n";
		}
		const auto publish = [&](FairySnesMachine* m, std::uint16_t cursor) {
			for (std::size_t i = 0; i < text.size(); ++i) {
				fairy_snes_debug_bus_write(m, ViewportTextSram + static_cast<std::uint32_t>(i),
					static_cast<std::uint8_t>(text[i]));
			}
			fairy_snes_debug_bus_write(m, ViewportSram + 8, static_cast<std::uint8_t>(cursor));
			fairy_snes_debug_bus_write(m, ViewportSram + 9, static_cast<std::uint8_t>(cursor >> 8));
			fairy_snes_debug_bus_write(m, ViewportSram + 20, 0);
			fairy_snes_debug_bus_write(m, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
			fairy_snes_debug_bus_write(m, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
			fairy_snes_debug_bus_write(m, ViewportSram + 34, 1);
			fairy_snes_debug_bus_write(m, ViewportSram + 124,
				static_cast<std::uint8_t>(std::size(paragraphs)));
			fairy_snes_debug_bus_write(m, ViewportSram + 125, 0);
			for (std::size_t i = 0; i < std::size(paragraphs); ++i) {
				const std::uint32_t at = ViewportSram + 384 + static_cast<std::uint32_t>(i * 8);
				fairy_snes_debug_bus_write(m, at + 0, static_cast<std::uint8_t>(spans[i].first));
				fairy_snes_debug_bus_write(m, at + 1, static_cast<std::uint8_t>(spans[i].first >> 8));
				fairy_snes_debug_bus_write(m, at + 2, static_cast<std::uint8_t>(spans[i].second));
				fairy_snes_debug_bus_write(m, at + 3, static_cast<std::uint8_t>(spans[i].second >> 8));
				fairy_snes_debug_bus_write(m, at + 4, 0);
				fairy_snes_debug_bus_write(m, at + 5, 0);
				fairy_snes_debug_bus_write(m, at + 6, paragraphs[i].alignment);
				fairy_snes_debug_bus_write(m, at + 7, 0);
			}
			fairy_snes_debug_bus_write(m, 0x70000a, 2);
		};
		const auto planeRow = [](FairySnesMachine* m, int row) {
			return wramString(m, 0x1000 + static_cast<std::uint32_t>(row * 30), 30);
		};

		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 174;
		// The cursor is parked past the end so the block cursor cannot replace a
		// glyph in any row this checks.
		publish(machine, static_cast<std::uint16_t>(text.size()));
		if (!runFrames(machine, 6)) return 174;
		// A left paragraph starts at the margin; a centred one is placed on
		// (30 - width) / 2; a right one ends at the last column. The wrapped
		// centred paragraph is centred line by line: its first line happens to
		// fill the plane exactly and cannot move, while its second is placed on
		// its own 25 cells rather than the paragraph's length.
		const struct { int row; const char* want; std::uint8_t shift; } rows[] = {
			{0, "LEFT                          ", 0},
			{1, "            CENTRE            ", 12},
			{2, "                         RIGHT", 25},
			{3, "WRAPPED CENTRED TEXT THAT RUNS", 0},
			{4, "  PAST ONE ROW OF THE PLANE   ", 2}
		};
		for (const auto& row : rows) {
			const std::string got = planeRow(machine, row.row);
			const std::string want(row.want);
			if (got != want
				|| fairy_snes_debug_wram(machine, 0x0f00 + static_cast<std::uint32_t>(row.row))
					!= row.shift) {
				std::fprintf(stderr,
					"alignment row %d\n  got  |%s| shift=%u\n  want |%s| shift=%u\n",
					row.row, got.c_str(),
					fairy_snes_debug_wram(machine, 0x0f00 + static_cast<std::uint32_t>(row.row)),
					want.c_str(), row.shift);
				return 175;
			}
		}
		fairy_snes_destroy(machine);

		// The caret belongs to a character, not to a cell: with the cursor on the
		// centred paragraph's second character, the block cursor has to sit on
		// that character where it is actually drawn.
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 176;
		publish(machine, static_cast<std::uint16_t>(spans[1].first + 1)); // "CENTRE"'s 'E'
		if (!runFrames(machine, 6)) return 176;
		const std::uint32_t caret = fairy_snes_debug_wram(machine, 0x0b)
			| (static_cast<std::uint32_t>(fairy_snes_debug_wram(machine, 0x0c)) << 8);
		if (caret != 30 + 12 + 1) {
			std::fprintf(stderr,
				"the caret did not follow the centred row: cell=%u want=%u\n", caret, 30 + 12 + 1);
			return 177;
		}
		fairy_snes_destroy(machine);

		// Clicking a centred row must select the character under the pointer.
		// The layout the hit-test walks is flush left, so a visible cell has to be
		// brought back into layout space or every click on a centred line would
		// land further into the text than the character it was aimed at.
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 178;
		publish(machine, 0);
		if (!runFrames(machine, 6)) return 178;
		const int producer_before = fairy_snes_debug_bus_read(machine, 0x700002)
			| (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
		// Row 1 column 12 is where 'C' of "CENTRE" is drawn: screen (8+96, 80+8).
		fairy_snes_mouse_event(machine, -24, -24, false, false); // (104, 88)
		if (!runFrames(machine, 2)) return 178;
		fairy_snes_mouse_event(machine, 0, 0, true, false);
		if (!runFrames(machine, 20)) return 178;
		const int producer_after = fairy_snes_debug_bus_read(machine, 0x700002)
			| (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
		const std::uint32_t click = 0x700100 + static_cast<std::uint32_t>(producer_after - 22);
		const std::uint16_t offset = fairy_snes_debug_bus_read(machine, click + 20)
			| (static_cast<std::uint16_t>(fairy_snes_debug_bus_read(machine, click + 21)) << 8);
		if (producer_after == producer_before
			|| fairy_snes_debug_bus_read(machine, click + 2) != 0x04
			|| fairy_snes_debug_bus_read(machine, click + 3) != 0x01
			|| offset != spans[1].first) {
			std::fprintf(stderr,
				"clicking the centred row's first character published offset %u, want %u (kind=%02x%02x)\n",
				offset, spans[1].first,
				fairy_snes_debug_bus_read(machine, click + 3),
				fairy_snes_debug_bus_read(machine, click + 2));
			return 179;
		}
		fairy_snes_destroy(machine);
	}

	// Toolbar click routing: clicking each bold/italic/underline/alignment
	// button emits the matching zero-payload DocumentEngine command kind
	// (20-25) into the commands ring. Boot pointer position is (128,112);
	// button centers are derived from the toolbar plane's screen rectangle
	// (x 160-247, y 8-23; see "Upload the 2x11 toolbar plane" and
	// `toolbarClick` in tools/fairywriter-rom/main.go).
	{
		struct ToolbarCase {
			int x, y;
			std::uint8_t want_kind;
		};
		const ToolbarCase cases[] = {
			{179, 11, 20}, // bold
			{203, 11, 21}, // italic
			{227, 11, 22}, // underline
			{179, 19, 23}, // align left
			{203, 19, 24}, // align center
			{227, 19, 25}, // align right
		};
		for (const auto& tc : cases) {
			FairySnesMachine* m = fairy_snes_create(rom.data(), rom.size());
			if (!m || !runFrames(m, 3)) return 172;
			fairy_snes_mouse_event(m, static_cast<std::int8_t>(tc.x - 128), static_cast<std::int8_t>(tc.y - 112), false, false);
			if (!runFrames(m, 4)) return 173;
			if (fairy_snes_debug_wram(m, 0x0334) != tc.x || fairy_snes_debug_wram(m, 0x0335) != tc.y) {
				std::fprintf(stderr, "toolbar pointer did not reach (%d,%d): got (%u,%u)\n", tc.x, tc.y,
					fairy_snes_debug_wram(m, 0x0334), fairy_snes_debug_wram(m, 0x0335));
				return 174;
			}
			fairy_snes_mouse_event(m, 0, 0, true, false); // left button rising edge
			if (!runFrames(m, 20)) return 175;
			const int producer = fairy_snes_debug_bus_read(m, 0x700002) | (fairy_snes_debug_bus_read(m, 0x700003) << 8);
			if (producer != 20 || fairy_snes_debug_bus_read(m, 0x700102) != tc.want_kind ||
				fairy_snes_debug_bus_read(m, 0x700103) != 0 || fairy_snes_debug_bus_read(m, 0x700104) != 0) {
				std::fprintf(stderr,
					"toolbar click at (%d,%d) did not emit kind=%u: producer=%d kind=%02x%02x count=%u\n",
					tc.x, tc.y, tc.want_kind, producer, fairy_snes_debug_bus_read(m, 0x700103),
					fairy_snes_debug_bus_read(m, 0x700102), fairy_snes_debug_bus_read(m, 0x700104));
				return 176;
			}
			fairy_snes_destroy(m);
		}
	}

	// Caret positioning: the caret must render at the true cursor cell, not
	// at the end of the rendered text. Two unconditional overwrite sites in
	// the draw loop (natural end-of-text, and the screenFull overflow path)
	// used to clobber $0b with the current/sentinel screen position whenever
	// the cursor sat before the very last rendered character -- this commits
	// a 10-character single-line document with the cursor in the middle and
	// checks both that $0b lands on the true cell and that the caret glyph
	// (0x7f) is drawn there, while a neighboring real character survives
	// untouched.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 177;
		constexpr std::string_view text = "abcdefghij";
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, 5); // cursor mid-document
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 6)) return 178;
		if (fairy_snes_debug_wram(machine, 0x1000 + 5) != 0x7f) {
			std::fprintf(stderr, "caret glyph did not render at the true mid-document cursor cell: tile=%02x\n",
				fairy_snes_debug_wram(machine, 0x1000 + 5));
			return 179;
		}
		if (fairy_snes_debug_wram(machine, 0x1000 + 4) != 'e' || fairy_snes_debug_wram(machine, 0x1000 + 6) != 'g') {
			std::fprintf(stderr, "neighboring characters were disturbed by caret rendering: [4]=%02x [6]=%02x\n",
				fairy_snes_debug_wram(machine, 0x1000 + 4), fairy_snes_debug_wram(machine, 0x1000 + 6));
			return 180;
		}
		fairy_snes_destroy(machine);
	}

	// Word-wrap counter overflow: $0c (word-length counter) is a single
	// zero-page byte incremented via 8-bit INC while measuring an unbroken
	// run of non-space characters. A run of 256+ such characters used to
	// wrap $0c back toward 0, which could make an oversized word look like
	// it "fits" on the current line after all. This commits "hi " (leaving
	// column 3) followed by 256 unbroken 'a' characters -- long enough to
	// wrap $0c to exactly 0 under the old bug -- and checks the word is
	// still correctly pushed to a fresh line rather than drawn starting at
	// column 3 of the first line.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 186;
		const std::string text = "hi " + std::string(256, 'a');
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 10)) return 187;
		if (fairy_snes_debug_wram(machine, 0x1000) != 'h' || fairy_snes_debug_wram(machine, 0x1001) != 'i'
			|| fairy_snes_debug_wram(machine, 0x1002) != ' ') {
			return 188;
		}
		for (std::size_t i = 3; i < 30; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + i) != ' ') {
				std::fprintf(stderr,
					"256-char word was drawn mid-line instead of wrapping to a fresh line: cell %zu = %02x\n",
					i, fairy_snes_debug_wram(machine, 0x1000 + i));
				return 189;
			}
		}
		for (std::size_t i = 0; i < 30; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + 30 + i) != 'a') {
				std::fprintf(stderr, "256-char word did not start at column 0 of the next line: cell %zu = %02x\n",
					i, fairy_snes_debug_wram(machine, 0x1000 + 30 + i));
				return 190;
			}
		}
		fairy_snes_destroy(machine);
	}

	// Word-fit addition overflow: the saturation guard above bounds $0c itself,
	// but the fit check then evaluated `column + $0c` in an 8-bit accumulator,
	// which can overflow on its own for a word short enough to never trip the
	// saturation. This commits a 16-character prefix (leaving column 16)
	// followed by an unbroken 240-character word: 16 + 240 is exactly 256, so
	// the old accumulator wrapped to 0, compared below 31, and drew the
	// oversized word starting mid-line at column 16. The word must instead be
	// pushed to a fresh line.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 191;
		const std::string text = "abcdefghijklmno " + std::string(240, 'z');
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 10)) return 192;
		for (std::size_t i = 0; i < 16; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + i) != static_cast<std::uint8_t>(text[i])) {
				std::fprintf(stderr, "prefix mismatch at cell %zu: got=%02x want=%02x\n", i,
					fairy_snes_debug_wram(machine, 0x1000 + i), static_cast<std::uint8_t>(text[i]));
				return 193;
			}
		}
		for (std::size_t i = 16; i < 30; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + i) != ' ') {
				std::fprintf(stderr,
					"240-char word was drawn mid-line: column+wordLen wrapped in 8 bits (cell %zu = %02x)\n",
					i, fairy_snes_debug_wram(machine, 0x1000 + i));
				return 194;
			}
		}
		for (std::size_t i = 0; i < 30; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + 30 + i) != 'z') {
				std::fprintf(stderr, "240-char word did not start at column 0 of the next line: cell %zu = %02x\n",
					i, fairy_snes_debug_wram(machine, 0x1000 + 30 + i));
				return 195;
			}
		}
		fairy_snes_destroy(machine);
	}

	// Word-wrap padding must not advance the output cursor past the 510-cell
	// document plane. `advanceToLine` pads the tail of a line when a word is
	// pushed to the next row, and it carried a `CPY #$f0 / BCC / JMP screenFull`
	// bound -- but renderDocument runs with 16-bit index registers (`REP #$10`),
	// where `CPY #imm` is three bytes. The bound was emitted as the two-byte
	// `c0 f0`, so it swallowed the `BCC` opcode, decoded as `CPY #$90F0`, and
	// turned the guard into two garbage instructions that only clobbered A --
	// which the next `LDA $0a` reloaded, hiding the defect completely. With no
	// live bound the padding could carry Y up to 29 cells past the plane and the
	// following draw wrote a glyph out of bounds: this exact text used to leave
	// 'w' at $11FE (cell 510, one past the plane), and a wrap starting from a
	// lower column reaches into the attribute plane at $1200. Cells 510+ must
	// stay untouched by document glyphs.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 250;
		std::string text;
		for (int w = 0; w < 120; ++w) text += "wwwwwwwwwwwwwwwwwwww ";
		if (text.size() > 2000) text.resize(2000);
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 20)) return 251;
		// The last in-bounds row must still render, so the tightened bound did
		// not truncate the plane back to its stale 8-row value.
		for (std::size_t i = 0; i < 20; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + 16 * 30 + i) != 'w') {
				std::fprintf(stderr, "row 16 lost text: the plane bound truncated rendering (cell %zu = %02x)\n",
					i, fairy_snes_debug_wram(machine, 0x1000 + 16 * 30 + i));
				return 252;
			}
		}
		// Cells 510 onward are past the plane. Nothing the document draws may
		// land there, and in particular not in the attribute plane at $1200.
		for (int i = 510; i < 540; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + i) == 'w') {
				std::fprintf(stderr,
					"word-wrap padding wrote a glyph past the 510-cell plane at cell %d ($%04x)\n",
					i, 0x1000 + i);
				return 253;
			}
		}
		fairy_snes_destroy(machine);
	}

	// The caret must render for a cursor in the middle of the document, not only
	// at the natural end of text. renderDocument runs with 16-bit index
	// registers, so `positionCursor`'s `STY $0b` capture writes a 16-bit cell
	// across $0b and $0c, and `drawCursor` reads it back with a 16-bit
	// `LDY $0b`. The word-length counter also lived at $0c, and every word
	// measured after the caret was captured re-zeroed and re-counted it --
	// overwriting the caret cell's high byte. The glyph then went to
	// $1000 + (lastWordLen<<8) + column, outside the document plane, so no caret
	// was drawn at all. With the cursor at grapheme 8 of this 61-character
	// document, the caret used to land at $1608 (high byte $06, left over from
	// measuring the trailing word "juliet") instead of $1008.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 240;
		std::string text = "alpha bravo charlie delta echo foxtrot golf hotel india juliet";
		const std::uint16_t cursor = 8; // inside "bravo", with many words after it
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(cursor));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, static_cast<std::uint8_t>(cursor >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 21, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 20)) return 241;
		if (fairy_snes_debug_wram(machine, 0x1000 + cursor) != 0x7f) {
			std::fprintf(stderr,
				"caret glyph missing at cell %u: got=%02x $0b=%02x $0c=%02x $56=%02x\n",
				cursor, fairy_snes_debug_wram(machine, 0x1000 + cursor),
				fairy_snes_debug_wram(machine, 0x000b), fairy_snes_debug_wram(machine, 0x000c),
				fairy_snes_debug_wram(machine, 0x0056));
			for (int i = 0; i < 0x2000; ++i) {
				if (fairy_snes_debug_wram(machine, 0x1000 + i) == 0x7f) {
					std::fprintf(stderr, "  caret glyph actually at offset %d ($%04x)\n", i, 0x1000 + i);
				}
			}
			return 242;
		}
		// The caret cell's high byte must survive the words measured after it.
		if (fairy_snes_debug_wram(machine, 0x000c) != 0) {
			std::fprintf(stderr, "caret cell high byte was clobbered: $0c=%02x\n",
				fairy_snes_debug_wram(machine, 0x000c));
			return 243;
		}
		fairy_snes_destroy(machine);
	}

	// The caret must sit on the active edge of an in-progress selection, not at
	// the far end of it. This is the 2026-07-26 manual observation ("the caret
	// appeared to stay at the document end rather than tracking the active edge
	// of a shift+arrow selection") turned into coverage. The host was already
	// correct -- makeViewport publishes m_cursor.position(), which for a
	// QTextCursor with a selection is the active edge -- so the fault was
	// entirely the $0c caret/word-counter collision above: shift+arrow moves the
	// cursor off the natural end of text, which is exactly the case that lost
	// the caret cell's high byte and drew the glyph outside the plane. Here the
	// user has shift+arrowed leftward from 30 back to 8, so the selection spans
	// [8,30) and the caret belongs at cell 8.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 244;
		const std::string text = "alpha bravo charlie delta echo foxtrot golf hotel india juliet";
		const std::uint16_t active_edge = 8;
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(active_edge));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 12, 8);  // selection start
		fairy_snes_debug_bus_write(machine, ViewportSram + 16, 30); // selection end
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 2);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 20)) return 245;
		if (fairy_snes_debug_wram(machine, 0x1000 + active_edge) != 0x7f) {
			std::fprintf(stderr,
				"caret did not track the active selection edge: cell %u = %02x ($0b=%02x $0c=%02x)\n",
				active_edge, fairy_snes_debug_wram(machine, 0x1000 + active_edge),
				fairy_snes_debug_wram(machine, 0x000b), fairy_snes_debug_wram(machine, 0x000c));
			return 246;
		}
		fairy_snes_destroy(machine);
	}

	// Wrap-aware vertical cursor movement. The host's MoveUp/MoveDown walk the
	// host document's own layout, which knows nothing about the cartridge's
	// 30-column wrap, so they cannot follow what is actually on screen. The
	// cartridge now resolves the target cell itself against its real layout and
	// publishes an absolute caret position (CommandPointerSetCursor, kind
	// $0104) through the same path the mouse pointer uses.
	{
		// Commit a viewport and settle the render. Layout here is a 90-character
		// unbroken run, so cell N holds character N and rows are exactly 30 wide.
		std::uint8_t generation = 1;
		const auto commitViewport = [&](const std::string& text, std::uint16_t cursor) {
			fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(cursor));
			fairy_snes_debug_bus_write(machine, ViewportSram + 9, static_cast<std::uint8_t>(cursor >> 8));
			fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
			fairy_snes_debug_bus_write(machine, ViewportSram + 28, 2);
			fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(text.size()));
			fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(text.size() >> 8));
			fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
			fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
			fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
			for (std::size_t i = 0; i < text.size(); ++i) {
				fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), text[i]);
			}
			// $70000a is a generation counter the cartridge compares against its
			// own copy; re-writing the same value is not a new commit.
			fairy_snes_debug_bus_write(machine, 0x70000a, ++generation);
			return runFrames(machine, 20);
		};
		// A published caret command is 20 header bytes followed by a 2-byte
		// viewport-relative UTF-16 offset.
		const auto record = [&](int index) {
			const std::uint32_t at = 0x700100 + static_cast<std::uint32_t>(index) * 22;
			struct Rec { std::uint8_t kind_lo, kind_hi, count; std::uint16_t offset; };
			return Rec{
				fairy_snes_debug_bus_read(machine, at + 2), fairy_snes_debug_bus_read(machine, at + 3),
				fairy_snes_debug_bus_read(machine, at + 4),
				static_cast<std::uint16_t>(fairy_snes_debug_bus_read(machine, at + 20)
					| (fairy_snes_debug_bus_read(machine, at + 21) << 8))};
		};
		std::string flat;
		for (int i = 0; i < 90; ++i) flat += static_cast<char>('a' + (i % 26));

		// Down from cell 5 (row 0, column 5) lands on cell 35 -- row 1, same
		// column -- which is character 35.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 200;
		if (!commitViewport(flat, 5)) return 201;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 202;
		{
			const auto r = record(0);
			if (r.kind_lo != 0x04 || r.kind_hi != 0x01 || r.count != 2 || r.offset != 35) {
				std::fprintf(stderr,
					"Down did not resolve one wrapped row: kind=%02x%02x count=%u offset=%u (want 0104/2/35)\n",
					r.kind_hi, r.kind_lo, r.count, r.offset);
				return 203;
			}
		}
		fairy_snes_destroy(machine);

		// Up from cell 35 returns to cell 5.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 204;
		if (!commitViewport(flat, 35)) return 205;
		if (!fairy_snes_key_event(machine, 0x75, true, true) || !runFrames(machine, 4)) return 206;
		{
			const auto r = record(0);
			if (r.kind_lo != 0x04 || r.kind_hi != 0x01 || r.offset != 5) {
				std::fprintf(stderr, "Up did not resolve one wrapped row: kind=%02x%02x offset=%u (want 0104/5)\n",
					r.kind_hi, r.kind_lo, r.offset);
				return 207;
			}
		}
		fairy_snes_destroy(machine);

		// Up from row 0 has no row above it on screen, so it must fall back to the
		// host's semantic MoveUp (14) and let the viewport scroll as before.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 208;
		if (!commitViewport(flat, 5)) return 209;
		if (!fairy_snes_key_event(machine, 0x75, true, true) || !runFrames(machine, 4)) return 210;
		{
			const auto r = record(0);
			if (r.kind_lo != 14 || r.kind_hi != 0 || r.count != 0) {
				std::fprintf(stderr, "Up on row 0 did not fall back to semantic MoveUp: kind=%02x%02x count=%u\n",
					r.kind_hi, r.kind_lo, r.count);
				return 211;
			}
		}
		fairy_snes_destroy(machine);

		// Shift extends the selection instead of moving the caret.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 212;
		if (!commitViewport(flat, 5)) return 213;
		if (!fairy_snes_key_event(machine, 0x12, true, false) || !runFrames(machine, 2)) return 214;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 215;
		{
			const auto r = record(0);
			if (r.kind_lo != 0x0f || r.kind_hi != 0x01 || r.offset != 35) {
				std::fprintf(stderr, "shift+Down did not extend the selection: kind=%02x%02x offset=%u\n",
					r.kind_hi, r.kind_lo, r.offset);
				return 216;
			}
		}
		fairy_snes_destroy(machine);

		// Sticky desired column. Ragged layout via explicit CRs:
		//   row 0 = characters 0..19   (20 wide)
		//   row 1 = characters 21..23  (3 wide)
		//   row 2 = characters 25..44  (20 wide)
		// From column 15 on row 0, Down clamps onto row 1's last character (23),
		// and the *next* Down must return to column 15 on row 2 -- character 40 --
		// rather than following the clamped column 2 to character 27.
		std::string ragged(20, 'A');
		ragged += '\r';
		ragged += "BBB";
		ragged += '\r';
		ragged += std::string(20, 'C');
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 217;
		if (!commitViewport(ragged, 15)) return 218;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 219;
		{
			const auto r = record(0);
			if (r.offset != 23) {
				std::fprintf(stderr, "Down onto a short line did not clamp to its end: offset=%u (want 23)\n",
					r.offset);
				return 220;
			}
		}
		// The guest's own optimistic cursor must agree with what was published.
		// The local Up/Down handler used to add or subtract 30 *characters*, which
		// is one screen row only when the layout is exactly 30 characters wide --
		// no wrap, no CRs. Here it clamped to character 45 (end of document) while
		// the wrap-aware resolution published 23, and because that local edit runs
		// before commandEnqueue and its render waits for VBlank, the wrong caret
		// was actually displayed for a frame.
		if (fairy_snes_debug_wram(machine, 0x0000) != 23) {
			std::fprintf(stderr,
				"local optimistic cursor disagrees with the published position: $00=%u (want 23; 45 means the "
				"local +/-30-character move is back)\n",
				fairy_snes_debug_wram(machine, 0x0000));
			return 246;
		}
		// The host would now place the caret at 23; replay that, then press Down
		// again without any intervening keystroke.
		if (!commitViewport(ragged, 23)) return 221;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 222;
		{
			const auto r = record(1);
			if (r.offset != 40) {
				std::fprintf(stderr,
					"sticky column was lost passing through a short line: offset=%u (want 40, got 27 if the "
					"clamped column stuck)\n",
					r.offset);
				return 223;
			}
		}
		fairy_snes_destroy(machine);

		// Resolution must work past cell 255. The hit-test used to compare with
		// `TYA; CMP $0339`, and TYA moves only the low byte of a 16-bit Y, so the
		// comparison wrapped every 256 cells. The pointer never exposed this
		// because it rejects rows >= 8, but vertical movement covers the whole
		// 510-cell plane. Twelve CR-terminated 10-character lines give an exact
		// mapping: line L occupies cells L*30..L*30+9 and starts at character
		// L*11. From line 9 column 3 (character 102, cell 273), Down must land on
		// line 10 column 3 -- character 113. An 8-bit compare would have wrapped
		// the target 303 to 47 and clamped onto line 1 instead.
		std::string tall;
		for (int line = 0; line < 12; ++line) {
			tall += std::string(10, static_cast<char>('A' + line));
			tall += '\r';
		}
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 232;
		if (!commitViewport(tall, 102)) return 233;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 234;
		{
			const auto r = record(0);
			if (r.kind_lo != 0x04 || r.offset != 113) {
				std::fprintf(stderr,
					"Down past cell 255 resolved wrongly: kind=%02x offset=%u (want 113; ~20 means the "
					"hit-test compare is still 8-bit)\n",
					r.kind_lo, r.offset);
				return 235;
			}
		}
		fairy_snes_destroy(machine);

		// A mouse click must publish the clicked character's real offset. The
		// publish path used to read the per-cell offset from $0400, which nothing
		// in the ROM ever writes, so every click sent offset 0 and dropped the
		// caret at the start of the viewport regardless of where you clicked. The
		// tables the viewport decode actually fills are $0700 (low) and $0900
		// (high). The pointer boots at (128,112), which is column 15 of row 4 --
		// cell 135, so character 135 in this flat layout.
		std::string wideEnough;
		for (int i = 0; i < 200; ++i) wideEnough += static_cast<char>('a' + (i % 26));
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 236;
		if (!commitViewport(wideEnough, 0)) return 237;
		fairy_snes_mouse_event(machine, 0, 0, true, false);
		if (!runFrames(machine, 6)) return 238;
		{
			const auto r = record(0);
			if (r.kind_lo != 0x04 || r.kind_hi != 0x01 || r.offset != 135) {
				std::fprintf(stderr,
					"pointer click published the wrong offset: kind=%02x%02x offset=%u (want 0104/135; 0 means "
					"the publish path is reading the unwritten $0400 table again)\n",
					r.kind_hi, r.kind_lo, r.offset);
				return 239;
			}
		}
		fairy_snes_destroy(machine);

		// The guest cursor's high byte must survive a keystroke. $00/$01 is the
		// 16-bit guest cursor -- the draw loop matches the caret with a 16-bit
		// `CPX $00` and the insert path advances it with a 16-bit `INC $00` -- but
		// the dispatch parked the pending key code in $01, so every keystroke
		// turned the cursor into `position + keyCode*256` until the host's next
		// viewport commit reset it. From character 102, Right left $00=103 with
		// $01=18, i.e. a cursor of 4711, putting the optimistic caret far outside
		// the document. The key code now lives at $5b.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 247;
		if (!commitViewport(tall, 102)) return 248;
		if (!fairy_snes_key_event(machine, 0x74, true, true) || !runFrames(machine, 4)) return 249;
		if (fairy_snes_debug_wram(machine, 0x0000) != 103 || fairy_snes_debug_wram(machine, 0x0001) != 0) {
			std::fprintf(stderr,
				"a keystroke corrupted the 16-bit guest cursor: $00=%u $01=%u (want 103/0)\n",
				fairy_snes_debug_wram(machine, 0x0000), fairy_snes_debug_wram(machine, 0x0001));
			return 251;
		}
		fairy_snes_destroy(machine);

		// Clicking below the eighth row must work. The pointer rejected rows >= 8
		// (`CMP #8`), a leftover from when the document plane really was 8 rows;
		// it has been 30x17 for a while, DMAing 17 rows to tilemap row 10 (screen
		// y 80..215), so the whole lower half of the document was unclickable.
		// Fourteen CR-terminated 10-character lines: line L occupies cells
		// L*30..L*30+9 and starts at character L*11. Clicking row 12, column 5 --
		// screen (48, 176) -- must resolve character 12*11+5 = 137.
		std::string deep;
		for (int line = 0; line < 14; ++line) {
			deep += std::string(10, static_cast<char>('A' + line));
			deep += '\r';
		}
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 240;
		if (!commitViewport(deep, 0)) return 241;
		fairy_snes_mouse_event(machine, -80, 64, false, false);
		if (!runFrames(machine, 4)) return 242;
		{
			const std::uint8_t px = fairy_snes_debug_wram(machine, 0x0334);
			const std::uint8_t py = fairy_snes_debug_wram(machine, 0x0335);
			if (px != 48 || py != 176) {
				std::fprintf(stderr, "pointer did not reach row 12 column 5: (%u,%u) want (48,176)\n", px, py);
				return 243;
			}
		}
		fairy_snes_mouse_event(machine, 0, 0, true, false);
		if (!runFrames(machine, 6)) return 244;
		{
			const auto r = record(0);
			if (r.kind_lo != 0x04 || r.kind_hi != 0x01 || r.offset != 137) {
				std::fprintf(stderr,
					"click below row 8 did not resolve: kind=%02x%02x offset=%u (want 0104/137; no record at "
					"all means the row bound is still 8)\n",
					r.kind_hi, r.kind_lo, r.offset);
				return 245;
			}
		}
		fairy_snes_destroy(machine);

		// The document-position track is a real scrollbar. Pressing anywhere on it
		// publishes the thumb's position along its travel (CommandScrollToFraction,
		// kind $0110) and the host decides what part of the document that is. The
		// track runs from x=11 for 234 pixels at y=69..76; the thumb centres under
		// the pointer, so a press at x=128 is thumb 128-11-4 = 113.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 252;
		if (!commitViewport(tall, 0)) return 253;
		fairy_snes_mouse_event(machine, 0, -40, false, false); // 112 -> 72, inside the track
		if (!runFrames(machine, 4)) return 254;
		if (fairy_snes_debug_wram(machine, 0x0335) != 72) {
			std::fprintf(stderr, "pointer did not reach the track: y=%u want 72\n",
				fairy_snes_debug_wram(machine, 0x0335));
			return 255;
		}
		fairy_snes_mouse_event(machine, 0, 0, true, false);
		if (!runFrames(machine, 6)) return 190;
		{
			const std::uint32_t at = 0x700100;
			const std::uint8_t kind_lo = fairy_snes_debug_bus_read(machine, at + 2);
			const std::uint8_t kind_hi = fairy_snes_debug_bus_read(machine, at + 3);
			const std::uint16_t thumb = static_cast<std::uint16_t>(
				fairy_snes_debug_bus_read(machine, at + 20)
				| (fairy_snes_debug_bus_read(machine, at + 21) << 8));
			if (kind_lo != 0x10 || kind_hi != 0x01 || thumb != 113) {
				std::fprintf(stderr,
					"track press did not publish a scroll: kind=%02x%02x thumb=%u (want 0110/113)\n",
					kind_hi, kind_lo, thumb);
				return 191;
			}
		}
		// The thumb sprite follows the pointer immediately rather than waiting for
		// the host to answer, so the drag feels attached.
		if (fairy_snes_debug_wram(machine, 0x0352) != 113 + 11) {
			std::fprintf(stderr, "thumb sprite did not follow the press: x=%u want %u\n",
				fairy_snes_debug_wram(machine, 0x0352), 113 + 11);
			return 192;
		}
		fairy_snes_destroy(machine);

		// Dragging with the button held keeps scrolling, and leaving the thin track
		// vertically must not turn the drag into a text selection -- the press
		// location decides, which is how every real scrollbar behaves. Dragging
		// without moving the thumb must not republish.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 198;
		if (!commitViewport(tall, 0)) return 199;
		fairy_snes_mouse_event(machine, -60, -40, false, false); // x 68, y 72: on the track
		if (!runFrames(machine, 4)) return 201;
		fairy_snes_mouse_event(machine, 0, 0, true, false);      // press -> thumb 68-15 = 53
		if (!runFrames(machine, 6)) return 202;
		fairy_snes_mouse_event(machine, 40, 30, true, false);    // drag right and well below the track
		if (!runFrames(machine, 6)) return 203;
		{
			const std::uint32_t second = 0x700100 + 22;
			const std::uint8_t kind_lo = fairy_snes_debug_bus_read(machine, second + 2);
			const std::uint16_t thumb = static_cast<std::uint16_t>(
				fairy_snes_debug_bus_read(machine, second + 20)
				| (fairy_snes_debug_bus_read(machine, second + 21) << 8));
			if (kind_lo != 0x10 || thumb != 93) {
				std::fprintf(stderr,
					"drag off the track did not keep scrolling: kind=%02x thumb=%u (want 10/93; kind 04 means "
					"it became a text selection)\n",
					kind_lo, thumb);
				return 204;
			}
		}
		{
			const int producer = fairy_snes_debug_bus_read(machine, 0x700002)
				| (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
			fairy_snes_mouse_event(machine, 0, 0, true, false); // held, thumb unchanged
			if (!runFrames(machine, 6)) return 205;
			const int after = fairy_snes_debug_bus_read(machine, 0x700002)
				| (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
			if (after != producer) {
				std::fprintf(stderr,
					"a held drag republished an unchanged thumb: producer %d -> %d\n", producer, after);
				return 206;
			}
		}
		fairy_snes_destroy(machine);

		// A window scrolled away from the caret sets status bit 4, and the
		// cartridge must then draw no caret at all. Without that the draw loop's
		// end-of-text fallback parks one at the end of the visible text, which
		// would look like the caret had jumped there.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 193;
		fairy_snes_debug_bus_write(machine, ViewportSram + 91, 16); // caret out of view
		if (!commitViewport(tall, 0)) return 194;
		for (int i = 0; i < 510; ++i) {
			if (fairy_snes_debug_wram(machine, 0x1000 + i) == 0x7f) {
				std::fprintf(stderr,
					"a caret was drawn at cell %d even though the view is scrolled off it\n", i);
				return 195;
			}
		}
		// Clearing the flag brings it back, so the suppression is the flag's doing
		// and not a caret that stopped rendering for some other reason.
		fairy_snes_debug_bus_write(machine, ViewportSram + 91, 0);
		if (!commitViewport(tall, 0)) return 196;
		{
			bool found = false;
			for (int i = 0; i < 510 && !found; ++i) {
				found = fairy_snes_debug_wram(machine, 0x1000 + i) == 0x7f;
			}
			if (!found) {
				std::fputs("clearing the out-of-view flag did not restore the caret\n", stderr);
				return 197;
			}
		}
		fairy_snes_destroy(machine);

		// A horizontal move must clear the sticky column, so a later Down starts
		// from the caret's real column again.
		machine = fairy_snes_create(rom.data(), rom.size());
		generation = 1;
		if (!machine || !runFrames(machine, 3)) return 224;
		if (!commitViewport(ragged, 15)) return 225;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 226;
		if (!commitViewport(ragged, 23)) return 227;
		if (!fairy_snes_key_event(machine, 0x74, true, true) || !runFrames(machine, 4)) return 228; // Right
		if (!commitViewport(ragged, 23)) return 229;
		if (!fairy_snes_key_event(machine, 0x72, true, true) || !runFrames(machine, 4)) return 230;
		{
			// Records: [0] Down, [1] Right (semantic, zero payload -> 20 bytes), [2] Down.
			const std::uint32_t at = 0x700100 + 22 + 20;
			const std::uint16_t offset = static_cast<std::uint16_t>(fairy_snes_debug_bus_read(machine, at + 20)
				| (fairy_snes_debug_bus_read(machine, at + 21) << 8));
			if (offset != 27) {
				std::fprintf(stderr,
					"a horizontal move did not clear the sticky column: offset=%u (want 27)\n", offset);
				return 231;
			}
		}
		fairy_snes_destroy(machine);
	}

	// Page Up/Page Down: these keys decode to internal semantic codes
	// 0x1a/0x1b correctly, but command_enqueue's dispatch chain used to stop
	// checking at 0x17 (End) and silently drop anything below the printable
	// threshold -- so no command was ever emitted in the document editor
	// (the same codes already worked in the file browser via a separate
	// dispatcher). Each key must now emit a zero-payload MovePageUp(18)/
	// MovePageDown(19) command.
	{
		struct PageCase {
			std::uint8_t scancode;
			std::uint8_t want_kind;
		};
		const PageCase cases[] = {
			{0x7d, 18}, // PageUp -> MovePageUp
			{0x7a, 19}, // PageDown -> MovePageDown
		};
		for (const auto& tc : cases) {
			machine = fairy_snes_create(rom.data(), rom.size());
			if (!machine || !runFrames(machine, 3)) return 181;
			if (!fairy_snes_key_event(machine, tc.scancode, true, true) || !runFrames(machine, 2)) return 182;
			const int producer = fairy_snes_debug_bus_read(machine, 0x700002) | (fairy_snes_debug_bus_read(machine, 0x700003) << 8);
			if (producer != 20 || fairy_snes_debug_bus_read(machine, 0x700102) != tc.want_kind ||
				fairy_snes_debug_bus_read(machine, 0x700103) != 0 || fairy_snes_debug_bus_read(machine, 0x700104) != 0) {
				std::fprintf(stderr,
					"scancode %02x did not emit kind=%u: producer=%d kind=%02x%02x count=%u\n",
					tc.scancode, tc.want_kind, producer, fairy_snes_debug_bus_read(machine, 0x700103),
					fairy_snes_debug_bus_read(machine, 0x700102), fairy_snes_debug_bus_read(machine, 0x700104));
				return 183;
			}
			fairy_snes_destroy(machine);
		}
	}

	// Document-plane clear coverage: rendering a shorter committed viewport
	// after a longer one must blank every cell that the shorter pass no longer
	// paints. The visible document plane is 30 columns x 17 rows (510 cells);
	// clearing only the first 240 cells leaves stale glyphs in rows 8-16.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 191;

		const std::string long_text(300, 'x');
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(long_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, static_cast<std::uint8_t>(long_text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 12, static_cast<std::uint8_t>(long_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 13, static_cast<std::uint8_t>(long_text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 16, static_cast<std::uint8_t>(long_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 17, static_cast<std::uint8_t>(long_text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 20, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(long_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, static_cast<std::uint8_t>(long_text.size() >> 8));
		fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
		for (std::size_t i = 0; i < long_text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), long_text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 2);
		if (!runFrames(machine, 12) || fairy_snes_debug_wram(machine, 0x1000 + 250) != 'x') {
			std::fprintf(stderr, "long viewport did not paint document cell 250 before clear test: tile=%02x\n",
				fairy_snes_debug_wram(machine, 0x1000 + 250));
			return 192;
		}

		constexpr std::string_view short_text = "ok";
		fairy_snes_debug_bus_write(machine, ViewportSram + 8, static_cast<std::uint8_t>(short_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 9, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 12, static_cast<std::uint8_t>(short_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 13, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 16, static_cast<std::uint8_t>(short_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 17, 0);
		fairy_snes_debug_bus_write(machine, ViewportSram + 28, 1);
		fairy_snes_debug_bus_write(machine, ViewportSram + 32, static_cast<std::uint8_t>(short_text.size()));
		fairy_snes_debug_bus_write(machine, ViewportSram + 33, 0);
		for (std::size_t i = 0; i < short_text.size(); ++i) {
			fairy_snes_debug_bus_write(machine, ViewportTextSram + static_cast<std::uint32_t>(i), short_text[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x70000a, 3);
		if (!runFrames(machine, 6)) return 193;
		const std::uint8_t stale_tile = fairy_snes_debug_wram(machine, 0x1000 + 250);
		const std::uint8_t stale_attr = fairy_snes_debug_wram(machine, 0x1200 + 250);
		if (stale_tile != ' ' || stale_attr != 0x08) {
			std::fprintf(stderr,
				"short viewport left stale document cell 250: tile=%02x attr=%02x want=20/08\n",
				stale_tile, stale_attr);
			return 194;
		}
		fairy_snes_destroy(machine);
	}

	// Document-position thumb: sprite 1 rides the static 234px track using the
	// committed bytes_before / total_document_bytes ratio. Exercise a document
	// larger than 8-bit metadata so the cartridge's common-shift normalization
	// is part of the test, then prove both its WRAM position and framebuffer OBJ
	// move from the start through the midpoint to the end.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 4)) return 195;
		const auto write32 = [&](std::uint32_t offset, std::uint32_t value) {
			for (int byte = 0; byte < 4; ++byte) {
				fairy_snes_debug_bus_write(machine, ViewportSram + offset + byte,
					static_cast<std::uint8_t>(value >> (byte * 8)));
			}
		};
		const auto commitPosition = [&](std::uint8_t generation, std::uint32_t before, std::uint32_t after) {
			constexpr std::uint32_t total = 1001;
			write32(0, generation);
			write32(8, before);
			write32(12, before);
			write32(16, before);
			write32(20, before);
			write32(24, total);
			write32(28, 1);
			fairy_snes_debug_bus_write(machine, ViewportSram + 32, 1);
			fairy_snes_debug_bus_write(machine, ViewportSram + 33, 0);
			fairy_snes_debug_bus_write(machine, ViewportSram + 34, 1);
			fairy_snes_debug_bus_write(machine, ViewportSram + 35, 0);
			write32(108, total);
			write32(112, before);
			write32(116, after);
			fairy_snes_debug_bus_write(machine, ViewportSram + 124, 0);
			fairy_snes_debug_bus_write(machine, ViewportSram + 125, 0);
			fairy_snes_debug_bus_write(machine, ViewportTextSram, 'a');
			fairy_snes_debug_bus_write(machine, 0x70000a, generation);
			return runFrames(machine, 8);
		};

		if (!commitPosition(2, 0, 1000) || fairy_snes_debug_wram(machine, 0x0352) != 11) {
			std::fprintf(stderr, "document-position thumb did not start at x=11: x=%u\n",
				fairy_snes_debug_wram(machine, 0x0352));
			return 196;
		}
		const std::uint64_t start_thumb = blockHash(machine, 11, 69, 8, 8);
		const std::uint64_t end_background = blockHash(machine, 237, 69, 8, 8);

		if (!commitPosition(3, 500, 500) || fairy_snes_debug_wram(machine, 0x0352) != 124) {
			std::fprintf(stderr, "document-position thumb midpoint mismatch: x=%u want=124\n",
				fairy_snes_debug_wram(machine, 0x0352));
			return 197;
		}
		const std::uint64_t start_background = blockHash(machine, 11, 69, 8, 8);
		const std::uint64_t middle_thumb = blockHash(machine, 124, 69, 8, 8);
		if (start_thumb == start_background) {
			std::fputs("document-position thumb moved in WRAM but not in the framebuffer at the track start\n", stderr);
			return 198;
		}

		if (!commitPosition(4, 1000, 0) || fairy_snes_debug_wram(machine, 0x0352) != 237) {
			std::fprintf(stderr, "document-position thumb did not reach x=237: x=%u\n",
				fairy_snes_debug_wram(machine, 0x0352));
			return 199;
		}
		const std::uint64_t middle_background = blockHash(machine, 124, 69, 8, 8);
		const std::uint64_t end_thumb = blockHash(machine, 237, 69, 8, 8);
		if (middle_thumb == middle_background || end_background == end_thumb) {
			std::fputs("document-position thumb OAM did not move visibly across the track\n", stderr);
			return 200;
		}
		fairy_snes_destroy(machine);
	}

	// Persistence settings and destructive transitions are cartridge-owned.
	// Settings events populate the full byte ranges, F3 edits them through a
	// typed command, Recovery History is a real paged cartridge browser with
	// opaque tokens/status text, and the shared dirty-transition dialog emits
	// one explicit Checkpoint/Save/Discard/Cancel decision.
	{
		machine = fairy_snes_create(rom.data(), rom.size());
		if (!machine || !runFrames(machine, 3)) return 232;
		std::vector<std::uint8_t> settings_wire;
		appendRecord(settings_wire, 0x8212, {1, 255, 0, 0, 4});
		for (std::size_t i = 0; i < settings_wire.size(); ++i) {
			fairy_snes_debug_bus_write(machine,
				0x702100 + static_cast<std::uint32_t>(i), settings_wire[i]);
		}
		fairy_snes_debug_bus_write(machine, 0x700006,
			static_cast<std::uint8_t>(settings_wire.size()));
		fairy_snes_debug_bus_write(machine, 0x700007,
			static_cast<std::uint8_t>(settings_wire.size() >> 8));
		if (!runFrames(machine, 3)
			|| fairy_snes_debug_wram(machine, 0x031b) != 1
			|| fairy_snes_debug_wram(machine, 0x031c) != 255
			|| fairy_snes_debug_wram(machine, 0x032b) != 0
			|| fairy_snes_debug_wram(machine, 0x0369) != 4) return 233;
		if (!fairy_snes_key_event(machine, 0x04, true, false)
			|| !runFrames(machine, 2)
			|| fairy_snes_debug_wram(machine, 0x031d) != 0x10
			|| !wramEquals(machine, 0x1007, "SAVE AND RECOVERY")
			|| !wramEquals(machine, 0x10a0, "RENDERED")) {
			std::fputs("F3 did not render the committed persistence settings\n", stderr);
			return 234;
		}
		if (!fairy_snes_key_event(machine, 0x74, true, true)
			|| !runFrames(machine, 2)
			|| fairy_snes_debug_bus_read(machine, 0x700102) != 0x12
			|| fairy_snes_debug_bus_read(machine, 0x700103) != 0x01
			|| fairy_snes_debug_bus_read(machine, 0x700104) != 3
			|| fairy_snes_debug_bus_read(machine, 0x700114) != 0
			|| fairy_snes_debug_bus_read(machine, 0x700115) != 255
			|| fairy_snes_debug_bus_read(machine, 0x700116) != 0) {
			std::fputs("settings change did not emit one complete typed value\n", stderr);
			return 235;
		}
		for (int row = 0; row < 3; ++row) {
			if (!fairy_snes_key_event(machine, 0x72, true, true)
				|| !runFrames(machine, 2)) return 236;
		}
		if (!fairy_snes_key_event(machine, 0x5a, true, false)
			|| !runFrames(machine, 2)
			|| fairy_snes_debug_wram(machine, 0x031d) != 4
			|| fairy_snes_debug_bus_read(machine, 0x700100 + 23 + 2) != 0x08
			|| fairy_snes_debug_bus_read(machine, 0x700100 + 23 + 3) != 0x01) {
			std::fputs("Recovery History did not issue its paged list command\n", stderr);
			return 236;
		}
		constexpr std::string_view recovery_id = "a1b2c3d4";
		constexpr std::string_view recovery_name =
			"07-29 12:34 STORY UNSAVED";
		std::vector<std::uint8_t> recovery_payload;
		recovery_payload.push_back(recovery_id.size());
		append16(recovery_payload, recovery_name.size());
		recovery_payload.push_back(2);
		append64(recovery_payload, 4096);
		append64(recovery_payload, 1234567);
		recovery_payload.insert(recovery_payload.end(),
			recovery_id.cbegin(), recovery_id.cend());
		recovery_payload.insert(recovery_payload.end(),
			recovery_name.cbegin(), recovery_name.cend());
		std::vector<std::uint8_t> recovery_page;
		appendRecord(recovery_page, 0x8200, recovery_payload);
		std::vector<std::uint8_t> recovery_complete;
		append32(recovery_complete, 1);
		append32(recovery_complete, 0);
		recovery_complete.push_back(1);
		recovery_complete.push_back(4);
		recovery_complete.push_back(0);
		appendRecord(recovery_page, 0x820f, recovery_complete);
		for (std::size_t i = 0; i < recovery_page.size(); ++i) {
			fairy_snes_debug_bus_write(machine,
				0x702100 + static_cast<std::uint32_t>(settings_wire.size() + i),
				recovery_page[i]);
		}
		const std::size_t recovery_event_producer =
			settings_wire.size() + recovery_page.size();
		fairy_snes_debug_bus_write(machine, 0x700006,
			static_cast<std::uint8_t>(recovery_event_producer));
		fairy_snes_debug_bus_write(machine, 0x700007,
			static_cast<std::uint8_t>(recovery_event_producer >> 8));
		if (!runFrames(machine, 3)
			|| fairy_snes_debug_wram(machine, 0x031d) != 7
			|| !wramEquals(machine, 0x1007, "RECOVERY HISTORY")
			|| !wramEquals(machine, 0x1000 + 32, recovery_name)
			|| !wramEquals(machine, 0x1600, recovery_id)) {
			std::fputs("Recovery History page did not render its status row\n", stderr);
			return 236;
		}
		if (!fairy_snes_key_event(machine, 0x5a, true, false)
			|| !runFrames(machine, 2)
			|| fairy_snes_debug_wram(machine, 0x031d) != 0
			|| fairy_snes_debug_bus_read(machine, 0x700100 + 43 + 2) != 0x09
			|| fairy_snes_debug_bus_read(machine, 0x700100 + 43 + 3) != 0x01
			|| !wramEquals(machine, 0x1800, recovery_id)) {
			std::fputs("Recovery History selection did not emit its opaque restore token\n",
				stderr);
			return 236;
		}
		const std::size_t decision_offset =
			fairy_snes_debug_bus_read(machine, 0x700002)
			| (std::size_t(fairy_snes_debug_bus_read(machine, 0x700003)) << 8);
		std::vector<std::uint8_t> transition_wire;
		appendRecord(transition_wire, 0x8213, {});
		for (std::size_t i = 0; i < transition_wire.size(); ++i) {
			fairy_snes_debug_bus_write(machine,
				0x702100 + static_cast<std::uint32_t>(
					recovery_event_producer + i),
				transition_wire[i]);
		}
		const std::size_t event_producer =
			recovery_event_producer + transition_wire.size();
		fairy_snes_debug_bus_write(machine, 0x700006,
			static_cast<std::uint8_t>(event_producer));
		fairy_snes_debug_bus_write(machine, 0x700007,
			static_cast<std::uint8_t>(event_producer >> 8));
		if (!runFrames(machine, 3)
			|| fairy_snes_debug_wram(machine, 0x031d) != 0x12
			|| !wramEquals(machine, 0x1008, "UNSAVED CHANGES")) {
			std::fputs("dirty transition event did not open the shared cartridge dialog\n", stderr);
			return 236;
		}
		if (!fairy_snes_key_event(machine, 0x72, true, true)
			|| !runFrames(machine, 2)
			|| !fairy_snes_key_event(machine, 0x72, true, true)
			|| !runFrames(machine, 2)
			|| !fairy_snes_key_event(machine, 0x5a, true, false)
			|| !runFrames(machine, 2)) return 237;
		const std::uint32_t decision_record =
			0x700100 + static_cast<std::uint32_t>(decision_offset);
		if (fairy_snes_debug_bus_read(machine, decision_record + 2) != 0x14
			|| fairy_snes_debug_bus_read(machine, decision_record + 3) != 0x01
			|| fairy_snes_debug_bus_read(machine, decision_record + 4) != 1
			|| fairy_snes_debug_bus_read(machine, decision_record + 20) != 2) {
			std::fputs("dirty transition did not emit the selected Discard decision\n", stderr);
			return 238;
		}
		fairy_snes_destroy(machine);
	}

	// --- Typing blip: SPC700 driver upload, key-on, and the settings path ---
	{
		FairySnesMachine* audio = fairy_snes_create(rom.data(), rom.size());
		if (!audio) return 239;

		// Peak absolute sample over `frames`, draining as it goes. Returns -1 if
		// the machine stalls, which is itself a failure worth distinguishing.
		const auto peakOver = [](FairySnesMachine* m, int frames) {
			std::vector<std::int16_t> block(800 * 2);
			int peak = 0;
			for (int i = 0; i < frames; ++i) {
				if (!fairy_snes_run_frame(m)) return -1;
				while (fairy_snes_audio_blocks(m) > 0) {
					if (!fairy_snes_audio_read(m, block.data(), 800)) break;
					for (const std::int16_t s : block) {
						const int a = s < 0 ? -s : s;
						if (a > peak) peak = a;
					}
				}
			}
			return peak;
		};

		if (!runFrames(audio, 20)) return 240;
		// The upload is bounded, so a failure here is a clear flag rather than a
		// hang -- which is exactly why this can be asserted at all.
		if (fairy_snes_debug_wram(audio, 0x0371) != 1) {
			std::fputs("SPC700 driver upload did not complete; audio flag $0371 is clear\n", stderr);
			return 241;
		}
		// The driver installs its DSP init table before accepting any command,
		// so these registers reading back as configured is proof it is running
		// on the other processor, not merely that bytes were copied.
		// FLG must leave bit 5 clear (echo writes permitted), and bits 6-7 clear
		// (not muted, not in reset).
		if (fairy_snes_debug_dsp_reg(audio, 0x5d) != 0x02
			|| fairy_snes_debug_dsp_reg(audio, 0x05) != 0xdf
			|| fairy_snes_debug_dsp_reg(audio, 0x06) != 0x7e
			|| fairy_snes_debug_dsp_reg(audio, 0x6c) != 0x00) {
			std::fprintf(stderr, "DSP init table not installed: DIR=%02x ADSR1=%02x ADSR2=%02x FLG=%02x\n",
				fairy_snes_debug_dsp_reg(audio, 0x5d), fairy_snes_debug_dsp_reg(audio, 0x05),
				fairy_snes_debug_dsp_reg(audio, 0x06), fairy_snes_debug_dsp_reg(audio, 0x6c));
			return 242;
		}
		// The echo unit is live, so its buffer address and FIR must be real. ESA
		// pointing at the driver image would have the DSP overwrite the program
		// it is running, which is silent until it is catastrophic.
		if (fairy_snes_debug_dsp_reg(audio, 0x6d) != 0x10
			|| fairy_snes_debug_dsp_reg(audio, 0x0f) == 0
			|| (fairy_snes_debug_dsp_reg(audio, 0x4d) & 0x01) == 0) {
			std::fprintf(stderr, "echo unit not configured: ESA=%02x FIR C0=%02x EON=%02x\n",
				fairy_snes_debug_dsp_reg(audio, 0x6d), fairy_snes_debug_dsp_reg(audio, 0x0f),
				fairy_snes_debug_dsp_reg(audio, 0x4d));
			return 259;
		}
		// Directory entry 0 must point at a block whose header loops, or the
		// oscillator would stop after sixteen samples.
		const std::uint16_t source0 = static_cast<std::uint16_t>(
			fairy_snes_debug_aram(audio, 0x0200) | (fairy_snes_debug_aram(audio, 0x0201) << 8));
		if ((fairy_snes_debug_aram(audio, source0) & 0x03) != 0x03) {
			std::fputs("BRR source 0 does not carry both loop and end flags\n", stderr);
			return 243;
		}

		// Nothing has been typed, so nothing may be audible. This is the
		// regression guard for the boot-time key-on the port residue once caused.
		if (const int idle = peakOver(audio, 20); idle != 0) {
			std::fprintf(stderr, "cartridge is not silent before any keystroke: peak %d\n", idle);
			return 244;
		}

		if (!fairy_snes_key_event(audio, scanForAscii('a'), true, false) || !runFrames(audio, 2)) return 245;
		fairy_snes_key_event(audio, scanForAscii('a'), false, false);
		if (const int blip = peakOver(audio, 30); blip <= 0) {
			std::fprintf(stderr, "typing produced no audio: peak %d\n", blip);
			return 246;
		}

		// Turning the blip off has to actually silence it, not just stop a
		// visual. Let the previous note finish releasing first.
		fairy_snes_debug_bus_write(audio, 0x7e0372, 0);
		if (peakOver(audio, 60) < 0) return 247;
		if (!fairy_snes_key_event(audio, scanForAscii('b'), true, false) || !runFrames(audio, 2)) return 248;
		fairy_snes_key_event(audio, scanForAscii('b'), false, false);
		if (const int muted = peakOver(audio, 30); muted != 0) {
			std::fprintf(stderr, "typing still audible with blips disabled: peak %d\n", muted);
			return 249;
		}

		// The driver's other command is "write this DSP register", which is the
		// whole mechanism the sound settings will ride on. Drive it the way the
		// cartridge does: payload first, then a changed sequence in port 0.
		fairy_snes_debug_bus_write(audio, 0x002141, 1);    // non-zero = set register
		fairy_snes_debug_bus_write(audio, 0x002142, 0x06); // V0ADSR2
		fairy_snes_debug_bus_write(audio, 0x002143, 0x7f);
		fairy_snes_debug_bus_write(audio, 0x002140,
			static_cast<std::uint8_t>(fairy_snes_debug_wram(audio, 0x0370) + 0x40));
		if (!runFrames(audio, 4)) return 250;
		if (fairy_snes_debug_dsp_reg(audio, 0x06) != 0x7f) {
			std::fprintf(stderr, "driver ignored a set-register command: ADSR2=%02x\n",
				fairy_snes_debug_dsp_reg(audio, 0x06));
			return 251;
		}

		// A host sound-settings event has to travel the whole way: mailbox to
		// cartridge state to nine handshaked DSP register writes. Pick values
		// distinct from the defaults in every field so nothing passes by
		// coincidence.
		std::vector<std::uint8_t> sound_events;
		appendRecord(sound_events, 0x8214, {
			1,    // typing blips on
			1,    // waveform: triangle
			0x0a, // attack
			0x05, // decay
			0x03, // sustain level
			0x11, // sustain rate
			0x07, // release
			0x2a, // pitch
			0x55, // volume
			0x33, // echo volume
			0x09, // echo delay
			0x22  // echo feedback
		});
		for (std::size_t i = 0; i < sound_events.size(); ++i) {
			fairy_snes_debug_bus_write(audio, 0x702100 + static_cast<std::uint32_t>(i), sound_events[i]);
		}
		fairy_snes_debug_bus_write(audio, 0x700006, static_cast<std::uint8_t>(sound_events.size()));
		fairy_snes_debug_bus_write(audio, 0x700007, static_cast<std::uint8_t>(sound_events.size() >> 8));
		if (!runFrames(audio, 6)) return 252;
		if (fairy_snes_debug_bus_read(audio, 0x700008) != sound_events.size()) {
			std::fputs("cartridge did not consume the sound-settings event\n", stderr);
			return 253;
		}
		// ADSR1 = 1 DDD AAAA = $80 | 5<<4 | 10; ADSR2 = LLL RRRRR = 3<<5 | 17.
		const std::uint8_t want_adsr1 = 0x80 | (5 << 4) | 0x0a;
		const std::uint8_t want_adsr2 = static_cast<std::uint8_t>((3 << 5) | 0x11);
		const std::uint8_t want_gain = 0xa0 | 0x07;
		if (fairy_snes_debug_dsp_reg(audio, 0x05) != want_adsr1
			|| fairy_snes_debug_dsp_reg(audio, 0x06) != want_adsr2
			|| fairy_snes_debug_dsp_reg(audio, 0x07) != want_gain
			|| fairy_snes_debug_dsp_reg(audio, 0x03) != 0x2a
			|| fairy_snes_debug_dsp_reg(audio, 0x00) != 0x55
			|| fairy_snes_debug_dsp_reg(audio, 0x01) != 0x55
			|| fairy_snes_debug_dsp_reg(audio, 0x04) != 1
			|| fairy_snes_debug_dsp_reg(audio, 0x2c) != 0x33
			|| fairy_snes_debug_dsp_reg(audio, 0x3c) != 0x33
			|| fairy_snes_debug_dsp_reg(audio, 0x7d) != 0x09
			|| fairy_snes_debug_dsp_reg(audio, 0x0d) != 0x22) {
			std::fprintf(stderr,
				"sound settings did not reach the DSP: ADSR1=%02x/%02x ADSR2=%02x/%02x "
				"GAIN=%02x/%02x PITCHH=%02x VOLL=%02x SRCN=%02x\n",
				fairy_snes_debug_dsp_reg(audio, 0x05), want_adsr1,
				fairy_snes_debug_dsp_reg(audio, 0x06), want_adsr2,
				fairy_snes_debug_dsp_reg(audio, 0x07), want_gain,
				fairy_snes_debug_dsp_reg(audio, 0x03),
				fairy_snes_debug_dsp_reg(audio, 0x00),
				fairy_snes_debug_dsp_reg(audio, 0x04));
			std::fprintf(stderr, "  echo: EVOLL=%02x EVOLR=%02x EDL=%02x EFB=%02x\n",
				fairy_snes_debug_dsp_reg(audio, 0x2c), fairy_snes_debug_dsp_reg(audio, 0x3c),
				fairy_snes_debug_dsp_reg(audio, 0x7d), fairy_snes_debug_dsp_reg(audio, 0x0d));
			return 254;
		}
		// The event re-enabled blips, so typing must be audible again -- this is
		// what proves byte 0 is wired to the switch and not merely stored.
		if (!fairy_snes_key_event(audio, scanForAscii('c'), true, false) || !runFrames(audio, 2)) return 255;
		fairy_snes_key_event(audio, scanForAscii('c'), false, false);
		if (const int again = peakOver(audio, 30); again <= 0) {
			std::fprintf(stderr, "settings event did not re-enable the blip: peak %d\n", again);
			return 256;
		}

		// Waveform 2 is the hardware noise source, which is a different signal
		// path inside the DSP rather than another sample.
		std::vector<std::uint8_t> noise_event;
		appendRecord(noise_event, 0x8214, {1, 2, 0x0f, 0x04, 0, 24, 20, 0x10, 0x60, 0, 2, 0});
		const std::uint32_t noise_base = 0x702100 + static_cast<std::uint32_t>(sound_events.size());
		for (std::size_t i = 0; i < noise_event.size(); ++i) {
			fairy_snes_debug_bus_write(audio, noise_base + static_cast<std::uint32_t>(i), noise_event[i]);
		}
		const std::size_t total = sound_events.size() + noise_event.size();
		fairy_snes_debug_bus_write(audio, 0x700006, static_cast<std::uint8_t>(total));
		fairy_snes_debug_bus_write(audio, 0x700007, static_cast<std::uint8_t>(total >> 8));
		if (!runFrames(audio, 6)) return 257;
		if ((fairy_snes_debug_dsp_reg(audio, 0x3d) & 0x01) == 0) {
			std::fprintf(stderr, "noise waveform did not set NON bit 0: NON=%02x\n",
				fairy_snes_debug_dsp_reg(audio, 0x3d));
			return 258;
		}
		fairy_snes_destroy(audio);
	}

	// --- SPC700 shaper: audition, capture, and the two live scopes ----------
	{
		FairySnesMachine* shaper = fairy_snes_create(rom.data(), rom.size());
		if (!shaper) return 260;
		if (!runFrames(shaper, 20)) return 261;
		// F5 is PS/2 set-2 scancode 0x03.
		if (!fairy_snes_key_event(shaper, 0x03, true, false) || !runFrames(shaper, 4)) return 262;
		fairy_snes_key_event(shaper, 0x03, false, false);
		if (!runFrames(shaper, 3)) return 263;
		if (fairy_snes_debug_wram(shaper, 0x031d) != 0x13) {
			std::fputs("F5 did not open the sound shaper\n", stderr);
			return 264;
		}
		// Space auditions. That one note both plays and refills the capture the
		// scopes are drawn from.
		if (!fairy_snes_key_event(shaper, 0x29, true, false) || !runFrames(shaper, 3)) return 265;
		fairy_snes_key_event(shaper, 0x29, false, false);
		if (!runFrames(shaper, 24)) return 266;

		// The capture must have reached WRAM. Reading straight from the SPC's
		// own ARAM would prove the driver works but not that the cartridge ever
		// streamed it, which is the half that can silently do nothing.
		int wave_nonzero = 0, env_nonzero = 0;
		for (int i = 0; i < 30; ++i) {
			if (fairy_snes_debug_wram(shaper, 0x1d00 + i)) ++wave_nonzero;
			if (fairy_snes_debug_wram(shaper, 0x1d20 + i)) ++env_nonzero;
		}
		if (wave_nonzero < 8 || env_nonzero < 4) {
			std::fprintf(stderr, "capture did not reach WRAM: %d waveform, %d envelope samples\n",
				wave_nonzero, env_nonzero);
			return 267;
		}
		// The envelope must actually decay -- a flat or rising trace would mean
		// we are plotting something other than the live ENVX register.
		const int first = fairy_snes_debug_wram(shaper, 0x1d20);
		const int later = fairy_snes_debug_wram(shaper, 0x1d20 + 6);
		if (first == 0 || later >= first) {
			std::fprintf(stderr, "envelope trace does not decay: [0]=%d [6]=%d\n", first, later);
			return 268;
		}
		// And both scopes must be drawn: their rows carry the bar tiles, which
		// are control codes 1-8 and appear nowhere else on the plane.
		const auto barCells = [&](int row) {
			int count = 0;
			for (int col = 0; col < 30; ++col) {
				const std::uint8_t cell =
					fairy_snes_debug_wram(shaper, 0x1000 + row * 30 + col);
				if (cell >= 1 && cell <= 8) ++count;
			}
			return count;
		};
		if (barCells(13) + barCells(14) < 8) {
			std::fputs("waveform scope drew no bars\n", stderr);
			return 269;
		}
		if (barCells(15) + barCells(16) < 4) {
			std::fputs("envelope scope drew no bars\n", stderr);
			return 270;
		}
		// A slider must reflect its value: ATTACK is at its maximum, so every
		// cell of its bar is lit.
		for (int col = 12; col < 24; ++col) {
			if (fairy_snes_debug_wram(shaper, 0x1000 + 3 * 30 + col) != '#') {
				std::fprintf(stderr, "ATTACK slider cell %d is not filled at maximum\n", col);
				return 271;
			}
		}
		// A slider is draggable. The renderer's fill thresholds and the
		// pointer's value table are computed from the same formula in two
		// different places, so a click must land the bar exactly where it was
		// pressed -- and the value must reach the DSP, not just the screen.
		{
			// Pointer starts at (128,112); VOLUME is field 8, row 9, y=151.
			// Slider cell 2 sits at x=116.
			int dx = 116 - 128, dy = 151 - 112;
			while (dx || dy) {
				const int sx = dx > 0 ? (dx > 100 ? 100 : dx) : (dx < -100 ? -100 : dx);
				const int sy = dy > 0 ? (dy > 100 ? 100 : dy) : (dy < -100 ? -100 : dy);
				fairy_snes_mouse_event(shaper, static_cast<std::int8_t>(sx),
					static_cast<std::int8_t>(sy), false, false);
				dx -= sx; dy -= sy;
				if (!runFrames(shaper, 1)) return 272;
			}
			fairy_snes_mouse_event(shaper, 0, 0, true, false);
			if (!runFrames(shaper, 6)) return 273;
			fairy_snes_mouse_event(shaper, 0, 0, false, false);
			if (!runFrames(shaper, 3)) return 274;
			// 12 slider cells over a maximum of 127: cell index 2 is 32.
			if (fairy_snes_debug_wram(shaper, 0x037a) != 32
				|| fairy_snes_debug_wram(shaper, 0x037e) != 8) {
				std::fprintf(stderr, "slider click set volume %u selection %u, want 32 and 8\n",
					fairy_snes_debug_wram(shaper, 0x037a),
					fairy_snes_debug_wram(shaper, 0x037e));
				return 275;
			}
			if (fairy_snes_debug_dsp_reg(shaper, 0x00) != 32) {
				std::fprintf(stderr, "slider click did not reach the DSP: VOLL=%02x\n",
					fairy_snes_debug_dsp_reg(shaper, 0x00));
				return 276;
			}
			// Three bars lit, nine not -- the bar landed where it was clicked.
			for (int col = 12; col < 24; ++col) {
				const std::uint8_t want = col < 15 ? '#' : '-';
				if (fairy_snes_debug_wram(shaper, 0x1000 + 9 * 30 + col) != want) {
					std::fprintf(stderr, "volume bar cell %d is not '%c' after the click\n",
						col, want);
					return 277;
				}
			}
		}
		// Walk every field and check the selection highlight covers exactly its
		// own row. The first eight rows have offsets that fit in a byte, so a
		// row-offset computation that wrapped at 255 looked perfectly correct until the
		// ninth field -- which is how it shipped. Only a full walk catches it.
		{
			// Back to the top of the list first.
			for (int i = 0; i < 12; ++i) {
				if (!fairy_snes_key_event(shaper, 0x75, true, true)) return 278; // Up
                if (!runFrames(shaper, 2)) return 278;
				fairy_snes_key_event(shaper, 0x75, false, true);
				if (!runFrames(shaper, 1)) return 278;
			}
			for (int selection = 0; selection < 12; ++selection) {
				int lit = 0, first = -1;
				for (int row = 0; row < 17; ++row) {
					for (int col = 0; col < 30; ++col) {
						if (fairy_snes_debug_wram(shaper, 0x1200 + row * 30 + col) == 4) {
							if (first < 0) first = row;
							++lit;
						}
					}
				}
				if (first != selection + 1 || lit != 30) {
					std::fprintf(stderr,
						"selection %d highlights row %d with %d cells, want row %d with 30\n",
						selection, first, lit, selection + 1);
					return 279;
				}
				if (!fairy_snes_key_event(shaper, 0x72, true, true)) return 280; // Down
				if (!runFrames(shaper, 2)) return 280;
				fairy_snes_key_event(shaper, 0x72, false, true);
				if (!runFrames(shaper, 1)) return 280;
			}
		}
		fairy_snes_destroy(shaper);
	}
	return 0;
}
