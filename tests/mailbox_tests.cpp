#include "mailbox.h"
#include <iostream>

using FairyWriter::MailboxRecord;
using FairyWriter::MailboxRing;
static int failures = 0;
static void expect(bool value, const char* text) { if (!value) { std::cerr << "FAIL: " << text << '\n'; ++failures; } }

int main() {
	expect(FairyWriter::MailboxLayout::TotalBytes == 32768 && FairyWriter::MailboxLayout::CommandOffset == 0x100 && FairyWriter::MailboxLayout::GlyphBytes == 0xf00, "mailbox uses the planned 32 KiB regions");
	MailboxRing ring(80);
	MailboxRecord in; in.kind=7; in.flags=3; in.sequence=99; in.revision=42; in.payload={0, 1, 0xff, 4};
	expect(ring.push(in), "record fits");
	MailboxRecord out;
	expect(ring.pop(out) && out.kind==7 && out.flags==3 && out.sequence==99 && out.revision==42 && out.payload==in.payload, "record round trips");
	expect(!ring.pop(out), "empty ring rejects pop");
	MailboxRecord large; large.payload.resize(61, 0xaa);
	expect(!ring.push(large), "record larger than available ring is rejected");
	MailboxRecord exactly_full; exactly_full.payload.resize(60, 0xaa);
	expect(!ring.push(exactly_full), "ring reserves one byte so equal indices cannot mean full");
	MailboxRecord a; a.kind=1; a.payload.resize(10, 1);
	MailboxRecord b; b.kind=2; b.payload.resize(10, 2);
	expect(ring.push(a) && ring.push(b), "two records fit");
	expect(ring.pop(out) && out.kind==1, "first record remains ordered");
	MailboxRecord c; c.kind=3; c.payload.resize(10, 3);
	expect(ring.push(c), "wrapped record fits");
	expect(ring.pop(out) && out.kind==2, "second record remains ordered after wrap");
	expect(ring.pop(out) && out.kind==3 && out.payload[0]==3, "wrapped payload is intact");
	// A ring sized OneRecordCapacity must hold one record of the widest payload
	// the 16-bit count field can describe, and nothing wider. The bridge's
	// host-side command ring is sized from this: at MailboxLayout::CommandBytes
	// it silently refused every paste of 8172 bytes or more.
	MailboxRing widest(MailboxRing::OneRecordCapacity);
	MailboxRecord maximum; maximum.kind = 31;
	maximum.payload.resize(MailboxRing::MaxRecordPayload, 0x5a);
	expect(widest.push(maximum), "a ring sized for one record accepts the widest payload the wire can describe");
	expect(widest.pop(out) && out.kind == 31 && out.payload.size() == MailboxRing::MaxRecordPayload
		&& out.payload.front() == 0x5a && out.payload.back() == 0x5a,
		"the widest record round trips intact");
	MailboxRecord past_the_wire; past_the_wire.payload.resize(MailboxRing::MaxRecordPayload + 1, 0x5a);
	expect(!widest.push(past_the_wire), "a payload past the 16-bit count field is rejected, however large the ring");
	MailboxRing exported_events(96);
	MailboxRecord event_a; event_a.kind = 20; event_a.payload.resize(3, 1);
	MailboxRecord event_b; event_b.kind = 21; event_b.payload.resize(5, 2);
	expect(exported_events.push(event_a) && exported_events.push(event_b),
		"host event records fit before consumer acknowledgement");
	const std::size_t after_event_a = 20 + event_a.payload.size();
	expect(!exported_events.consumeTo(after_event_a - 1)
		&& exported_events.used() == 48,
		"consumer acknowledgement must land on a complete record boundary");
	expect(exported_events.consumeTo(after_event_a)
		&& exported_events.used() == 25,
		"consumer acknowledgement retires exactly the acknowledged record");
	expect(exported_events.pop(out) && out.kind == event_b.kind,
		"unacknowledged event remains available after host synchronization");
	in.revision = 41;
	expect(!FairyWriter::mailboxRevisionMatches(in, 42), "stale mutation is rejected by revision");
	expect(FairyWriter::mailboxRevisionMatches(in, 41), "current mutation matches revision");
	std::vector<std::uint8_t> malformed(80, 0);
	// A committed header may arrive before a payload only if the producer
	// violates the protocol; the consumer must not expose or partially apply it.
	malformed[0] = 1; malformed[2] = 9; malformed[4] = 4;
	MailboxRing imported(80);
	expect(imported.importRaw(malformed.data(), 0, 20), "raw ring state imports");
	MailboxRecord rejected; expect(!imported.pop(rejected), "partial payload is rejected without a record");
	malformed[4] = 0; malformed[0] = 2;
	expect(imported.importRaw(malformed.data(), 0, 20) && !imported.pop(rejected), "invalid protocol record is rejected without mutation");
	MailboxRecord valid_after_corruption; valid_after_corruption.kind = 17;
	expect(imported.push(valid_after_corruption) && imported.pop(rejected) && rejected.kind == 17, "consumer recovers after discarding a corrupt committed record");
	malformed[0] = 1; malformed[4] = 0; malformed[2] = 0;
	expect(imported.importRaw(malformed.data(), 0, 20) && imported.pop(rejected) && rejected.kind == 0, "valid zero-payload record remains consumable after rejection");
	std::uint32_t state = 0x9e3779b9u;
	for (int iteration = 0; iteration < 10000; ++iteration) {
		state = state * 1664525u + 1013904223u;
		const std::size_t read_index = state % malformed.size();
		state = state * 1664525u + 1013904223u;
		const std::size_t write_index = state % malformed.size();
		for (std::size_t i = 0; i < malformed.size(); ++i) {
			state = state * 1664525u + 1013904223u;
			malformed[i] = static_cast<std::uint8_t>(state >> 24);
		}
		// Raw consumers must tolerate arbitrary partial and malformed states.
		expect(imported.importRaw(malformed.data(), read_index, write_index), "fuzz raw indices remain bounded");
		imported.pop(rejected);
	}
	FairyWriter::ViewportSnapshot viewport;
	viewport.revision = 9;
	viewport.cursor = 12;
	viewport.selection_start = 4;
	viewport.selection_end = 12;
	viewport.chapter = 7;
	viewport.status_flags = 3;
	viewport.line_count = 0x0102;
	viewport.paragraph_count = 5;
	viewport.page_count = 2;
	viewport.current_page = 1;
	viewport.total_document_bytes = 100;
	viewport.bytes_before = 10;
	viewport.bytes_after = 20;
	viewport.display_title = {'S','T','O','R','Y'};
	viewport.grapheme_offsets = {0, 2, 3, 4};
	viewport.line_break_offsets = {3};
	FairyWriter::FormatRunWire run;
	run.offset = 0; run.length = 2; run.flags = 1; run.heading_level = 0; run.alignment = 0;
	viewport.format_runs = {run};
	viewport.utf8 = {0xc3, 0xa9, ' ', 'x'};
	std::vector<std::uint8_t> slot(FairyWriter::MailboxLayout::ViewportSlotBytes);
	expect(FairyWriter::encodeViewport(viewport, slot), "viewport fits in a slot");
	expect(slot[0] == 9 && slot[8] == 12 && slot[32] == 4 && slot[34] == 7 && slot[36] == 5
		&& slot[37] == 'S' && slot[41] == 'Y'
		&& slot[FairyWriter::MailboxLayout::ViewportHeaderBytes] == 0xc3
		&& slot[FairyWriter::MailboxLayout::ViewportHeaderBytes + 1] == 0xa9,
		"viewport title, chapter, and UTF-8 payload are serialized");
	expect(slot[91] == 3 && slot[92] == 0x02 && slot[93] == 0x01 && slot[94] == 0 && slot[95] == 0,
		"viewport status flags and line count are serialized in the header tail");
	expect(slot[96] == 5 && slot[100] == 2 && slot[104] == 1 && slot[108] == 100 && slot[112] == 10 && slot[116] == 20,
		"extended statistics and surrounding budgets are serialized");
	expect(slot[120] == 4 && slot[122] == 1 && slot[124] == 1, "table counts are serialized");
	expect(slot[128] == 0 && slot[130] == 2 && slot[132] == 3 && slot[134] == 4, "grapheme offsets table serialized");
	expect(slot[256] == 3, "line break offsets table serialized");
	expect(slot[384] == 0 && slot[386] == 2 && slot[388] == 1, "format runs table serialized");
	viewport.display_title.resize(FairyWriter::MailboxLayout::ViewportTitleBytes + 1);
	expect(!FairyWriter::encodeViewport(viewport, slot), "oversized display title is rejected");
	viewport.display_title={'S','T','O','R','Y'};
	viewport.utf8.resize(FairyWriter::MailboxLayout::ViewportSlotBytes);
	expect(!FairyWriter::encodeViewport(viewport, slot), "oversized viewport is rejected");
	FairyWriter::ViewportSlots slots;
	expect(slots.activeIndex() == 0 && slots.publish(viewport) == false, "oversized snapshot never commits a slot");
	viewport.utf8.resize(4);
	expect(slots.publish(viewport) && slots.activeIndex() == 1, "complete snapshot commits inactive slot");
	FairyWriter::GlyphTile glyph; glyph.request_id = 0x1234; glyph.font_role = 2; glyph.style = 7; glyph.width_tiles = 2; glyph.flags = 1; glyph.pixels.resize(64, 0xaa);
	std::vector<std::uint8_t> glyph_wire;
	expect(FairyWriter::encodeGlyphTile(glyph, glyph_wire) && glyph_wire.size() == 72, "two-tile glyph response encodes at a fixed wire size");
	FairyWriter::GlyphTile decoded_glyph;
	expect(FairyWriter::decodeGlyphTile(glyph_wire.data(), glyph_wire.size(), decoded_glyph) && decoded_glyph.request_id == glyph.request_id && decoded_glyph.pixels == glyph.pixels, "glyph response round trips its cache key and 4bpp pixels");
	glyph_wire[4] = 3;
	expect(!FairyWriter::decodeGlyphTile(glyph_wire.data(), glyph_wire.size(), decoded_glyph), "invalid glyph width is rejected before cache mutation");
	FairyWriter::GlyphRing glyph_ring;
	expect(FairyWriter::GlyphRing::Capacity == 53, "glyph ring capacity covers the complete 0x0f00 SRAM region");
	for (std::size_t i = 0; i < FairyWriter::GlyphRing::Capacity; ++i) { glyph.request_id = static_cast<std::uint16_t>(i); expect(glyph_ring.push(glyph), "glyph ring accepts bounded responses"); }
	expect(glyph_ring.used() == FairyWriter::GlyphRing::Capacity && !glyph_ring.push(glyph), "glyph ring rejects overflow without eviction");
	for (std::size_t i = 0; i < FairyWriter::GlyphRing::Capacity; ++i) expect(glyph_ring.pop(decoded_glyph) && decoded_glyph.request_id == i, "glyph ring preserves FIFO request order");
	expect(glyph_ring.used() == 0 && !glyph_ring.pop(decoded_glyph), "glyph ring reports empty after draining");
	if (!failures) std::cout << "All FairyWriter mailbox tests passed.\n";
	return failures;
}
