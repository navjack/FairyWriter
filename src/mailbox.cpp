#include "mailbox.h"
#include <algorithm>

namespace FairyWriter {

namespace {
void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) { b[o]=v; b[o+1]=v>>8; }
void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) { put16(b,o,v); put16(b,o+2,v>>16); }
void put64(std::vector<std::uint8_t>& b, std::size_t o, std::uint64_t v) { put32(b,o,v); put32(b,o+4,v>>32); }
}

bool encodeViewport(const ViewportSnapshot& s, std::vector<std::uint8_t>& slot) {
	if (slot.size() != MailboxLayout::ViewportSlotBytes
		|| s.display_title.size() > MailboxLayout::ViewportTitleBytes
		|| s.utf8.size() > slot.size() - MailboxLayout::ViewportHeaderBytes
		|| s.utf8.size() > 0xffff) return false;
	std::fill(slot.begin(), slot.end(), 0);
	put64(slot, 0, s.revision);
	put32(slot, 8, s.cursor);
	put32(slot, 12, s.selection_start);
	put32(slot, 16, s.selection_end);
	put32(slot, 20, s.text_offset);
	put32(slot, 24, s.character_count);
	put32(slot, 28, s.word_count);
	put16(slot, 32, static_cast<std::uint16_t>(s.utf8.size()));
	put16(slot, 34, s.chapter);
	slot[36] = static_cast<std::uint8_t>(s.display_title.size());
	std::copy(s.display_title.begin(), s.display_title.end(), slot.begin() + 37);
	slot[91] = s.status_flags;
	put32(slot, 92, s.line_count);
	put32(slot, 96, s.paragraph_count);
	put32(slot, 100, s.page_count);
	put32(slot, 104, s.current_page);
	put32(slot, 108, s.total_document_bytes);
	put32(slot, 112, s.bytes_before);
	put32(slot, 116, s.bytes_after);

	const std::size_t grapheme_count = std::min<std::size_t>(64, s.grapheme_offsets.size());
	put16(slot, 120, static_cast<std::uint16_t>(grapheme_count));
	const std::size_t line_break_count = std::min<std::size_t>(64, s.line_break_offsets.size());
	put16(slot, 122, static_cast<std::uint16_t>(line_break_count));
	const std::size_t format_run_count = std::min<std::size_t>(16, s.format_runs.size());
	put16(slot, 124, static_cast<std::uint16_t>(format_run_count));
	put16(slot, 126, 0); // reserved

	// Grapheme offsets table at 128 (0x80)
	for (std::size_t i = 0; i < grapheme_count; ++i) {
		put16(slot, 128 + i * 2, s.grapheme_offsets[i]);
	}
	// Line break offsets table at 256 (0x100)
	for (std::size_t i = 0; i < line_break_count; ++i) {
		put16(slot, 256 + i * 2, s.line_break_offsets[i]);
	}
	// Format runs table at 384 (0x180)
	for (std::size_t i = 0; i < format_run_count; ++i) {
		const std::size_t offset = 384 + i * 8;
		const auto& run = s.format_runs[i];
		put16(slot, offset, run.offset);
		put16(slot, offset + 2, run.length);
		slot[offset + 4] = run.flags;
		slot[offset + 5] = run.heading_level;
		slot[offset + 6] = run.alignment;
		slot[offset + 7] = run.reserved;
	}

	std::copy(s.utf8.begin(), s.utf8.end(), slot.begin() + MailboxLayout::ViewportHeaderBytes);
	return true;
}

bool encodeGlyphTile(const GlyphTile& glyph, std::vector<std::uint8_t>& wire) {
	if ((glyph.width_tiles != 1 && glyph.width_tiles != 2) || glyph.pixels.size() != static_cast<std::size_t>(glyph.width_tiles) * 32) return false;
	wire.assign(8 + 64, 0);
	put16(wire, 0, glyph.request_id);
	wire[2] = glyph.font_role;
	wire[3] = glyph.style;
	wire[4] = glyph.width_tiles;
	wire[5] = glyph.flags;
	put16(wire, 6, static_cast<std::uint16_t>(glyph.pixels.size()));
	std::copy(glyph.pixels.cbegin(), glyph.pixels.cend(), wire.begin() + 8);
	return true;
}

bool decodeGlyphTile(const std::uint8_t* wire, std::size_t wire_size, GlyphTile& glyph) {
	if (!wire || wire_size != 72 || (wire[4] != 1 && wire[4] != 2)) return false;
	const std::size_t pixels = static_cast<std::size_t>(wire[4]) * 32;
	if ((static_cast<std::uint16_t>(wire[6]) | (static_cast<std::uint16_t>(wire[7]) << 8)) != pixels) return false;
	GlyphTile decoded;
	decoded.request_id = static_cast<std::uint16_t>(wire[0]) | (static_cast<std::uint16_t>(wire[1]) << 8);
	decoded.font_role = wire[2]; decoded.style = wire[3]; decoded.width_tiles = wire[4]; decoded.flags = wire[5];
	decoded.pixels.assign(wire + 8, wire + 8 + pixels);
	glyph = std::move(decoded);
	return true;
}

bool GlyphRing::push(const GlyphTile& glyph) {
	std::vector<std::uint8_t> wire;
	if (m_used == Capacity || !encodeGlyphTile(glyph, wire)) return false;
	GlyphTile decoded;
	if (!decodeGlyphTile(wire.data(), wire.size(), decoded)) return false;
	m_slots[m_write] = std::move(decoded);
	m_write = (m_write + 1) % Capacity;
	++m_used;
	return true;
}

