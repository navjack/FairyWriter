#ifndef FAIRYWRITER_MAILBOX_H
#define FAIRYWRITER_MAILBOX_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace FairyWriter {

struct MailboxLayout final {
	static constexpr std::size_t TotalBytes = 32 * 1024;
	static constexpr std::size_t HeaderOffset = 0x0000;
	static constexpr std::size_t CommandOffset = 0x0100;
	static constexpr std::size_t CommandBytes = 0x2000;
	static constexpr std::size_t EventOffset = 0x2100;
	static constexpr std::size_t EventBytes = 0x2000;
	static constexpr std::size_t ViewportOffset = 0x4100;
	static constexpr std::size_t ViewportSlotBytes = 0x1800;
	static constexpr std::size_t ViewportHeaderBytes = 0x0200;
	static constexpr std::size_t ViewportTitleBytes = 54;
	static constexpr std::size_t CartridgeWorkingBytes = 510;
	static constexpr std::size_t GlyphOffset = 0x7100;
	static constexpr std::size_t GlyphBytes = 0x0f00;
};

static_assert(MailboxLayout::GlyphOffset + MailboxLayout::GlyphBytes == MailboxLayout::TotalBytes,
	"mailbox regions must cover exactly 32 KiB");
static constexpr std::uint16_t MailboxProtocol = 1;

struct MailboxRecord {
	std::uint16_t protocol = 1;
	std::uint16_t kind = 0;
	std::uint16_t flags = 0;
	std::uint32_t sequence = 0;
	std::uint64_t revision = 0;
	std::vector<std::uint8_t> payload;
};

struct FormatRunWire final {
	std::uint16_t offset = 0;
	std::uint16_t length = 0;
	std::uint8_t flags = 0;
	std::uint8_t heading_level = 0;
	std::uint8_t alignment = 0;
	std::uint8_t reserved = 0;
};
enum FormatRunFlags : std::uint8_t {
	FormatRunBold = 1 << 0,
	FormatRunItalic = 1 << 1,
	FormatRunUnderline = 1 << 2,
	FormatRunSpellingIssue = 1 << 3,
	FormatRunGrammarIssue = 1 << 4
};

struct ViewportSnapshot {
	std::uint64_t revision = 0;
	std::uint32_t cursor = 0;
	std::uint32_t selection_start = 0;
	std::uint32_t selection_end = 0;
	std::uint32_t text_offset = 0;
	std::uint32_t character_count = 0;
	std::uint32_t word_count = 0;
	std::uint16_t chapter = 1;
	// bit 0: dirty; bit 1: read-only; bit 2: at start; bit 3: at end.
	std::uint8_t status_flags = 0;
	std::uint32_t line_count = 1;
	std::uint32_t paragraph_count = 1;
	std::uint32_t page_count = 1;
	std::uint32_t current_page = 1;
	std::uint32_t total_document_bytes = 0;
	std::uint32_t bytes_before = 0;
	std::uint32_t bytes_after = 0;
	std::vector<std::uint8_t> display_title;
	std::vector<std::uint16_t> grapheme_offsets;
	std::vector<std::uint16_t> line_break_offsets;
	std::vector<FormatRunWire> format_runs;
	std::vector<std::uint8_t> utf8;
};

struct GlyphTile final {
	std::uint16_t request_id = 0;
	std::uint8_t font_role = 0;
	std::uint8_t style = 0;
	std::uint8_t width_tiles = 1;
	std::uint8_t flags = 0;
	std::vector<std::uint8_t> pixels;
};

// A glyph response is a deterministic 4bpp tile payload. One response may
// occupy one or two adjacent 8x8 tiles; the fixed wire size keeps SRAM
// admission constant and makes malformed responses rejectable before cache
// mutation.
bool encodeGlyphTile(const GlyphTile& glyph, std::vector<std::uint8_t>& wire);
bool decodeGlyphTile(const std::uint8_t* wire, std::size_t wire_size, GlyphTile& glyph);

class GlyphRing final {
public:
	static constexpr std::size_t WireBytes = 72;
	static constexpr std::size_t Capacity = MailboxLayout::GlyphBytes / WireBytes;
	GlyphRing() = default;
	bool push(const GlyphTile& glyph);
	bool pop(GlyphTile& glyph);
	std::size_t used() const noexcept { return m_used; }
	std::size_t freeSpace() const noexcept { return Capacity - m_used; }
	void clear() noexcept { m_read = m_write = m_used = 0; }

private:
	GlyphTile m_slots[Capacity];
	std::size_t m_read = 0;
	std::size_t m_write = 0;
	std::size_t m_used = 0;
};

// Slot format: revision(8), cursor(4), selection start/end(4+4), text offset(4),
// character/word counts(4+4), text byte count(2), chapter(2), title byte
// count(1), 54 resident title bytes, status flags(1), line count(4), then UTF-8
// document bytes. The caller swaps slots only after the complete buffer has
// been written.
bool encodeViewport(const ViewportSnapshot& snapshot, std::vector<std::uint8_t>& slot);

class ViewportSlots final {
public:
	ViewportSlots();
	bool publish(const ViewportSnapshot& snapshot);
	const std::vector<std::uint8_t>& active() const noexcept { return m_slots[m_active]; }
	std::uint8_t activeIndex() const noexcept { return m_active; }

private:
	std::vector<std::uint8_t> m_slots[2];
	std::uint8_t m_active = 0;
};

// A single-producer/single-consumer byte ring. The indices are deliberately
// private: a record becomes visible only when its complete payload is copied.
class MailboxRing final {
public:
	// The record header, and the widest payload the wire can describe. A
	// record's payload length is a 16-bit field, so 0xffff is a property of the
	// format rather than a policy choice; a ring that must hold one complete
	// maximum record therefore needs HeaderSize + MaxRecordPayload + 1 bytes,
	// the trailing byte being the never-used one that keeps equal indices
	// meaning empty (see push()).
	static constexpr std::size_t HeaderSize = 20;
	static constexpr std::size_t MaxRecordPayload = 0xffff;
	static constexpr std::size_t OneRecordCapacity = HeaderSize + MaxRecordPayload + 1;

	explicit MailboxRing(std::size_t capacity);
	bool push(const MailboxRecord& record);
	bool pop(MailboxRecord& record);
	std::size_t used() const noexcept;
	std::size_t freeSpace() const noexcept;
	void clear() noexcept;
	bool consumeTo(std::size_t read);
	bool importRaw(const std::uint8_t* bytes, std::size_t read, std::size_t write);
	bool exportRaw(std::uint8_t* bytes, std::size_t bytes_size, std::size_t& read, std::size_t& write) const;

private:
	std::vector<std::uint8_t> m_storage;
	std::size_t m_read = 0;
	std::size_t m_write = 0;
	std::size_t m_used = 0;
	std::uint8_t readByte(std::size_t offset) const noexcept;
	void writeByte(std::size_t offset, std::uint8_t value) noexcept;
	std::uint16_t read16(std::size_t offset) const noexcept;
	std::uint32_t read32(std::size_t offset) const noexcept;
	std::uint64_t read64(std::size_t offset) const noexcept;
	void write16(std::size_t offset, std::uint16_t value) noexcept;
	void write32(std::size_t offset, std::uint32_t value) noexcept;
	void write64(std::size_t offset, std::uint64_t value) noexcept;
};

// Mutating commands are accepted only against the revision that produced
// their viewport. A mismatch is a conflict, never an implicit merge.
inline bool mailboxRevisionMatches(const MailboxRecord& record, std::uint64_t current) noexcept {
	return record.revision == current;
}

} // namespace FairyWriter

#endif
