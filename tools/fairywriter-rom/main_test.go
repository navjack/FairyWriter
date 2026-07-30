package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestCartridgeImageOwnsRealSnesContract(t *testing.T) {
	first, tiles := build()
	second, secondTiles := build()
	if !bytes.Equal(first, second) || tiles != secondTiles {
		t.Fatal("source-owned cartridge build is not deterministic")
	}
	if len(first) != 0x10000 || binary.LittleEndian.Uint16(first[0x7ffc:]) != 0x8000 {
		t.Fatal("cartridge is not a 64 KiB LoROM with a real reset vector")
	}
	if first[0x7fd5] != 0x20 || first[0x7fd6] != 0x02 || first[0x7fd7] != 6 || first[0x7fd8] != 5 {
		t.Fatal("header does not declare 64 KiB LoROM plus the 32 KiB SRAM mailbox")
	}
	// Bank 0 is program plus scan map; bank 1 is data. The scan map is the one
	// table read through DB with `LDA abs,X`, so it has to stay in bank 0 -- in
	// bank 1 that read would silently return program bytes.
	if scanMapOffset/0x8000 != 0 || scanMapOffset+256 > 0x7fb0 {
		t.Fatalf("scan map at %#x is not inside bank 0 below the v3 header", scanMapOffset)
	}
	for _, region := range []struct {
		name   string
		offset int
	}{
		{"palette", paletteOffset}, {"tilemap", tilemapOffset}, {"menu", menuOffset},
		{"browser", browserOffset}, {"browser-ready", browserReadyOffset},
		{"help", helpOffset}, {"settings", settingsOffset},
		{"save-format", saveFormatOffset}, {"filename", filenameOffset},
		{"tiles", tilesOffset},
	} {
		if region.offset/0x8000 != 1 {
			t.Fatalf("%s data at %#x is not in bank 1", region.name, region.offset)
		}
	}
	// snes_other.c picks the mapper from whichever candidate header location
	// scores highest, not from the map-mode byte. At 64 KiB $81c0 and $ffc0 are
	// both probed, so both windows have to stay empty or the cartridge can load
	// as HiROM. tools/fairywriter-romcheck asserts the resulting scores.
	for _, window := range [][2]int{{0x81b0, 0x8200}, {bank1Limit, romSize}} {
		for i := window[0]; i < window[1]; i++ {
			if first[i] != 0 {
				t.Fatalf("reserved header-scoring window %#x-%#x is not zero at %#x",
					window[0], window[1], i)
			}
		}
	}
	// Two independent ceilings. 1024 is VRAM -- BG1 plus OBJ cannot address more
	// 4bpp tiles however large the cartridge gets -- and bank1Limit is where the
	// tile blob actually runs out of ROM. This used to compare against 0x7fc0,
	// i.e. 562 tiles, while build() panicked at 555, so the contract could never
	// fail before the builder crashed.
	if tiles > 1024 || tiles*32 > bank1Limit-tilesOffset {
		t.Fatalf("PPU character data exceeds BG1 or cartridge capacity: %d tiles", tiles)
	}
	complement := binary.LittleEndian.Uint16(first[0x7fdc:])
	checksum := binary.LittleEndian.Uint16(first[0x7fde:])
	if complement != ^checksum {
		t.Fatal("SNES checksum and complement disagree")
	}
	// The canonical checksum is taken with the checksum reading $0000 and its
	// complement reading $ffff so the pair cancels. Zeroing both lands 0x1fe low
	// -- accepted by a lenient loader, rejected by a real verifier.
	canonical := append([]byte(nil), first...)
	binary.LittleEndian.PutUint16(canonical[0x7fdc:], 0xffff)
	binary.LittleEndian.PutUint16(canonical[0x7fde:], 0x0000)
	var sum uint16
	for _, v := range canonical {
		sum += uint16(v)
	}
	if sum != checksum {
		t.Fatalf("checksum %#04x is not the canonical sum %#04x", checksum, sum)
	}
	// Developer ID $33 is what tells a loader the v3 extended header at $ffb0 is
	// real, so those bytes have to be written rather than inherited from
	// whatever the scan-map tail left behind.
	if first[0x7fda] != 0x33 {
		t.Fatal("header does not declare the v3 extended header")
	}
	if !bytes.Equal(first[0x7fb0:0x7fb6], []byte("FWFWTR")) {
		t.Fatalf("v3 extended header is not populated: % x", first[0x7fb0:0x7fc0])
	}
	// Every vector except RESET lands on an RTI so a spurious interrupt returns
	// instead of re-entering the reset path and re-running init.
	stub := binary.LittleEndian.Uint16(first[0x7fe4:])
	if stub < 0x8000 || first[stub-0x8000] != 0x40 {
		t.Fatalf("interrupt vectors do not point at an RTI stub (%#04x)", stub)
	}
	for off := 0x7fe0; off < 0x8000; off += 2 {
		want := stub
		if off == 0x7ffc {
			want = 0x8000
		}
		if got := binary.LittleEndian.Uint16(first[off:]); got != want {
			t.Fatalf("vector at %#x is %#04x, want %#04x", off, got, want)
		}
	}
	// DB is established at reset rather than inherited: SEP #$30 then PHK; PLB.
	if !bytes.Contains(first[:32], []byte{0xe2, 0x30, 0x4b, 0xab}) {
		t.Fatal("reset path does not establish DB with PHK; PLB")
	}
	// Pin the real guest boundary: long SRAM reads/writes, the VBlank gate,
	// and the VRAM data port must remain in the generated 65816 body.
	for _, sequence := range [][]byte{
		{0xaf, 0x00, 0x00, 0x70},
		{0x8f, 0x02, 0x00, 0x70},
		{0xad, 0x12, 0x42},
		{0xa9, 0x18, 0x8d, 0x01, 0x43}, // staged tile-plane DMA to VMDATAL
		{0xbd, 0x00, 0x05},             // guest document buffer read
		{0x9d, 0x00, 0x02},             // guest document buffer write
		{0xa9, 0x7f, 0x8d, 0x01, 0x42}, // IOBIT low begins XBAND transfer
		{0xad, 0x17, 0x40, 0x29, 0x03}, // two DATA bits per read
		{0xc9, 0x78},                   // XBAND keyboard ID
		{0xc9, 0xe0},                   // extended-scancode prefix
		{0xc9, 0xf0},                   // key-release prefix
		{0xbd, byte(loromAddr(scanMapOffset)), byte(loromAddr(scanMapOffset) >> 8)}, // guest-side scancode map
	} {
		if !bytes.Contains(first[:scanMapOffset], sequence) {
			t.Fatalf("65816 cartridge body lost required machine sequence % x", sequence)
		}
	}
}