bool GlyphRing::pop(GlyphTile& glyph) {
	if (m_used == 0) return false;
	glyph = std::move(m_slots[m_read]);
	m_read = (m_read + 1) % Capacity;
	--m_used;
	return true;
}

ViewportSlots::ViewportSlots()
{
	m_slots[0].resize(MailboxLayout::ViewportSlotBytes);
	m_slots[1].resize(MailboxLayout::ViewportSlotBytes);
}

bool ViewportSlots::publish(const ViewportSnapshot& snapshot) {
	const std::uint8_t next = static_cast<std::uint8_t>(m_active ^ 1);
	if (!encodeViewport(snapshot, m_slots[next])) return false;
	m_active = next;
	return true;
}

MailboxRing::MailboxRing(std::size_t capacity) : m_storage(capacity) {}
std::size_t MailboxRing::used() const noexcept { return m_used; }
std::size_t MailboxRing::freeSpace() const noexcept { return m_storage.size() - m_used; }
void MailboxRing::clear() noexcept { m_read = m_write = m_used = 0; }
bool MailboxRing::consumeTo(std::size_t read) {
	if (m_storage.empty() || read >= m_storage.size()) return false;
	if (read == m_read) return true;

	std::size_t cursor = m_read;
	std::size_t remaining = m_used;
	std::size_t consumed = 0;
	while (remaining >= HeaderSize) {
		if (read16(cursor) != MailboxProtocol) return false;
		const std::size_t count = read16(cursor + 4);
		const std::size_t record_size = HeaderSize + count;
		if (record_size > remaining) return false;
		cursor = (cursor + record_size) % m_storage.size();
		remaining -= record_size;
		consumed += record_size;
		if (cursor == read) {
			m_read = cursor;
			m_used -= consumed;
			return true;
		}
	}
	return false;
}
bool MailboxRing::importRaw(const std::uint8_t* bytes, std::size_t read, std::size_t write) {
	if (!bytes || m_storage.empty() || read >= m_storage.size() || write >= m_storage.size()) return false;
	std::copy(bytes, bytes + m_storage.size(), m_storage.begin());
	m_read = read; m_write = write;
	m_used = (write >= read) ? write - read : m_storage.size() - read + write;
	return m_used <= m_storage.size();
}
bool MailboxRing::exportRaw(std::uint8_t* bytes, std::size_t bytes_size, std::size_t& read, std::size_t& write) const {
	if (!bytes || bytes_size < m_storage.size()) return false;
	std::copy(m_storage.begin(), m_storage.end(), bytes);
	read = m_read; write = m_write;
	return true;
}
std::uint8_t MailboxRing::readByte(std::size_t o) const noexcept { return m_storage[o % m_storage.size()]; }
void MailboxRing::writeByte(std::size_t o, std::uint8_t v) noexcept { m_storage[o % m_storage.size()] = v; }
std::uint16_t MailboxRing::read16(std::size_t o) const noexcept { return readByte(o) | (std::uint16_t(readByte(o+1)) << 8); }
std::uint32_t MailboxRing::read32(std::size_t o) const noexcept { return read16(o) | (std::uint32_t(read16(o+2)) << 16); }
std::uint64_t MailboxRing::read64(std::size_t o) const noexcept { return read32(o) | (std::uint64_t(read32(o+4)) << 32); }
void MailboxRing::write16(std::size_t o, std::uint16_t v) noexcept { writeByte(o,v); writeByte(o+1,v>>8); }
void MailboxRing::write32(std::size_t o, std::uint32_t v) noexcept { write16(o,v); write16(o+2,v>>16); }
void MailboxRing::write64(std::size_t o, std::uint64_t v) noexcept { write32(o,v); write32(o+4,v>>32); }

bool MailboxRing::push(const MailboxRecord& r) {
	// Keep one byte unused so equal indices always mean empty. Without this
	// invariant, a completely full exported ring is indistinguishable from an
	// empty ring to the process on the other side of the mailbox.
	if (m_storage.empty() || r.protocol != MailboxProtocol || r.payload.size() > MaxRecordPayload || HeaderSize + r.payload.size() >= freeSpace()) return false;
	const auto n = HeaderSize + r.payload.size();
	write16(m_write, r.protocol); write16(m_write+2, r.kind); write16(m_write+4, static_cast<std::uint16_t>(r.payload.size()));
	write16(m_write+6, r.flags); write32(m_write+8, r.sequence); write64(m_write+12, r.revision);
	for (std::size_t i=0; i<r.payload.size(); ++i) writeByte(m_write+HeaderSize+i, r.payload[i]);
	m_write = (m_write+n) % m_storage.size(); m_used += n; return true;
}

bool MailboxRing::pop(MailboxRecord& r) {
	if (m_used < HeaderSize) return false;
	const auto count = read16(m_read+4); const auto n = HeaderSize + count;
	if (n < HeaderSize || n > m_used) return false;
	if (read16(m_read) != MailboxProtocol) {
		// A committed producer index may expose a corrupt record. Consume its
		// bounded wire extent so the consumer cannot remain permanently stuck,
		// but never expose the record to the bridge or mutate document state.
		m_read = (m_read + n) % m_storage.size();
		m_used -= n;
		return false;
	}
	r.protocol=read16(m_read); r.kind=read16(m_read+2); r.flags=read16(m_read+6); r.sequence=read32(m_read+8); r.revision=read64(m_read+12);
	r.payload.resize(count); for (std::size_t i=0;i<count;++i) r.payload[i]=readByte(m_read+HeaderSize+i);
	m_read=(m_read+n)%m_storage.size(); m_used-=n; return true;
}
} // namespace FairyWriter