func TestProofingVisualsEnabledWithSafeStartupState(t *testing.T) {
	if !proofingVisualsEnabled {
		t.Fatal("proofing visuals should be enabled once proof-map wiring and rendering coverage are in place")
	}
	rom, _ := build()
	// Startup still keeps subscreen/color-math off for the current palette-based
	// proofing path.
	for _, sequence := range [][]byte{
		{0xa9, 0x00, 0x8d, 0x2d, 0x21}, // LDA #$00; STA $212d
		{0xa9, 0x30, 0x8d, 0x30, 0x21}, // LDA #$30; STA $2130
		{0xa9, 0x00, 0x8d, 0x31, 0x21}, // LDA #$00; STA $2131
	} {
		if !bytes.Contains(rom[:scanMapOffset], sequence) {
			t.Fatalf("missing proofing safety gate sequence % x", sequence)
		}
	}
}

func TestProofingIntegrationConsumesProofMapAndAvoidsLegacyMaskWrites(t *testing.T) {
	rom, _ := build()
	// Enabled proofing must consume the staged proof map in draw-character
	// attribute selection, while still avoiding the old BG3 proof-mask write
	// path that previously destabilized rendering.
	required := []byte{0xbd, 0x00, 0x0b} // LDA $0b00,X
	if !bytes.Contains(rom[:scanMapOffset], required) {
		t.Fatalf("enabled proofing did not consume proof-map sequence % x", required)
	}
	disallowed := []byte{0x99, 0x00, 0x13} // STA $1300,Y legacy mask path
	if bytes.Contains(rom[:scanMapOffset], disallowed) {
		t.Fatalf("legacy proof-mask write sequence must remain absent % x", disallowed)
	}
}

func TestProofingFormatRunFlagsProjectIntoPerCellProofMap(t *testing.T) {
	rom, _ := build()
	// Viewport decode must consume format-run table metadata from slot header
	// and project spelling/grammar flags into per-cell proof-map bytes.
	for _, sequence := range [][]byte{
		{0xaf, 0x7c, 0x41, 0x70}, // LDA.l $70417c (format run count)
		{0xbf, 0x84, 0x42, 0x70}, // LDA.l $704284,X (run flags)
		{0xdf, 0x80, 0x42, 0x70}, // CMP.l $704280,X (run start)
		{0xdf, 0x82, 0x42, 0x70}, // CMP.l $704282,X (run length)
		{0x99, 0x00, 0x0b},       // STA $0b00,Y (proof map write)
	} {
		if !bytes.Contains(rom[:scanMapOffset], sequence) {
			t.Fatalf("missing format-run proof-map wiring sequence % x", sequence)
		}
	}
}

func TestRichStyleFlagsProjectIntoPerCellProofMapAndGateTileGeneration(t *testing.T) {
	if !richStyleVisualsEnabled {
		t.Fatal("rich style visuals should be enabled once glyph-shape tiles and draw-loop wiring are in place")
	}
	rom, tiles := build()
	// The projection mask must include bold|italic|underline (bits 0-2)
	// alongside spelling|grammar (bits 3-4) so format-run style flags reach
	// the per-cell proof map, not just proofing flags.
	required := []byte{0xbf, 0x84, 0x42, 0x70, 0x29, 0x1f} // LDA.l $704284,X; AND #$1f
	if !bytes.Contains(rom[:scanMapOffset], required) {
		t.Fatalf("rich-style-enabled build did not widen the format-run projection mask to #$1f % x", required)
	}
	disallowed := []byte{0xbf, 0x84, 0x42, 0x70, 0x29, 0x18} // the narrower spell|grammar-only mask
	if bytes.Contains(rom[:scanMapOffset], disallowed) {
		t.Fatalf("rich-style-enabled build must not retain the narrower spell|grammar-only projection mask % x", disallowed)
	}
	// One glyph page per style combination: the plain page keeps all 128
	// ASCII-aligned tiles, and each of the seven styled pages stores printable
	// ASCII only.
	wantGlyphTiles := 128 + (stylePageCount-1)*styledPageTiles
	if tiles < wantGlyphTiles {
		t.Fatalf("rich-style-enabled build should emit at least %d glyph tiles, got %d",
			wantGlyphTiles, tiles)
	}
}

// Alignment reaches the cartridge in the format run's own byte, and a run that
// carries nothing else must still be projected: a centred plain paragraph has no
// bold, italic, underline or proofing bit anywhere in it.
func TestParagraphAlignmentIsProjectedAndApplied(t *testing.T) {
	rom, _ := build()
	program := rom[:scanMapOffset]
	for _, want := range []struct {
		name  string
		bytes []byte
	}{
		{"the run's alignment byte is read", []byte{0xbf, 0x86, 0x42, 0x70, 0x29, 0x03}},
		{"it is staged per character at $0d00", []byte{0x99, 0x00, 0x0d}},
		{"the row's alignment is read back at $0f14", []byte{0xad, 0x14, 0x0f, 0xc9, 0x01}},
		{"a centred row is placed on half its free width", []byte{0xa9, 0x1e, 0x38, 0xed, 0x18, 0x0f, 0x4a}},
		{"a right aligned row is placed on all of it", []byte{0xa9, 0x1e, 0x38, 0xed, 0x18, 0x0f}},
		{"the per-row shift is recorded for the pointer", []byte{0x9d, 0x00, 0x0f}},
	} {
		if !bytes.Contains(program, want.bytes) {
			t.Fatalf("alignment build is missing where %s: % x", want.name, want.bytes)
		}
	}
}

// Bold, italic and underline are independent character properties, so a writer
// can hold any combination of them and each combination needs its own glyph.
// Collapsing two of them onto one page is exactly the defect where holding bold
// and italic together showed only one of the two.
func TestEveryStyleCombinationRendersItsOwnGlyph(t *testing.T) {
	for _, ch := range []byte("AgQ!7") {
		seen := map[string]int{}
		for style := 0; style < stylePageCount; style++ {
			key := string(glyphShapePixels(ch, style))
			if previous, clash := seen[key]; clash {
				t.Fatalf("character %q renders identically for style masks %d and %d",
					ch, previous, style)
			}
			seen[key] = style
		}
	}
	// Each property must also be individually visible in a combination, not
	// merely different from its neighbours: underline owns the 8th pixel row,
	// and italic's skew puts ink in a column the upright glyph never reaches.
	upright := glyphShapePixels('A', 0)
	for style := 1; style < stylePageCount; style++ {
		pixels := glyphShapePixels('A', style)
		underlined := false
		for col := 0; col < 8; col++ {
			if pixels[7*8+col] != 0 && pixels[7*8+col] != 4 {
				underlined = true
			}
		}
		if want := style&styleUnderline != 0; underlined != want {
			t.Fatalf("style mask %d underline row lit=%v, want %v", style, underlined, want)
		}
		ink, uprightInk := 0, 0
		for i := range pixels {
			if pixels[i] == 15 {
				ink++
			}
			if upright[i] == 15 {
				uprightInk++
			}
		}
		if style&styleBold != 0 && ink <= uprightInk {
			t.Fatalf("style mask %d is not bolder than the upright glyph: %d vs %d ink pixels",
				style, ink, uprightInk)
		}
	}
}

// The packed tile blob and the strided BG1 id space are two different
// orderings. Every uploaded run must land on the ids the draw loop computes,
// stay inside BG1's 1024-id ceiling, and never overlap another run: a run at the
// wrong VRAM address is invisible in the ROM bytes and shows up only as garbage
// glyphs on screen.
func TestTileUploadsCoverEveryGlyphPageWithoutOverlap(t *testing.T) {
	_, tiles, uploads := encode(scene())
	occupied := map[int]bool{}
	for _, upload := range uploads {
		if upload.count <= 0 {
			t.Fatalf("upload %+v transfers nothing", upload)
		}
		if upload.romTile+upload.count > len(tiles)/32 {
			t.Fatalf("upload %+v reads past the %d-tile blob", upload, len(tiles)/32)
		}
		for i := 0; i < upload.count; i++ {
			id := upload.vramTile + i
			if id >= stylePageCount*stylePageStride {
				t.Fatalf("upload %+v reaches tile id %d, past BG1's addressable pages", upload, id)
			}
			if occupied[id] {
				t.Fatalf("upload %+v re-uploads tile id %d", upload, id)
			}
			occupied[id] = true
		}
	}
	for page := 0; page < stylePageCount; page++ {
		first, last := styledFirstChar, styledLastChar
		if page == 0 {
			first, last = 0, 127
		}
		for ch := first; ch <= last; ch++ {
			if !occupied[page*stylePageStride+ch] {
				t.Fatalf("glyph page %d has no uploaded tile for character %d", page, ch)
			}
		}
	}
	// Scene art must live in the styled pages' unreferenced control-character
	// slots rather than past them, which is what keeps the eight pages inside
	// the 1024-id ceiling.
	for _, id := range sceneTileIds() {
		if id%stylePageStride >= styledFirstChar {
			t.Fatalf("scene tile id %d is inside a styled page's printable range", id)
		}
	}
}

func TestXbandScancodeMapIsCartridgeOwned(t *testing.T) {
	m := xbandScanMap()
	for scan, want := range map[byte]byte{
		0x05: 0x18,
		0x0d: 0x09,
		0x1c: 'a', 0x32: 'b', 0x21: 'c', 0x15: 'q', 0x1a: 'z',
		0x16: '1', 0x45: '0', 0x29: ' ', 0x5a: 0x0d, 0x66: 0x08,
		0x4e: '-', 0x55: '=', 0x54: '[', 0x5b: ']', 0x4c: ';',
		0x52: '\'', 0x0e: '`', 0x5d: '\\',
		0x70: '!', 0x7e: '@', 0x7d: '#', 0x7c: '$', 0x7b: '%',
		0x7a: '^', 0x79: '&', 0x78: '*', 0x77: '(', 0x76: ')',
		0x75: '_', 0x74: '+', 0x73: '<', 0x72: '>', 0x71: '?',
		0x6f: '"', 0x6e: '{', 0x6d: '}', 0x6c: ':', 0x6b: '|',
		0x69: '~',
	} {
		if got := m[scan]; got != want {
			t.Fatalf("XBAND scan %02x mapped to %q, want %q", scan, got, want)
		}
	}
	for _, prefix := range []byte{0xe0, 0xf0} {
		if m[prefix] != 0 {
			t.Fatalf("protocol prefix %02x must not map to printable text", prefix)
		}
	}
}

func TestFilenamePlaneIsACompleteBoundedDialog(t *testing.T) {
	plane := filenamePlane()
	if len(plane) != 30*8 {
		t.Fatalf("filename plane is %d bytes, want one complete 30x8 page", len(plane))
	}
	if !bytes.Equal(plane[3*30+2:3*30+28], []byte("+------------------------+")) ||
		!bytes.Equal(plane[4*30+2:4*30+28], []byte("|                        |")) {
		t.Fatal("filename plane lost its 24-cell bordered input field")
	}
	if bytes.Contains(plane, []byte("FILE BROWSER")) {
		t.Fatal("filename dialog must not inherit browser-plane content")
	}
}

func TestPrintableKeyboardGlyphsAreResident(t *testing.T) {
	for _, ch := range []byte("!@#$%^&*()_+-=[]{};:'\",.<>/?\\|`~") {
		g, ok := glyphs[ch]
		if !ok {
			t.Fatalf("printable keyboard character %q has no resident SNES glyph", ch)
		}
		var ink byte
		for _, row := range g {
			ink |= row
		}
		if ink == 0 {
			t.Fatalf("printable keyboard character %q renders as a blank tile", ch)
		}
	}
}

// The document panel's frame must actually enclose the document plane. These are
// two independent numbers -- the plane's size lives in the DMA/render code and
// the panel's size lives in scene() -- and they silently drifted apart once the
// plane grew from 8 rows to 30x17: the document kept painting 26 lines past its
// own border and straight over the footer bar that used to sit below it, which
// looked like a stray maroon tile hanging outside the panel.
func TestDocumentPanelEnclosesTheDocumentPlane(t *testing.T) {
	const (
		documentTopY = 80 // tilemap row 10, where the plane's first row lands
		documentRows = 17 // rows the VBlank upload DMAs
		documentMidX = 128
	)
	documentBottomY := documentTopY + documentRows*8 - 1

	c := scene()
	// frame() paints its field colour last, so every row the document covers must
	// already be field-coloured in the static art.
	field := c[documentTopY][documentMidX]
	for y := documentTopY; y <= documentBottomY; y++ {
		if got := c[y][documentMidX]; got != field {
			t.Fatalf("document row at y=%d is not inside the panel field: got %d want %d; "+
				"the panel frame in scene() no longer encloses all %d document rows",
				y, got, field, documentRows)
		}
	}
	// And the panel must close underneath the document rather than running off the
	// bottom of the screen.
	if documentBottomY+1 >= screenH {
		t.Fatalf("document plane reaches y=%d with no room for a bottom border before %d",
			documentBottomY, screenH)
	}
	if c[documentBottomY+1][documentMidX] == field {
		t.Fatalf("no panel border below the document plane at y=%d; the frame does not close",
			documentBottomY+1)
	}
}
