// fairywriter-rom builds FairyWriter's source-owned SNES LoROM cartridge.
// The generated .sfc is a build artifact and contains no commercial ROM data.
package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"sort"
)

const (
	screenW = 256
	screenH = 224
	// Two LoROM banks. The cartridge was 32 KiB -- the smallest image the SNES
	// accepts, and the only thing a 32 KiB image can be -- until program space
	// ran down to 59 free program bytes and zero free PPU-tile slots. LoROM
	// itself was never the constraint; it addresses up to 4 MB.
	romSize = 0x10000

	// Bank 0 is program plus the scan map, and nothing else. The layout follows
	// the standard LoROM convention for data-heavy carts: code at the start of
	// the image, bulk data at the end.
	//
	// The scan map stays in bank 0 because it is the one data table read with
	// `LDA abs,X` rather than a long read -- it resolves through DB, so moving
	// it to bank 1 would silently read program bytes instead. Everything else is
	// either DMA'd (bank supplied via $4304) or read with `LDA.l` (bank in the
	// operand), so both follow their offsets across the boundary.
	scanMapOffset = 0x7eb0 // 256 bytes, ending exactly at the v3 extended header

	// Bank 1 is data. It opens at 0x8200 rather than 0x8000 to clear the first
	// reserved window; see reservedWindows below for why those exist.
	paletteOffset      = 0x8200 // 128
	tilemapOffset      = 0x8280 // 2048
	menuOffset         = 0x8a80 // 240
	browserOffset      = 0x8b70 // 240
	browserReadyOffset = 0x8c60 // 240
	// The help card is a full-screen static plane like the menu and browser
	// planes, not a dialog overlay, so it gets its own 240-byte page and is
	// copied by the same 8-bit-X loop they use.
	helpOffset            = 0x8d50 // 240
	settingsOffset        = 0x8e40 // 240
	saveFormatOffset      = 0x8f30 // 240
	saveRootTitleOffset   = 0x9020 // 30
	saveFolderTitleOffset = 0x903e // 30
	filenameOffset        = 0x9080 // 240
	// Tiles are last because they are the only variable-size blob, so the fixed
	// pages above keep stable offsets. Capacity here is 0xffb0-tilesOffset =
	// 28208 bytes = 881 tiles, up from the 554 that exactly filled the old
	// layout. The hard ceiling beyond that is VRAM, not ROM: BG1 plus OBJ cannot
	// address more than 1024 4bpp tiles however large the cartridge gets. New
	// static planes come out of this budget by shifting tilesOffset up.
	tilesOffset = 0x9180
	// bank1Limit is where bank 1's data has to stop: the start of the reserved
	// header-scoring window at $ffb0. See the reserved-window check in build().
	bank1Limit = 0xffb0
	// Proofing visuals must stay opt-in until a fully test-gated BG3/color-math
	// path lands without destabilizing document tile rendering.
	proofingVisualsEnabled = true
	documentBaseAttr       = byte(0x08)
	proofingSpellingAttr   = byte(0x0c)
	proofingGrammarAttr    = byte(0x04)
	documentPlaneColumns   = 30
	documentPlaneRows      = 17
	documentPlaneCells     = documentPlaneColumns * documentPlaneRows
	scrollThumbTrackStart  = 11
	scrollThumbTrackWidth  = 234
	scrollThumbWidth       = 8
	scrollThumbTravel      = scrollThumbTrackWidth - scrollThumbWidth
	scrollThumbY           = 69
	// Rich style (bold/italic/underline) rendering: a real kill switch, not
	// just a comment -- setting this false removes the extra 384 glyph-shape
	// tiles from encode() and reverts the draw loop's rich-style computation
	// to a no-op (attribute/tile-id bits it would set are simply never
	// touched), independent of proofingVisualsEnabled above.
	richStyleVisualsEnabled = true
)

type canvas [screenH][screenW]byte

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

var glyphs = map[byte][7]byte{
	' ': {}, 'A': {14, 17, 17, 31, 17, 17, 17}, 'B': {30, 17, 17, 30, 17, 17, 30},
	'C': {14, 17, 16, 16, 16, 17, 14}, 'D': {30, 17, 17, 17, 17, 17, 30},
	'E': {31, 16, 16, 30, 16, 16, 31}, 'F': {31, 16, 16, 30, 16, 16, 16},
	'G': {14, 17, 16, 23, 17, 17, 15}, 'H': {17, 17, 17, 31, 17, 17, 17},
	'I': {31, 4, 4, 4, 4, 4, 31}, 'J': {7, 2, 2, 2, 18, 18, 12},
	'K': {17, 18, 20, 24, 20, 18, 17}, 'L': {16, 16, 16, 16, 16, 16, 31},
	'M': {17, 27, 21, 21, 17, 17, 17}, 'N': {17, 25, 21, 19, 17, 17, 17},
	'O': {14, 17, 17, 17, 17, 17, 14}, 'P': {30, 17, 17, 30, 16, 16, 16},
	'Q': {14, 17, 17, 17, 21, 18, 13}, 'R': {30, 17, 17, 30, 20, 18, 17},
	'S': {15, 16, 16, 14, 1, 1, 30}, 'T': {31, 4, 4, 4, 4, 4, 4},
	'U': {17, 17, 17, 17, 17, 17, 14}, 'V': {17, 17, 17, 17, 17, 10, 4},
	'W': {17, 17, 17, 21, 21, 21, 10}, 'X': {17, 17, 10, 4, 10, 17, 17},
	'Y': {17, 17, 10, 4, 4, 4, 4}, 'Z': {31, 1, 2, 4, 8, 16, 31},
	'a': {0, 0, 14, 1, 15, 17, 15}, 'b': {16, 16, 30, 17, 17, 17, 30},
	'c': {0, 0, 14, 17, 16, 17, 14}, 'd': {1, 1, 15, 17, 17, 17, 15},
	'e': {0, 0, 14, 17, 31, 16, 14}, 'f': {6, 9, 8, 28, 8, 8, 8},
	'g': {0, 15, 17, 17, 15, 1, 14}, 'h': {16, 16, 30, 17, 17, 17, 17},
	'i': {4, 0, 12, 4, 4, 4, 14}, 'j': {2, 0, 6, 2, 2, 18, 12},
	'k': {16, 16, 18, 20, 24, 20, 18}, 'l': {12, 4, 4, 4, 4, 4, 14},
	'm': {0, 0, 26, 21, 21, 17, 17}, 'n': {0, 0, 30, 17, 17, 17, 17},
	// 'p' keeps its bowl at x-height (row 2, as in o/n/e) so it cannot be read as
	// a capital: the old shape was 'P' verbatim, shifted down one row.
	'o': {0, 0, 14, 17, 17, 17, 14}, 'p': {0, 0, 30, 17, 30, 16, 16},
	'q': {0, 15, 17, 17, 15, 1, 1}, 'r': {0, 0, 22, 25, 16, 16, 16},
	's': {0, 0, 15, 16, 14, 1, 30}, 't': {8, 8, 28, 8, 8, 9, 6},
	'u': {0, 0, 17, 17, 17, 19, 13}, 'v': {0, 0, 17, 17, 17, 10, 4},
	'w': {0, 0, 17, 17, 21, 21, 10}, 'x': {0, 0, 17, 10, 4, 10, 17},
	'y': {0, 17, 17, 17, 15, 1, 14}, 'z': {0, 0, 31, 2, 4, 8, 31},
	'0': {14, 17, 19, 21, 25, 17, 14}, '1': {4, 12, 4, 4, 4, 4, 14},
	'2': {14, 17, 1, 2, 4, 8, 31}, '3': {30, 1, 1, 14, 1, 1, 30},
	'4': {2, 6, 10, 18, 31, 2, 2}, '5': {31, 16, 16, 30, 1, 1, 30},
	'6': {14, 16, 16, 30, 17, 17, 14}, '7': {31, 1, 2, 4, 8, 8, 8},
	'8': {14, 17, 17, 14, 17, 17, 14}, '9': {14, 17, 17, 15, 1, 1, 14},
	':': {0, 4, 4, 0, 4, 4, 0}, '.': {0, 0, 0, 0, 0, 4, 4},
	',': {0, 0, 0, 0, 4, 4, 8}, '!': {4, 4, 4, 4, 4, 0, 4},
	'?': {14, 17, 1, 2, 4, 0, 4}, '-': {0, 0, 0, 31, 0, 0, 0},
	'\'': {4, 4, 8, 0, 0, 0, 0}, '"': {10, 10, 20, 0, 0, 0, 0},
	'/': {1, 2, 2, 4, 8, 8, 16}, '\\': {16, 8, 8, 4, 2, 2, 1},
	'@': {14, 17, 23, 21, 23, 16, 14}, '#': {10, 31, 10, 10, 31, 10, 0},
	'$': {4, 15, 20, 14, 5, 30, 4}, '%': {25, 26, 4, 8, 11, 19, 0},
	'^': {4, 10, 17, 0, 0, 0, 0}, '&': {12, 18, 20, 8, 21, 18, 13},
	'*': {0, 21, 14, 31, 14, 21, 0}, '(': {2, 4, 8, 8, 8, 4, 2},
	')': {8, 4, 2, 2, 2, 4, 8}, '_': {0, 0, 0, 0, 0, 0, 31},
	'+': {0, 4, 4, 31, 4, 4, 0}, '=': {0, 0, 31, 0, 31, 0, 0},
	'[': {14, 8, 8, 8, 8, 8, 14}, ']': {14, 2, 2, 2, 2, 2, 14},
	'{': {2, 4, 4, 8, 4, 4, 2}, '}': {8, 4, 4, 2, 4, 4, 8},
	';': {0, 4, 4, 0, 4, 4, 8}, '<': {2, 4, 8, 16, 8, 4, 2},
	'>': {8, 4, 2, 1, 2, 4, 8}, '|': {4, 4, 4, 4, 4, 4, 4},
	'`': {8, 4, 0, 0, 0, 0, 0}, '~': {0, 0, 9, 22, 0, 0, 0},
}

func color(r, g, b uint16) uint16 { return (r & 31) | ((g & 31) << 5) | ((b & 31) << 10) }

func (c *canvas) rect(x, y, w, h int, v byte) {
	for yy := max(0, y); yy < min(screenH, y+h); yy++ {
		for xx := max(0, x); xx < min(screenW, x+w); xx++ {
			c[yy][xx] = v
		}
	}
}

func (c *canvas) frame(x, y, w, h int, field byte) {
	c.rect(x, y, w, h, 1)
	c.rect(x+1, y+1, w-2, h-2, 6)
	c.rect(x+3, y+3, w-6, h-6, 2)
	c.rect(x+5, y+5, w-10, h-10, field)
}

func (c *canvas) text(x, y int, s string, ink byte) {
	for _, raw := range []byte(s) {
		ch := raw
		if ch >= 'a' && ch <= 'z' {
			ch -= 32
		}
		g, ok := glyphs[ch]
		if !ok {
			g = glyphs['?']
		}
		for row, bits := range g {
			for col := 0; col < 5; col++ {
				if bits&(1<<uint(4-col)) != 0 {
					if x+col+1 < screenW && y+row+1 < screenH {
						c[y+row+1][x+col+1] = 2
					}
					if x+col < screenW && y+row < screenH {
						c[y+row][x+col] = ink
					}
				}
			}
		}
		x += 6
	}
}

func scene() canvas {
	var c canvas
	c.rect(0, 0, screenW, screenH, 12)
	for y := 0; y < screenH; y += 4 {
		for x := (y / 4) % 2 * 4; x < screenW; x += 8 {
			c.rect(x, y, 4, 2, 13)
		}
	}

	// Brown manuscript identity card. Its document title is supplied by the
	// committed host viewport and rendered by the cartridge at runtime.
	c.rect(4, 4, 150, 43, 9)
	c.rect(5, 5, 148, 41, 10)
	// The card interior and its speckle sit behind the live title cells, which
	// only cover the top four rows -- so a maroon interior showed as a red band
	// along the card's bottom edge once the cell planes stopped painting maroon.
	// Interior and speckle are now in the same blue family as everything else;
	// the amber border stays as the manuscript accent.
	c.rect(7, 7, 144, 37, 4)
	for y := 9; y < 43; y += 8 {
		for x := 9; x < 150; x += 12 {
			c.rect(x, y, 2, 2, 3)
			c.rect(x+3, y+2, 2, 2, 5)
		}
	}
	c.frame(158, 4, 94, 43, 4)
	// Interior of this card is a live toolbar row (bold/italic/underline,
	// alignment), uploaded from WRAM every render pass -- see
	// "Upload the 2x11 toolbar plane" below. No static text here.
	// The document panel has to enclose the whole document plane. That plane is
	// 30x17 and DMAs to tilemap row 10, so it paints y 80..215; `frame` insets
	// its field by 5, so the field runs y+5 .. y+h-6 and h must be at least 170
	// for the field's last row to reach 215. It used to be 139 -- sized when the
	// plane was only 8 rows -- which left the document painting 26 lines past its
	// own border and straight over the footer bar that used to sit at y 194..219,
	// burying its "F1 MENU / X SAVE / B BACK" hint under maroon document cells.
	// That stray maroon was the document itself, outside its frame.
	//
	// 17 rows end at y=215 and a bottom border needs 5 more, so there is no room
	// left for the footer: the hint bar is gone rather than the text rows.
	c.frame(4, 51, 248, 170, 4)
	c.rect(11, 72, 234, 2, 5)
	return c
}

func textPlane(lines ...string) []byte {
	plane := make([]byte, 30*8)
	for i := range plane {
		plane[i] = ' '
	}
	for row, line := range lines {
		if row >= 8 {
			break
		}
		copy(plane[row*30:(row+1)*30], []byte(line))
	}
	return plane
}

// The title row is not a selectable entry -- the '>' cursor and the highlight
// are written at row selection+1 -- so it is free to carry the help hint. A
// ninth row is not: the plane copy loop bounds itself with an 8-bit CPX #$F0,
// and 9 rows is 270 bytes, which would need that loop widened to a 16-bit
// index. That is the exact register-width change behind three separate
// defects in this file, and a help entry is not worth it.
func mainMenuPlane() []byte {
	return textPlane(
		"   FAIRYWRITER MENU   F2 HELP",
		"  NEW DOCUMENT",
		"  OPEN...",
		"  SAVE",
		"  SAVE AS...",
		"  RECENT FILES",
		"  SMART CHAPTERS",
		"  STATISTICS",
	)
}

// The controls card reached with F2. Like the browser-loading plane it has no
// selected row, so it inherits the cleared attribute plane and needs no
// highlight pass. Every line must fit 30 columns.
func helpPlane() []byte {
	return textPlane(
		"        FAIRYWRITER HELP",
		"  CTRL S SAVE    CTRL N NEW",
		"  CTRL Z UNDO    CTRL Y REDO",
		"  CTRL B I U   BOLD ITAL UNDL",
		"  CTRL C X V   COPY CUT PASTE",
		"  F3 SAVE SET    F4 FIND",
		"  DRAG THE BAR ABOVE TO SCROLL",
		"  F1 OR BACK RETURNS",
	)
}

func settingsPlane() []byte {
	return textPlane(
		"       SAVE AND RECOVERY",
		"  MODE:",
		"  INTERVAL:       MIN",
		"  COPIES:",
		"  RECOVERY HISTORY...",
		"  MARKDOWN:",
		"  LEFT RIGHT CHANGE",
		"  F3 OR BACK RETURNS",
	)
}

func saveFormatPlane() []byte {
	return textPlane(
		"        SAVE AS FORMAT",
		"  ODT",
		"  DOCX",
		"  RTF",
		"  MARKDOWN",
		"",
		"  ENTER CHOOSES",
		"  BACK CANCELS",
	)
}

func browserLoadingPlane() []byte {
	return textPlane(
		"         FILE BROWSER",
		"",
		"  ASKING THE HOST FOR FILES...",
		"",
		"  FAIRYWRITER KEEPS HOST PATHS",
		"  PRIVATE AND USES OPAQUE IDS.",
		"",
		"  F1 BACK",
	)
}

func transitionPlane() []byte {
	return textPlane(
		"        UNSAVED CHANGES",
		"  CHECKPOINT",
		"  SAVE",
		"  DISCARD",
		"  CANCEL",
		"",
		"  ENTER CHOOSES",
		"  BACK CANCELS",
	)
}

func browserReadyPlane() []byte {
	return textPlane(
		"         FILE BROWSER",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
	)
}

// Filename entry is a real modal plane rather than text painted over the last
// browser listing. The lower document panel owns the field and instructions;
// renderFilename also repurposes the title and formatting cards as contextual
// Save/Cancel controls. The visible field is deliberately the same width as
// the bounded filename buffer so entered text never disappears off-screen.
func filenamePlane() []byte {
	return textPlane(
		"       SAVE NEW DOCUMENT",
		"  ENTER A NAME FOR THE FILE",
		"",
		"  +------------------------+",
		"  |                        |",
		"  +------------------------+",
		"  TYPE NAME, THEN CHOOSE SAVE",
		"  ENTER USES HIGHLIGHTED ITEM",
	)
}

// Rich style is a three-bit mask, not a list of mutually exclusive shapes:
// bold, italic and underline are independent properties of a character and a
// writer can hold any two or all three at once. The mask value doubles as the
// glyph page index, and the page index is exactly what the tilemap carries:
// bit 0 of the page lands in the character byte (+128) and bits 1-2 in the
// attribute byte's tile-id bits 8-9, so the draw loop needs no arithmetic
// beyond masking the host's own flag bits. These values therefore match
// FormatRunBold/Italic/Underline in the host's mailbox wire on purpose.
const (
	styleBold      = 1
	styleItalic    = 2
	styleUnderline = 4
	// One page per mask value: plain plus every combination.
	stylePageCount = 8
	// Tile id = page*stylePageStride + ASCII. The stride is what makes the
	// character byte and the two attribute bits sufficient.
	stylePageStride = 128
	// Only printable ASCII can be styled, so a styled page stores that range
	// and nothing else. The ids below styledFirstChar and above styledLastChar
	// in each styled page are never referenced by the draw loop, which is where
	// the background/scene tiles live -- see encode().
	styledFirstChar = 32
	styledLastChar  = 126
	styledPageTiles = styledLastChar - styledFirstChar + 1
	// A capital glyph fills rows 0-6, so the underline has exactly one pixel row
	// and it is the row that touches the next line of text. Drawn in the ink
	// white at the full cell width it stops reading as an underline and starts
	// reading as a bar between two lines -- badly so where an underlined line
	// sits above another one. Index 5 is the palette's pale blue: it holds a
	// clearly visible line under its own text while staying quiet enough that
	// the letters of the following line stay readable against it, and stopping
	// one column short of the cell edge keeps the line from fusing into an
	// unbroken band across the whole width.
	underlineInk     = 5
	underlineColumns = 7
)

// glyphShapePixels renders one resident glyph's 8x8 pixel buffer (background
// color 4, ink color 15) for a rich-style mask (styleBold|styleItalic|
// styleUnderline, 0 for plain). Base glyphs are 5 columns wide (see `glyphs`),
// leaving columns 5-7 free, so bold's 1px dilation and italic's up-to-2px skew
// compose within the cell -- even together -- without clipping into the
// neighboring character, and underline owns the otherwise unused 8th pixel row
// that neither of the other two touches.
func glyphShapePixels(ch byte, style int) []byte {
	pixels := make([]byte, 64)
	for i := range pixels {
		pixels[i] = 4 // document-field blue behind every resident glyph
	}
	if ch == 127 {
		if style != 0 {
			// The block cursor glyph is style-invariant; the cursor draw
			// path always references the plain-bank tile directly.
			return pixels
		}
		// Guest-owned block cursor tile. It temporarily replaces the tile
		// under the insertion point and is restored by the next redraw.
		for row := 0; row < 8; row++ {
			pixels[row*8], pixels[row*8+1] = 14, 14
		}
		return pixels
	}
	g, ok := glyphs[ch]
	if !ok && ch >= 'a' && ch <= 'z' {
		g, ok = glyphs[ch-32]
	}
	if !ok {
		return pixels
	}
	for row, bits := range g {
		shift := 0
		if style&styleItalic != 0 { // skew increasing toward the top row, capped at 2px
			shift = (6 - row) / 3
		}
		for col := 0; col < 5; col++ {
			if bits&(1<<uint(4-col)) == 0 {
				continue
			}
			at := col + shift
			if at >= 0 && at < 8 {
				pixels[row*8+at] = 15
			}
			if style&styleBold != 0 && at+1 < 8 { // dilate every lit pixel one column right
				pixels[row*8+at+1] = 15
			}
		}
	}
	if style&styleUnderline != 0 { // light the glyph's unused 8th pixel row
		for col := 0; col < underlineColumns; col++ {
			pixels[7*8+col] = underlineInk
		}
	}
	return pixels
}

// tileUpload is one contiguous ROM-to-VRAM character-data transfer. The tile
// blob is packed (a styled page stores printable ASCII only) while the BG1 tile
// ids it occupies are strided, so the ROM order and the VRAM order are not the
// same sequence and the upload cannot be a single DMA.
type tileUpload struct {
	romTile  int
	vramTile int
	count    int
}

// sceneTileIds returns the BG1 tile ids available for background/scene art, in
// allocation order. Each styled page reserves stylePageStride ids but only
// stores printable ASCII, so the control-character slots of every styled page
// are ids the draw loop can never reference. Using them keeps the whole glyph
// scheme inside BG1's 1024-id ceiling instead of pushing scene art past it.
func sceneTileIds() []int {
	var ids []int
	for page := 1; page < stylePageCount; page++ {
		for ch := 0; ch < styledFirstChar; ch++ {
			ids = append(ids, page*stylePageStride+ch)
		}
	}
	return ids
}

func encode(c canvas) ([]byte, []byte, []tileUpload) {
	index := map[string]uint16{}
	var tiles [][]byte
	var uploads []tileUpload
	// Tile ids 0..127 intentionally equal ASCII. The 65816 can therefore
	// consume a host text byte from cartridge SRAM and write it directly to a
	// BG1 tilemap cell without a lookup table or host-side rendering.
	// Ids page*128 + ASCII are seven more glyph pages, one per bold/italic/
	// underline combination, selected by the character byte's bit 7 and the
	// attribute byte's tile-id bits 8-9 (see the draw loop's rich-style
	// attribute logic). A styled page stores printable ASCII only.
	for ch := 0; ch < 128; ch++ {
		pixels := glyphShapePixels(byte(ch), 0)
		tiles = append(tiles, pixels)
		if _, exists := index[string(pixels)]; !exists {
			index[string(pixels)] = uint16(ch)
		}
	}
	uploads = append(uploads, tileUpload{romTile: 0, vramTile: 0, count: 128})
	if richStyleVisualsEnabled {
		for page := 1; page < stylePageCount; page++ {
			uploads = append(uploads, tileUpload{
				romTile:  len(tiles),
				vramTile: page*stylePageStride + styledFirstChar,
				count:    styledPageTiles,
			})
			for ch := styledFirstChar; ch <= styledLastChar; ch++ {
				pixels := glyphShapePixels(byte(ch), page)
				id := uint16(page*stylePageStride + ch)
				tiles = append(tiles, pixels)
				if _, exists := index[string(pixels)]; !exists {
					index[string(pixels)] = id
				}
			}
		}
	}
	// Scene art claims the styled pages' unreferenced control-character slots.
	// Consecutive claims are merged so the upload stays a handful of DMAs.
	free := sceneTileIds()
	if !richStyleVisualsEnabled {
		free = nil
	}
	claimSceneTile := func(pixels []byte) uint16 {
		var id int
		if len(free) > 0 {
			id, free = free[0], free[1:]
		} else {
			// No styled pages (rich style disabled) or the holes are exhausted:
			// fall back to appending past the last glyph page.
			id = stylePageCount * stylePageStride
			for _, upload := range uploads {
				if end := upload.vramTile + upload.count; end > id {
					id = end
				}
			}
		}
		last := &uploads[len(uploads)-1]
		if last.vramTile+last.count == id && last.romTile+last.count == len(tiles) {
			last.count++
		} else {
			uploads = append(uploads, tileUpload{
				romTile: len(tiles), vramTile: id, count: 1,
			})
		}
		tiles = append(tiles, pixels)
		return uint16(id)
	}
	tilemap := make([]byte, 32*32*2)
	for ty := 0; ty < 28; ty++ {
		for tx := 0; tx < 32; tx++ {
			pixels := make([]byte, 64)
			for y := 0; y < 8; y++ {
				for x := 0; x < 8; x++ {
					pixels[y*8+x] = c[ty*8+y][tx*8+x] & 15
				}
			}
			key := string(pixels)
			id, ok := index[key]
			if !ok {
				id = claimSceneTile(pixels)
				index[key] = id
			}
			binary.LittleEndian.PutUint16(tilemap[(ty*32+tx)*2:], id)
		}
	}
	// The two guest OBJ tiles are always final so the program can address their
	// ROM bytes as (tiles end - 64) and upload them to the OBJ name base. They
	// are never referenced by the BG tilemap: tile 0 is the mouse pointer and
	// tile 1 is the document-position thumb.
	tiles = append(tiles, pointerSpritePixels())
	tiles = append(tiles, scrollThumbSpritePixels())
	encoded := make([]byte, len(tiles)*32)
	for id, pixels := range tiles {
		for y := 0; y < 8; y++ {
			for x := 0; x < 8; x++ {
				v := pixels[y*8+x]
				bit := byte(1 << uint(7-x))
				base := id * 32
				if v&1 != 0 {
					encoded[base+y*2] |= bit
				}
				if v&2 != 0 {
					encoded[base+y*2+1] |= bit
				}
				if v&4 != 0 {
					encoded[base+16+y*2] |= bit
				}
				if v&8 != 0 {
					encoded[base+16+y*2+1] |= bit
				}
			}
		}
	}
	return tilemap, encoded, uploads
}

// pointerSpritePixels returns the 8x8 4bpp arrow used for the resident SNES
// mouse pointer. Index 0 is transparent, index 1 is the dark outline, and index
// 15 is the white fill. The hotspot is the top-left pixel, matching the
// framebuffer-relative coordinate the host forwards into the port-1 packet.
func pointerSpritePixels() []byte {
	rows := [8][8]byte{
		{1, 0, 0, 0, 0, 0, 0, 0},
		{1, 1, 0, 0, 0, 0, 0, 0},
		{1, 15, 1, 0, 0, 0, 0, 0},
		{1, 15, 15, 1, 0, 0, 0, 0},
		{1, 15, 15, 15, 1, 0, 0, 0},
		{1, 15, 15, 15, 15, 1, 0, 0},
		{1, 15, 15, 1, 1, 1, 0, 0},
		{1, 1, 0, 1, 15, 1, 0, 0},
	}
	pixels := make([]byte, 64)
	for y := 0; y < 8; y++ {
		for x := 0; x < 8; x++ {
			pixels[y*8+x] = rows[y][x]
		}
	}
	return pixels
}

// scrollThumbSpritePixels returns the 8x8 4bpp marker that rides the static
// document-position track. It shares the pointer's OBJ palette: index 0 is
// transparent, index 1 is the dark outline, and index 15 is the white fill.
func scrollThumbSpritePixels() []byte {
	rows := [8][8]byte{
		{0, 0, 1, 1, 1, 1, 0, 0},
		{0, 1, 15, 15, 15, 15, 1, 0},
		{1, 15, 15, 15, 15, 15, 15, 1},
		{1, 15, 15, 15, 15, 15, 15, 1},
		{1, 15, 15, 15, 15, 15, 15, 1},
		{1, 15, 15, 15, 15, 15, 15, 1},
		{0, 1, 15, 15, 15, 15, 1, 0},
		{0, 0, 1, 1, 1, 1, 0, 0},
	}
	pixels := make([]byte, 64)
	for y := 0; y < 8; y++ {
		for x := 0; x < 8; x++ {
			pixels[y*8+x] = rows[y][x]
		}
	}
	return pixels
}

func xbandScanMap() [256]byte {
	var m [256]byte
	for scan, ch := range map[byte]byte{
		0x05: 0x18, // F1 opens the cartridge-owned menu.
		0x04: 0x19, // F3 opens Save and Recovery settings.
		0x0c: 0x1d, // F4 opens the cartridge-owned find screen.
		// Key codes 0x11-0x1b are taken by the extended cursor commands, F1 and
		// F3; 0x1c and 0x1d are used by F2 and F4.
		0x06: 0x1c, // F2 opens the cartridge-owned help card.
		0x1c: 'a', 0x32: 'b', 0x21: 'c', 0x23: 'd', 0x24: 'e', 0x2b: 'f',
		0x34: 'g', 0x33: 'h', 0x43: 'i', 0x3b: 'j', 0x42: 'k', 0x4b: 'l',
		0x3a: 'm', 0x31: 'n', 0x44: 'o', 0x4d: 'p', 0x15: 'q', 0x2d: 'r',
		0x1b: 's', 0x2c: 't', 0x3c: 'u', 0x2a: 'v', 0x1d: 'w', 0x22: 'x',
		0x35: 'y', 0x1a: 'z',
		0x16: '1', 0x1e: '2', 0x26: '3', 0x25: '4', 0x2e: '5',
		0x36: '6', 0x3d: '7', 0x3e: '8', 0x46: '9', 0x45: '0',
		0x29: ' ', 0x4e: '-', 0x49: '.', 0x41: ',', 0x4a: '/', 0x4c: ';',
		0x52: '\'', 0x55: '=', 0x54: '[', 0x5b: ']', 0x5d: '\\', 0x0e: '`',
		0x70: '!', 0x7e: '@', 0x7d: '#', 0x7c: '$', 0x7b: '%', 0x7a: '^',
		0x79: '&', 0x78: '*', 0x77: '(', 0x76: ')', 0x75: '_', 0x74: '+',
		0x73: '<', 0x72: '>', 0x71: '?', 0x6f: '"', 0x6e: '{', 0x6d: '}',
		0x6c: ':', 0x6b: '|', 0x69: '~',
		0x0d: 0x09, // Tab advances focus in cartridge-owned dialogs.
		0x5a: 0x0d, 0x66: 0x08,
	} {
		m[scan] = ch
	}
	return m
}

// LoROM maps each 32 KiB cartridge bank into the upper half of a CPU bank, so a
// flat file offset splits into a bank byte and a $8000-$ffff address. These are
// the single authority on that translation -- before they existed, `0x8000+off`
// was open-coded at ~90 sites, which is exactly the kind of thing that silently
// keeps compiling while addressing the wrong bank once the ROM outgrows 32 KiB.
func loromBank(offset int) byte   { return byte(offset >> 15) }
func loromAddr(offset int) uint16 { return uint16(0x8000 + (offset & 0x7fff)) }

// emitProgram returns the 65816 program image and the offset of its RTI
// interrupt landing pad, which build() wires into every non-RESET vector.
func emitProgram(tileBytes int, tileUploads []tileUpload) ([]byte, int) {
	var p []byte
	b := func(v ...byte) { p = append(p, v...) }
	// patch16 back-patches the 16-bit operand of an already-emitted instruction
	// at `at` to point at `target`. `jump` is the same thing named for its
	// common use (JMP/JSR abs); both exist because ~40 call sites used to inline
	// this assignment and bypass the helper entirely.
	patch16 := func(at, target int) {
		addr := loromAddr(target)
		p[at+1], p[at+2] = byte(addr), byte(addr>>8)
	}
	jump := patch16
	// jmpTo/jsrTo emit a same-bank absolute JMP/JSR. Both stay 16-bit: the
	// program lives entirely in bank 0, so a long JML/JSL would only cost bytes.
	jmpTo := func(target int) { addr := loromAddr(target); b(0x4c, byte(addr), byte(addr>>8)) }
	jsrTo := func(target int) { addr := loromAddr(target); b(0x20, byte(addr), byte(addr>>8)) }
	// branch back-patches an already-emitted 2-byte relative branch. It cannot
	// widen one: a trampoline is 3 bytes longer, and every label in this emitter
	// is a raw `len(p)` int already captured in a local, so inserting bytes here
	// would silently invalidate every offset downstream. Growing a branch is
	// therefore a source-level fix, and the panic says which one.
	//
	// The standard 65816 long-branch trampoline inverts the condition and skips
	// over an absolute JMP:
	//
	//	skip := len(p)
	//	b(0xd0, 0) // BNE over the JMP, i.e. the inverse of the BEQ you wanted
	//	jmpTo(target)
	//	branch(skip, len(p))
	//
	// Making this automatic needs labels to become objects with a relaxation
	// pass rather than ints -- worth doing, but not as a blind rewrite across
	// 423 branch sites in a ROM that currently works.
	branch := func(at, target int) {
		delta := target - (at + 2)
		if delta < -128 || delta > 127 {
			panic(fmt.Sprintf("65816 branch at %#x to %#x is out of range (delta %d); "+
				"rewrite it as an inverted-condition trampoline over a jmpTo", at, target, delta))
		}
		p[at+1] = byte(int8(delta))
	}
	sta := func(addr uint16) { b(0x8d, byte(addr), byte(addr>>8)) }
	ldaSta := func(v byte, addr uint16) { b(0xa9, v); sta(addr) }
	// dma takes a flat cartridge offset, not a CPU address: the A-bus address in
	// $4302/$4303 does not carry into the bank byte in $4304, so a transfer that
	// straddles a bank boundary silently wraps and re-reads the start of its own
	// bank rather than continuing into the next one.
	dma := func(srcOffset int, size uint16, mode, dest byte) {
		if (srcOffset&0x7fff)+int(size) > 0x8000 {
			panic(fmt.Sprintf("DMA source %#x size %d straddles a bank boundary", srcOffset, size))
		}
		src := loromAddr(srcOffset)
		ldaSta(mode, 0x4300)
		ldaSta(dest, 0x4301)
		ldaSta(byte(src), 0x4302)
		ldaSta(byte(src>>8), 0x4303)
		ldaSta(loromBank(srcOffset), 0x4304)
		ldaSta(byte(size), 0x4305)
		ldaSta(byte(size>>8), 0x4306)
		ldaSta(1, 0x420b)
	}
	b(0x78, 0x18, 0xfb, 0xc2, 0x30, 0xa2, 0xff, 0x1f, 0x9a, 0xe2, 0x30)
	// PHK; PLB -- establish DB=PB=$00 rather than inheriting it from reset
	// state. Every absolute STA to a hardware register below depends on DB being
	// $00, and the render path deliberately swaps DB to $7e and back (see the
	// note above the VBlank upload block), so the initial value should be set
	// here explicitly instead of being a property of how the CPU came up.
	b(0x4b, 0xab)
	ldaSta(0x80, 0x2100)
	b(0x9c, 0x00, 0x42)
	ldaSta(1, 0x2105)
	ldaSta(0, 0x2107)
	ldaSta(1, 0x210b)
	b(0x9c, 0x21, 0x21)
	dma(paletteOffset, 128, 0, 0x22)
	// OBJ palette 0 for the resident pointer and document-position thumb:
	// index 1 is the dark outline, index 15 the white fill, and index 0 stays
	// transparent.
	ldaSta(129, 0x2121)
	ldaSta(0x00, 0x2122)
	ldaSta(0x00, 0x2122)
	ldaSta(143, 0x2121)
	ldaSta(0xff, 0x2122)
	ldaSta(0x7f, 0x2122)
	ldaSta(0x80, 0x2115)
	b(0x9c, 0x16, 0x21, 0x9c, 0x17, 0x21)
	dma(tilemapOffset, 2048, 1, 0x18)
	// Character data is uploaded chunk by chunk. The packed ROM blob and the
	// strided BG1 id space are two different orderings (see tileUpload), so each
	// contiguous run sets its own VRAM word address. VMADD is a word address and
	// one 4bpp tile is 16 words.
	for _, upload := range tileUploads {
		const tileWords = 16
		vramWord := 0x1000 + upload.vramTile*tileWords
		ldaSta(byte(vramWord), 0x2116)
		ldaSta(byte(vramWord>>8), 0x2117)
		dma(tilesOffset+upload.romTile*32, uint16(upload.count*32), 1, 0x18)
	}
	// Upload the pointer and position-thumb OBJ tiles to VRAM word $6000 (OBJ
	// name base 3). Their ROM bytes are the final 64 of the tile blob.
	ldaSta(0x80, 0x2115)
	ldaSta(0x00, 0x2116)
	ldaSta(0x60, 0x2117)
	dma(tilesOffset+tileBytes-64, 64, 1, 0x18)
	// OBSEL: 8x8 objects, name base 3 -> VRAM word $6000.
	ldaSta(0x03, 0x2101)
	// Hide every sprite by parking it off the 224-line screen, then clear the
	// high OAM table so no sprite inherits a large size or high-X bit. Sprite 0
	// is the mouse pointer; sprite 1 is the document-position thumb.
	ldaSta(0x00, 0x2102)
	ldaSta(0x00, 0x2103)
	b(0xa2, 0x80) // LDX #128 sprites
	oamHide := len(p)
	b(0xa9, 0x00, 0x8d, 0x04, 0x21) // x = 0
	b(0xa9, 0xf0, 0x8d, 0x04, 0x21) // y = 240 (off-screen)
	b(0xa9, 0x00, 0x8d, 0x04, 0x21) // tile 0
	b(0x8d, 0x04, 0x21)             // attr 0 (A already 0)
	b(0xca)                         // DEX
	bneOamHide := len(p)
	b(0xd0, 0)
	branch(bneOamHide, oamHide)
	b(0xa2, 0x20) // LDX #32 high-table bytes
	oamHideHigh := len(p)
	b(0x9c, 0x04, 0x21, 0xca) // STZ $2104; DEX
	bneOamHideHigh := len(p)
	b(0xd0, 0)
	branch(bneOamHideHigh, oamHideHigh)
	ldaSta(0x11, 0x212c) // enable BG1 and OBJ
	// Proofing visuals remain disabled by default; when enabled, proofing style
	// bytes are consumed from cartridge-owned staging to choose per-cell
	// palette attributes in the draw loop.
	ldaSta(0x00, 0x212d) // keep subscreen disabled until proofing path is proven safe
	ldaSta(0x30, 0x2130) // color math disabled (safe baseline)
	ldaSta(0x00, 0x2131) // no layers participate in color math
	ldaSta(0x0f, 0x2100)
	// The primary input is the real XBAND controller-port-2 protocol. The
	// three-byte SRAM channel remains a bring-up fallback for emulators that do
	// not yet expose the rare keyboard peripheral.
	b(0x64, 0x00, 0x64, 0x08)                                                                   // cursor=0, document length=0
	b(0x9c, 0x0f, 0x03)                                                                         // Caps Lock state starts off in dedicated WRAM.
	b(0xa9, 0xff, 0x8f, 0x10, 0x03, 0x7e)                                                       // no host viewport has been consumed
	b(0x9c, 0x11, 0x03, 0x9c, 0x12, 0x03)                                                       // command sequence starts at zero
	b(0x9c, 0x17, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x19, 0x03)                                     // command kind/count/flags high state
	b(0x9c, 0x13, 0x03, 0x9c, 0x1d, 0x03, 0x9c, 0x1e, 0x03)                                     // help return mode, editor mode, menu selection
	b(0x9c, 0x14, 0x03, 0x9c, 0x1a, 0x03, 0x9c, 0x1b, 0x03)                                     // settings return/selection, Save+Recovery mode
	b(0xa9, 0x01, 0x8d, 0x1c, 0x03, 0xa9, 0x05, 0x8d, 0x2b, 0x03)                               // one minute, five retained copies
	b(0x9c, 0x2e, 0x03, 0x9c, 0x2f, 0x03, 0x9c, 0x68, 0x03, 0x9c, 0x69, 0x03, 0x9c, 0x6a, 0x03) // rendered Markdown, ODT, no filter/current format/transition Save
	b(0x9c, 0x1f, 0x03, 0x9c, 0x20, 0x03, 0x9c, 0x31, 0x03)                                     // browser count/selection and filename length
	b(0x9c, 0x32, 0x03)                                                                         // find query length
	b(0x9c, 0x6d, 0x03)                                                                         // filename dialog focus: name/save/cancel
	b(0xa9, 128, 0x8d, 0x34, 0x03, 0xa9, 112, 0x8d, 0x35, 0x03, 0x9c, 0x36, 0x03)               // SNES mouse pointer and previous left button
	b(0xa9, scrollThumbTrackStart, 0x8d, 0x52, 0x03, 0xa9, 0x01, 0x8d, 0x53, 0x03)              // document-position thumb starts dirty at track origin
	b(0xa9, 0xff, 0x8d, 0x3c, 0x03, 0x8d, 0x3d, 0x03)                                           // no sticky vertical column yet ($033c/$033d)
	// Versioned 32 KiB SRAM mailbox header. The host and cartridge share only
	// committed indices here; payload regions begin at the fixed offsets in
	// src/mailbox.h and are never treated as an authoritative document copy.
	b(0xc2, 0x20)                               // REP #$20: 16-bit accumulator for mailbox init
	b(0xa9, 0x01, 0x00, 0x8f, 0x00, 0x00, 0x70) // protocol = 1
	b(0xa9, 0x00, 0x00, 0x8f, 0x02, 0x00, 0x70) // command producer index
	b(0x8f, 0x04, 0x00, 0x70)                   // command consumer index
	b(0x8f, 0x06, 0x00, 0x70)                   // event producer index
	b(0x8f, 0x08, 0x00, 0x70)                   // event consumer index
	b(0xe2, 0x20)                               // SEP #$20: 8-bit accumulator
	initialRenderCall := len(p)
	b(0x20, 0, 0)
	loop := len(p)
	// Refresh the mouse pointer whenever a spin lands in vblank, and flush a
	// document-thumb position only when a viewport marked it dirty. The pointer
	// tracks $0334/$0335 while idle; the thumb position/dirty byte live at
	// $0352/$0353. Gating on vblank avoids active-display OAM writes.
	b(0xad, 0x12, 0x42) // LDA $4212
	bplPointerSkip := len(p)
	b(0x10, 0) // BPL afterPointer (not in vblank)
	ldaSta(0x00, 0x2102)
	b(0x9c, 0x03, 0x21)                   // STZ $2103
	b(0xad, 0x34, 0x03, 0x8d, 0x04, 0x21) // x = $0334
	b(0xad, 0x35, 0x03, 0x8d, 0x04, 0x21) // y = $0335
	b(0xa9, 0x00, 0x8d, 0x04, 0x21)       // tile 0
	b(0xa9, 0x30, 0x8d, 0x04, 0x21)       // attr: OBJ priority 3, in front of BG1
	b(0xad, 0x53, 0x03)
	beqAfterThumb := len(p)
	b(0xf0, 0)                            // OAM persists; only rewrite sprite 1 after a viewport change
	b(0xad, 0x52, 0x03, 0x8d, 0x04, 0x21) // sprite 1 x = document-position thumb
	b(0xa9, scrollThumbY, 0x8d, 0x04, 0x21)
	b(0xa9, 0x01, 0x8d, 0x04, 0x21) // tile 1
	b(0xa9, 0x30, 0x8d, 0x04, 0x21) // attr: OBJ priority 3, in front of BG1
	b(0x9c, 0x53, 0x03)             // dirty = 0
	afterThumb := len(p)
	branch(beqAfterThumb, afterThumb)
	afterPointer := len(p)
	branch(bplPointerSkip, afterPointer)
	viewportCall := len(p)
	b(0x20, 0, 0) // consume the host viewport before processing input
	eventCall := len(p)
	b(0x20, 0, 0) // consume one committed host event without blocking input
	commandSpaceCall := len(p)
	b(0x20, 0, 0, 0xb0, 0x03) // preserve XBAND bytes until the host ring has room
	jmpTo(loop)
	mousePollCall := len(p)
	b(0x20, 0, 0) // controller-port-1 SNES Mouse; returns semantic Enter on a new left click
	b(0xf0, 0x03)
	mouseKeyJump := len(p)
	b(0x4c, 0, 0)
	pollCall := len(p)
	b(0x20, 0, 0) // JSR xband_poll
	bneKey := len(p)
	b(0xd0, 0)
	b(0xaf, 0x01, 0x00, 0x70) // LDA.l $700001 sequence
	b(0xcf, 0x0c, 0x00, 0x70) // CMP.l $70000c fallback ack (not mailbox producer)
	beq := len(p)
	b(0xf0, 0)
	b(0xaf, 0x00, 0x00, 0x70) // fallback ASCII
	key := len(p)
	patch16(mouseKeyJump, key)
	// The pending key code lives at $5b, not $01. $00/$01 is the 16-bit guest
	// cursor -- the draw loop matches the caret with a 16-bit `CPX $00` and the
	// insert path advances it with a 16-bit `INC $00` -- so parking the key code
	// in $01 overwrote the cursor's high byte on every keystroke. The guest
	// cursor became `position + keyCode*256` until the host's next viewport
	// commit reset it, which put the optimistic caret far outside the document
	// (measured: cursor 102, then Right left $00=103 with $01=18, i.e. 4711).
	b(0x85, 0x5b) // key -> $5b
	branch(bneKey, key)
	// Snapshot the caret's on-screen cell and column before the local optimistic
	// edit runs. editorDispatch moves the guest cursor first and commandEnqueue
	// only runs afterwards, so by the time wrap-aware Up/Down is reached the
	// live caret registers already describe where the *local* handler moved to,
	// not where the user actually pressed the key. Vertical movement resolves
	// against this snapshot instead.
	b(0xc2, 0x20)       // REP #$20 (16-bit A)
	b(0xa5, 0x0b)       // LDA $0b (caret cell)
	b(0x8d, 0x65, 0x03) // STA $0365
	b(0xad, 0x5c, 0x03) // LDA $035c (caret column)
	b(0x8d, 0x67, 0x03) // STA $0367
	b(0xe2, 0x20)       // SEP #$20
	editCall := len(p)
	b(0x20, 0, 0)
	commandEnqueueCall := len(p)
	b(0x20, 0, 0)
	b(0xaf, 0x01, 0x00, 0x70, 0x8f, 0x0c, 0x00, 0x70)
	// Absolute JMP, not a relative BRA. This closes the main loop across the
	// whole key/mouse dispatch, and it was previously emitted as
	// `b(0x80, byte(int8(loop-(back+2))))` -- a hand-computed 8-bit displacement
	// that silently wrapped instead of failing once the body grew past -128,
	// which lands the loop in the middle of an instruction. The branch helper
	// used everywhere else would have caught it; a JMP removes the limit outright.
	jmpTo(loop)
	branch(beq, loop)

	// xband_poll: receive a complete XBAND scancode batch into $0100-$010f,
	// then return one make-code translation per call. Queue bytes are consumed
	// before the peripheral is polled again, so E0/F0 multi-byte events cannot
	// be torn across transactions.
	poll := len(p)
	patch16(pollCall, poll)
	b(0xa5, 0x05) // LDA queue_count
	bneDequeue := len(p)
	b(0xd0, 0)
	ldaSta(0x7f, 0x4201) // IOBIT low starts keyboard transaction
	get8Call1 := len(p)
	b(0x20, 0, 0)
	b(0xc9, 0x78) // XBAND keyboard device ID
	b(0xf0, 0x03) // BEQ present; otherwise use an absolute jump
	absentJump1 := len(p)
	b(0x4c, 0, 0)
	ldaSta(0xff, 0x4201)
	get4Call := len(p)
	b(0x20, 0, 0)
	b(0x29, 0x0f, 0x85, 0x05, 0x64, 0x06) // count, queue_index=0
	b(0xd0, 0x03)                         // BNE batch present; otherwise use an absolute jump
	absentJump2 := len(p)
	b(0x4c, 0, 0)
	b(0xa2, 0x00) // LDX #0 (8-bit index)
	readBatch := len(p)
	b(0xda) // PHX: getbits uses X as its two-bit group counter
	get8Call2 := len(p)
	b(0x20, 0, 0)
	b(0xfa, 0x9d, 0x00, 0x01, 0xe8, 0xe4, 0x05) // PLX; store; advance
	bccBatch := len(p)
	b(0x90, 0)
	branch(bccBatch, readBatch)
	ldaSta(0xff, 0x4201)
	dequeue := len(p)
	branch(bneDequeue, dequeue)
	b(0xa6, 0x06, 0xbd, 0x00, 0x01, 0xe6, 0x06, 0xc6, 0x05) // pop queue
	b(0xc9, 0xe0)
	bneNotExtended := len(p)
	b(0xd0, 0)
	b(0xa9, 0x01, 0x85, 0x02, 0x64, 0x03, 0xa9, 0x00, 0x60)
	notExtended := len(p)
	branch(bneNotExtended, notExtended)
	b(0xc9, 0xf0)
	bneNotRelease := len(p)
	b(0xd0, 0)
	b(0xa9, 0x01, 0x85, 0x03, 0xa9, 0x00, 0x60)
	notRelease := len(p)
	branch(bneNotRelease, notRelease)
	b(0xa6, 0x03) // release prefix pending?
	beqMake := len(p)
	b(0xf0, 0)
	b(0x64, 0x02, 0x64, 0x03) // consume extended/release prefixes
	b(0xc9, 0x12)             // release either Shift key
	beqClearShift := len(p)
	b(0xf0, 0)
	b(0xc9, 0x59)
	bneReleaseDone := len(p)
	b(0xd0, 0)
	clearShift := len(p)
	branch(beqClearShift, clearShift)
	b(0x64, 0x04)
	releaseDone := len(p)
	branch(bneReleaseDone, releaseDone)
	b(0xa9, 0x00, 0x60) // all break codes are state updates, never text
	makeCode := len(p)
	branch(beqMake, makeCode)
	b(0xc9, 0x58) // Caps Lock make toggles persistent case state.
	bneCapsMake := len(p)
	b(0xd0, 0)
	b(0xad, 0x0f, 0x03, 0x49, 0x01, 0x8d, 0x0f, 0x03, 0x85, 0x04, 0xa9, 0x00, 0x60)
	capsMakeDone := len(p)
	branch(bneCapsMake, capsMakeDone)
	b(0xa6, 0x02) // extended make codes are cartridge-owned cursor commands
	beqNormal := len(p)
	b(0xf0, 0)
	b(0x64, 0x02)
	b(0xc9, 0x6b) // Left
	bneExtDown := len(p)
	b(0xd0, 0)
	b(0xa9, 0x11, 0x60)
	extDown := len(p)
	branch(bneExtDown, extDown)
	b(0xc9, 0x72) // Down
	bneExtRight := len(p)
	b(0xd0, 0)
	b(0xa9, 0x14, 0x60)
	extRight := len(p)
	branch(bneExtRight, extRight)
	b(0xc9, 0x74) // Right
	bneExtUp := len(p)
	b(0xd0, 0)
	b(0xa9, 0x12, 0x60)
	extUp := len(p)
	branch(bneExtUp, extUp)
	b(0xc9, 0x75) // Up
	bneExtDeleteCheck := len(p)
	b(0xd0, 0)
	b(0xa9, 0x13, 0x60)
	extDeleteCheck := len(p)
	branch(bneExtDeleteCheck, extDeleteCheck)
	b(0xc9, 0x71) // Delete
	bneExtHomeCheck := len(p)
	b(0xd0, 0)
	b(0xa9, 0x15, 0x60)
	extHomeCheck := len(p)
	branch(bneExtHomeCheck, extHomeCheck)
	b(0xc9, 0x6c) // Home
	bneExtEndCheck := len(p)
	b(0xd0, 0)
	b(0xa9, 0x16, 0x60)
	extEndCheck := len(p)
	branch(bneExtEndCheck, extEndCheck)
	b(0xc9, 0x69) // End
	bneExtPageUpCheck := len(p)
	b(0xd0, 0)
	b(0xa9, 0x17, 0x60)
	extPageUpCheck := len(p)
	branch(bneExtPageUpCheck, extPageUpCheck)
	b(0xc9, 0x7d) // PageUp
	bneExtPageDownCheck := len(p)
	b(0xd0, 0)
	b(0xa9, 0x1a, 0x60)
	extPageDownCheck := len(p)
	branch(bneExtPageDownCheck, extPageDownCheck)
	b(0xc9, 0x7a) // PageDown
	bneUnknownExt := len(p)
	b(0xd0, 0)
	b(0xa9, 0x1b, 0x60)
	unknownExt := len(p)
	branch(bneUnknownExt, unknownExt)
	b(0xa9, 0x00, 0x60)
	normal := len(p)
	branch(beqNormal, normal)
	b(0xc9, 0x12) // Shift make
	beqShiftMake := len(p)
	b(0xf0, 0)
	b(0xc9, 0x59)
	beqShiftMake2 := len(p)
	b(0xf0, 0)
	b(0xaa, 0xbd, byte(loromAddr(scanMapOffset)), byte(loromAddr(scanMapOffset)>>8))
	b(0xa6, 0x04)
	beqNormalReturn := len(p)
	b(0xf0, 0)
	for _, shifted := range []struct{ unshifted, shifted byte }{
		{'-', '_'}, {'.', '>'}, {',', '<'}, {'/', '?'}, {'\'', '"'},
	} {
		b(0xc9, shifted.unshifted)
		bneShifted := len(p)
		b(0xd0, 0)
		b(0xa9, shifted.shifted, 0x60)
		shiftedDone := len(p)
		branch(bneShifted, shiftedDone)
	}
	b(0xc9, 'a')
	bccNormalReturn := len(p)
	b(0x90, 0)
	b(0xc9, 'z'+1)
	bcsNormalReturn := len(p)
	b(0xb0, 0)
	b(0x38, 0xe9, 0x20) // shifted ASCII letter -> uppercase tile/document byte
	normalReturn := len(p)
	branch(beqNormalReturn, normalReturn)
	branch(bccNormalReturn, normalReturn)
	branch(bcsNormalReturn, normalReturn)
	b(0xc9, 0x00, 0x60) // return flags describe the translated byte, not Shift state
	shiftMake := len(p)
	branch(beqShiftMake, shiftMake)
	branch(beqShiftMake2, shiftMake)
	b(0xa9, 0x01, 0x85, 0x04, 0xa9, 0x00, 0x60)
	absent := len(p)
	for _, at := range []int{absentJump1, absentJump2} {
		patch16(at, absent)
	}
	ldaSta(0xff, 0x4201)
	b(0xa9, 0x00, 0x60)

	// mouse_poll reads the real four-byte controller-port-1 packet. The packet
	// carries signed-magnitude Y/X deltas and button state; only the cartridge
	// maps a rising left button into its existing menu/browser Enter behavior.
	mousePoll := len(p)
	patch16(mousePollCall, mousePoll)
	b(0xa9, 0x01, 0x8d, 0x16, 0x40, 0x9c, 0x16, 0x40) // latch then shift
	b(0xa2, 0x00)                                     // X = packet byte
	mouseByte := len(p)
	b(0xa9, 0x00, 0x95, 0x20, 0xa0, 0x08) // packet[X] = 0; eight serial bits
	mouseBit := len(p)
	b(0xad, 0x16, 0x40, 0x4a, 0x36, 0x20, 0x88)
	bneMouseBit := len(p)
	b(0xd0, 0)
	b(0xe8, 0xe0, 0x04)
	bneMouseByte := len(p)
	b(0xd0, 0)
	branch(bneMouseBit, mouseBit)
	branch(bneMouseByte, mouseByte)
	b(0xa5, 0x20, 0x29, 0xf0)
	bneNoMouse := len(p)
	b(0xd0, 0)
	branch(bneNoMouse, absent)
	// Apply Y (packet byte 2) and X (byte 3), saturating to the 256x224 screen.
	b(0xa5, 0x22)
	bmiMouseYNegative := len(p)
	b(0x30, 0)
	b(0x29, 0x7f, 0x18, 0x6d, 0x35, 0x03, 0x90, 0x02, 0xa9, 223, 0xc9, 224, 0x90, 0x02, 0xa9, 223, 0x8d, 0x35, 0x03)
	jumpMouseYDone := len(p)
	b(0x4c, 0, 0)
	mouseYNegative := len(p)
	branch(bmiMouseYNegative, mouseYNegative)
	b(0x29, 0x7f, 0x85, 0x24, 0xad, 0x35, 0x03, 0x38, 0xe5, 0x24, 0xb0, 0x02, 0xa9, 0x00, 0x8d, 0x35, 0x03)
	mouseYDone := len(p)
	jump(jumpMouseYDone, mouseYDone)
	b(0xa5, 0x23)
	bmiMouseXNegative := len(p)
	b(0x30, 0)
	b(0x29, 0x7f, 0x18, 0x6d, 0x34, 0x03, 0x90, 0x02, 0xa9, 255, 0x8d, 0x34, 0x03)
	jumpMouseXDone := len(p)
	b(0x4c, 0, 0)
	mouseXNegative := len(p)
	branch(bmiMouseXNegative, mouseXNegative)
	b(0x29, 0x7f, 0x85, 0x24, 0xad, 0x34, 0x03, 0x38, 0xe5, 0x24, 0xb0, 0x02, 0xa9, 0x00, 0x8d, 0x34, 0x03)
	mouseXDone := len(p)
	jump(jumpMouseXDone, mouseXDone)
	// Require a rising edge, then derive the visible row directly from Y.
	// The toolbar-click check between here and `mouseRelease` made this a
	// long branch, so it uses the standard inverted-BNE-over-JMP trampoline.
	b(0xa5, 0x21, 0x29, 0x40)
	bneMouseRisingEdge := len(p)
	b(0xd0, 0)
	jmpMouseReleaseAt := len(p)
	b(0x4c, 0, 0) // JMP mouseRelease (patched once the label is known)
	mouseRisingEdge := len(p)
	branch(bneMouseRisingEdge, mouseRisingEdge)
	b(0xad, 0x36, 0x03)
	b(0xf0, 0x03) // BEQ skips the held-button trampoline.
	jmpPointerHeldAt := len(p)
	b(0x4c, 0, 0)
	b(0xa9, 0x01, 0x8d, 0x36, 0x03, 0xad, 0x1d, 0x03)
	// Filename mode owns two large contextual buttons in the former formatting
	// card and a field in the document panel. A click changes the same focus
	// byte keyboard navigation uses, then returns Enter for either action or a
	// private refresh key for the field. Save and Cancel therefore have one
	// activation path regardless of whether the user clicked or navigated.
	b(0xc9, 0x0a)
	bneMouseNotFilename := len(p)
	b(0xd0, 0)
	b(0xad, 0x35, 0x03) // LDA $0335 (mouse Y)
	b(0xc9, 8)
	bccMouseFilenameField := len(p)
	b(0x90, 0)
	b(0xc9, 24)
	bcsMouseFilenameField := len(p)
	b(0xb0, 0)
	b(0xad, 0x34, 0x03) // LDA $0334 (mouse X)
	b(0xc9, 160)
	bccMouseFilenameDone := len(p)
	b(0x90, 0)
	b(0xc9, 248)
	bcsMouseFilenameDone := len(p)
	b(0xb0, 0)
	b(0xad, 0x35, 0x03, 0xc9, 16)
	bccMouseFilenameSave := len(p)
	b(0x90, 0)
	b(0xa9, 0x02, 0x8d, 0x6d, 0x03, 0xa9, 0x0d, 0x60) // Cancel focus, then Enter.
	mouseFilenameSave := len(p)
	branch(bccMouseFilenameSave, mouseFilenameSave)
	b(0xa9, 0x01, 0x8d, 0x6d, 0x03, 0xa9, 0x0d, 0x60) // Save focus, then Enter.
	mouseFilenameField := len(p)
	branch(bccMouseFilenameField, mouseFilenameField)
	branch(bcsMouseFilenameField, mouseFilenameField)
	b(0xad, 0x35, 0x03, 0xc9, 104)
	bccMouseFilenameDone2 := len(p)
	b(0x90, 0)
	b(0xc9, 128)
	bcsMouseFilenameDone2 := len(p)
	b(0xb0, 0)
	b(0xad, 0x34, 0x03, 0xc9, 24)
	bccMouseFilenameDone3 := len(p)
	b(0x90, 0)
	b(0xc9, 232)
	bcsMouseFilenameDone3 := len(p)
	b(0xb0, 0)
	b(0x9c, 0x6d, 0x03, 0xa9, 0x1e, 0x60) // Name focus, then refresh.
	mouseFilenameDone := len(p)
	branch(bccMouseFilenameDone, mouseFilenameDone)
	branch(bcsMouseFilenameDone, mouseFilenameDone)
	branch(bccMouseFilenameDone2, mouseFilenameDone)
	branch(bcsMouseFilenameDone2, mouseFilenameDone)
	branch(bccMouseFilenameDone3, mouseFilenameDone)
	branch(bcsMouseFilenameDone3, mouseFilenameDone)
	b(0xa9, 0x00, 0x60)
	mouseNotFilename := len(p)
	branch(bneMouseNotFilename, mouseNotFilename)
	b(0xc9, 0x01)
	// The document-body, toolbar, scroll-track and browser blocks now sit between
	// here and `mouseMenu`, so this uses the standard inverted-BNE-over-JMP
	// trampoline rather than a relative branch that no longer reaches.
	b(0xd0, 0x03) // BNE skips the jump
	jmpMouseMenuAt := len(p)
	b(0x4c, 0, 0) // JMP mouseMenu (patched once the label is known)
	// Mode 0 is the document body: a click resolves to a caret position rather
	// than a menu/browser row, and returns no key so it is not treated as Enter.
	b(0xc9, 0x00) // CMP #0 (document mode?)
	bneNotDocClick := len(p)
	b(0xd0, 0) // BNE notDocClick
	// The toolbar card (bold/italic/underline + alignment buttons) occupies
	// screen pixels x=160..247, y=8..23 -- see the "Upload the 2x11 toolbar
	// plane" comment for the VRAM/WRAM layout this overlays. A click there
	// takes priority over the document body even in mode 0.
	b(0xad, 0x35, 0x03) // LDA $0335 (mouse Y)
	b(0xc9, 8)
	bccNotToolbarClick := len(p)
	b(0x90, 0)
	b(0xc9, 24)
	bcsNotToolbarClick := len(p)
	b(0xb0, 0)
	b(0xad, 0x34, 0x03) // LDA $0334 (mouse X)
	b(0xc9, 160)
	bccNotToolbarClick2 := len(p)
	b(0x90, 0)
	b(0xc9, 248)
	bcsNotToolbarClick2 := len(p)
	b(0xb0, 0)
	toolbarClickCall := len(p)
	b(0x20, 0, 0)       // JSR toolbarClick
	b(0xa9, 0x00, 0x60) // LDA #0; RTS (no key)
	notToolbarClick := len(p)
	branch(bccNotToolbarClick, notToolbarClick)
	branch(bcsNotToolbarClick, notToolbarClick)
	branch(bccNotToolbarClick2, notToolbarClick)
	branch(bcsNotToolbarClick2, notToolbarClick)
	// The document-position track sits directly above the document body: the
	// thumb sprite rides at y=scrollThumbY over a track running from
	// scrollThumbTrackStart for scrollThumbTrackWidth pixels. A press anywhere on
	// it scrolls the view to that point and arms a scroll drag, so the thumb does
	// not have to be hit precisely -- at 8 pixels wide it would be a poor target.
	b(0xad, 0x35, 0x03) // LDA $0335 (mouse Y)
	b(0xc9, scrollThumbY)
	bccNotTrackClick := len(p)
	b(0x90, 0)
	b(0xc9, scrollThumbY+8)
	bcsNotTrackClick := len(p)
	b(0xb0, 0)
	b(0xa9, 0x01, 0x8d, 0x33, 0x03) // LDA #1; STA $0333 (scroll drag armed)
	b(0xa9, 0xff, 0x8d, 0x63, 0x03) // LDA #$ff; STA $0363 -- no thumb value matches, so a press always publishes
	scrollTrackClickCall := len(p)
	b(0x20, 0, 0)       // JSR scrollTrackDrag
	b(0xa9, 0x00, 0x60) // LDA #0; RTS (no key)
	notTrackClick := len(p)
	branch(bccNotTrackClick, notTrackClick)
	branch(bcsNotTrackClick, notTrackClick)
	b(0x9c, 0x33, 0x03) // STZ $0333 (a document-body press is not a scroll drag)
	pointerClickCall := len(p)
	b(0x20, 0, 0)       // JSR pointerClick
	b(0xa9, 0x00, 0x60) // LDA #0; RTS (no key)
	// A held left button is either a scroll drag or a text selection drag,
	// decided by where the press landed. Tracking that in $0333 is what lets the
	// pointer leave the thin track while dragging without turning into a
	// selection, which is how every real scrollbar behaves.
	pointerHeld := len(p)
	b(0xad, 0x1d, 0x03) // LDA $031d (mode)
	b(0xc9, 0x00)       // CMP #0
	bneSkipDrag := len(p)
	b(0xd0, 0)          // BNE skipDrag
	b(0xad, 0x33, 0x03) // LDA $0333 (scroll drag armed?)
	beqNotScrollDrag := len(p)
	b(0xf0, 0)
	scrollTrackDragCall := len(p)
	b(0x20, 0, 0)       // JSR scrollTrackDrag
	b(0xa9, 0x00, 0x60) // LDA #0; RTS (no key)
	notScrollDrag := len(p)
	branch(beqNotScrollDrag, notScrollDrag)
	pointerDragCall := len(p)
	b(0x20, 0, 0) // JSR pointerDrag
	skipDrag := len(p)
	branch(bneSkipDrag, skipDrag)
	b(0xa9, 0x00, 0x60) // LDA #0; RTS (no key)
	notDocClick := len(p)
	branch(bneNotDocClick, notDocClick)
	b(0xc9, 0x05)
	bccMouseDone := len(p)
	b(0x90, 0)
	b(0xc9, 0x08)
	bcsMouseDone := len(p)
	b(0xb0, 0)
	// Browser entries are rows 1..7, capped by the host-published count.
	b(0xad, 0x35, 0x03, 0x4a, 0x4a, 0x4a, 0x38, 0xe9, 0x01)
	bcsMouseBrowserRow := len(p)
	b(0xb0, 0)
	b(0xa9, 0x00)
	mouseBrowserRow := len(p)
	branch(bcsMouseBrowserRow, mouseBrowserRow)
	b(0xcd, 0x1f, 0x03)
	bccMouseBrowserStore := len(p)
	b(0x90, 0)
	b(0xad, 0x1f, 0x03)
	beqMouseDone2 := len(p)
	b(0xf0, 0)
	b(0x3a)
	mouseBrowserStore := len(p)
	branch(bccMouseBrowserStore, mouseBrowserStore)
	b(0x8d, 0x20, 0x03, 0xa9, 0x0d, 0x60)
	// Main menu has seven fixed rows.
	mouseMenu := len(p)
	jump(jmpMouseMenuAt, mouseMenu)
	b(0xad, 0x35, 0x03, 0x4a, 0x4a, 0x4a, 0x38, 0xe9, 0x01)
	bcsMouseMenuRow := len(p)
	b(0xb0, 0)
	b(0xa9, 0x00)
	mouseMenuRow := len(p)
	branch(bcsMouseMenuRow, mouseMenuRow)
	b(0xc9, 0x07)
	bccMouseMenuStore := len(p)
	b(0x90, 0)
	b(0xa9, 0x06)
	mouseMenuStore := len(p)
	branch(bccMouseMenuStore, mouseMenuStore)
	b(0x8d, 0x1e, 0x03, 0xa9, 0x0d, 0x60)
	mouseRelease := len(p)
	jump(jmpMouseReleaseAt, mouseRelease)
	b(0x9c, 0x36, 0x03)
	b(0x9c, 0x33, 0x03) // releasing ends any scroll drag
	mouseDone := len(p)
	jump(jmpPointerHeldAt, pointerHeld)
	branch(bccMouseDone, mouseDone)
	branch(bcsMouseDone, mouseDone)
	branch(beqMouseDone2, mouseDone)
	b(0xa9, 0x00, 0x60)

	// pointer_click maps the resident pointer to a document caret. It converts
	// the pointer pixel into a document (column,row), replays the real layout by
	// re-rendering with the hit-test active, then publishes the clicked cell's
	// viewport-relative UTF-16 offset as CommandPointerSetCursor. The host adds
	// the viewport start and owns the authoritative caret; the cartridge only
	// resolves pixels to a cell.
	pointerClick := len(p) // press entry (left rising edge)
	patch16(pointerClickCall, pointerClick)
	// $FFFF is not a reachable cell, so a press always passes the drag
	// repeat-suppression check below.
	b(0xc2, 0x20)                   // REP #$20
	b(0xa9, 0xff, 0xff)             // LDA #$FFFF
	b(0x8d, 0x50, 0x03)             // STA $0350 (last emitted cell)
	b(0xe2, 0x20)                   // SEP #$20
	b(0xa9, 0x04, 0x8d, 0x44, 0x03) // LDA #$04; STA $0344 (kind low = SetCursor)
	braResolve := len(p)
	b(0x80, 0)            // BRA pointerResolve
	pointerDrag := len(p) // drag entry (left held over the body)
	patch16(pointerDragCall, pointerDrag)
	b(0xa9, 0x0f, 0x8d, 0x44, 0x03) // LDA #$0f; STA $033e (kind low = ExtendCursor)
	pointerResolve := len(p)
	branch(braResolve, pointerResolve)
	// column = (pointerX - 8) / 8, rejected outside the 30-cell document width.
	b(0xad, 0x34, 0x03) // LDA $0334
	b(0x38, 0xe9, 0x08) // SEC; SBC #8
	bccPtrOut1 := len(p)
	b(0x90, 0)          // BCC ptrOut
	b(0x4a, 0x4a, 0x4a) // LSR x3
	b(0xc9, 0x1e)       // CMP #30
	bcsPtrOut1 := len(p)
	b(0xb0, 0)          // BCS ptrOut
	b(0x8d, 0x46, 0x03) // STA $0346 (column, low)
	b(0x9c, 0x47, 0x03) // STZ $0347 (high -- kept 0 so the 16-bit add below is safe)
	// row = (pointerY - 80) / 8, across the full document height.
	//
	// This bound was #8, left over from when the document plane really was 8
	// rows. The plane has been 30x17 (510 cells) for a while -- it DMAs 17 rows
	// (`LDX #$11`) to tilemap row 10, i.e. screen y 80..215 -- so clicking
	// anywhere below the eighth row silently did nothing. The row arithmetic
	// below is 16-bit for the same reason: row*30 exceeds 255 from row 9 on.
	b(0xad, 0x35, 0x03) // LDA $0335
	b(0x38, 0xe9, 0x50) // SEC; SBC #80
	bccPtrOut2 := len(p)
	b(0x90, 0)          // BCC ptrOut
	b(0x4a, 0x4a, 0x4a) // LSR x3
	b(0xc9, 0x11)       // CMP #17
	bcsPtrOut2 := len(p)
	b(0xb0, 0) // BCS ptrOut
	// targetCell = row*30 + column (monotonic output position used by the hit-test).
	b(0xaa)             // TAX (X = row)
	b(0xc2, 0x20)       // REP #$20 (16-bit A: row*30 reaches 480)
	b(0xa9, 0x00, 0x00) // LDA #0
	mulLoop := len(p)
	b(0xe0, 0x00) // CPX #0 (8-bit index here)
	beqMulDone := len(p)
	b(0xf0, 0)                // BEQ mulDone
	b(0x18, 0x69, 0x1e, 0x00) // CLC; ADC #30
	b(0xca)                   // DEX
	braMulLoop := len(p)
	b(0x80, 0) // BRA mulLoop
	mulDone := len(p)
	branch(beqMulDone, mulDone)
	branch(braMulLoop, mulLoop)
	b(0x18, 0x6d, 0x46, 0x03) // CLC; ADC $0346 (column)
	b(0x8d, 0x39, 0x03)       // STA $0339 (16-bit target cell -> $0339/$033a)
	// Suppress a repeat command for the same cell (a still-held drag). A press
	// sets $0350 to $FFFF so it always passes here. The compare is 16-bit: with
	// 510 reachable cells, two different cells can share a low byte.
	b(0xcd, 0x50, 0x03) // CMP $0350
	beqSameCell := len(p)
	b(0xf0, 0)    // BEQ ptrOut (same cell)
	b(0xe2, 0x20) // SEP #$20
	pointerResolveCall := len(p)
	b(0x20, 0, 0)       // JSR resolveCellCommand
	b(0xc2, 0x20)       // REP #$20
	b(0xad, 0x39, 0x03) // LDA $0339 (16-bit)
	b(0x8d, 0x50, 0x03) // STA $0350 (last emitted cell)
	b(0xe2, 0x20)       // SEP #$20
	braPtrOut := len(p)
	b(0x80, 0)
	// The same-cell early-out is reached with a 16-bit accumulator, so it has to
	// narrow back before returning.
	ptrSameCell := len(p)
	branch(beqSameCell, ptrSameCell)
	b(0xe2, 0x20) // SEP #$20
	ptrOut := len(p)
	branch(braPtrOut, ptrOut)
	branch(bccPtrOut1, ptrOut)
	branch(bcsPtrOut1, ptrOut)
	branch(bccPtrOut2, ptrOut)
	branch(bcsPtrOut2, ptrOut)
	b(0x60) // RTS

	// scrollTrackDrag turns a press or drag on the document-position track into a
	// view scroll. It publishes only the thumb's position along its travel and
	// lets the host decide what part of the document that is. The caret is not
	// touched: the user places it afterwards by clicking in the scrolled view,
	// which is also what ends the scroll and re-anchors the window on the caret.
	scrollTrackDrag := len(p)
	patch16(scrollTrackClickCall, scrollTrackDrag)
	patch16(scrollTrackDragCall, scrollTrackDrag)
	// thumb = pointerX - trackStart - thumbWidth/2, clamped to the travel, so the
	// thumb centres under the pointer rather than hanging off to its right.
	b(0xad, 0x34, 0x03)                                     // LDA $0334 (mouse X)
	b(0x38, 0xe9, scrollThumbTrackStart+scrollThumbWidth/2) // SEC; SBC #(trackStart + half thumb)
	bccScrollAtStart := len(p)
	b(0x90, 0) // BCC scrollAtStart (left of the track)
	b(0xc9, scrollThumbTravel+1)
	bccScrollHave := len(p)
	b(0x90, 0)                 // BCC scrollHave (inside the travel)
	b(0xa9, scrollThumbTravel) // past the right end
	braScrollHave := len(p)
	b(0x80, 0)
	scrollAtStart := len(p)
	branch(bccScrollAtStart, scrollAtStart)
	b(0xa9, 0x00)
	scrollHave := len(p)
	branch(bccScrollHave, scrollHave)
	branch(braScrollHave, scrollHave)
	// Publish only when the thumb actually moved. A held button otherwise floods
	// the command ring with an identical scroll every frame.
	b(0xcd, 0x63, 0x03) // CMP $0363 (last published thumb)
	beqScrollDone := len(p)
	b(0xf0, 0)
	b(0x8d, 0x63, 0x03) // STA $0363
	b(0x8d, 0x00, 0x18) // STA $1800 (payload[0] = thumb)
	b(0x9c, 0x01, 0x18) // STZ $1801 (payload[1]; the travel is 226, so the high byte is always 0)
	// Move the sprite immediately so the drag feels attached to the pointer. The
	// host's next viewport republishes it from real byte offsets regardless.
	b(0x18, 0x69, scrollThumbTrackStart, 0x8d, 0x52, 0x03) // CLC; ADC #trackStart; STA $0352
	b(0xa9, 0x01, 0x8d, 0x53, 0x03)                        // mark the thumb dirty
	b(0xa9, 0x10, 0x8d, 0x16, 0x03)                        // kind low = $10 (CommandScrollToFraction)
	b(0xa9, 0x01, 0x8d, 0x17, 0x03)                        // kind high = $01
	b(0xa9, 0x02, 0x8d, 0x15, 0x03)                        // payload count low = 2
	b(0x9c, 0x18, 0x03)                                    // payload count high = 0
	b(0x9c, 0x19, 0x03)                                    // flags = 0
	scrollCommandCall := len(p)
	b(0x20, 0, 0) // JSR commandWrite
	scrollDone := len(p)
	branch(beqScrollDone, scrollDone)
	b(0x60) // RTS

	// resolveCellCommand turns a target document cell into a real caret command.
	// It is the single publish path shared by the mouse pointer and by
	// wrap-aware Up/Down: both only have to decide *which* cell they want, then
	// let the cartridge's own layout resolve it.
	//
	// Input:  $0339/$033a = 16-bit target cell, $0344 = command kind low byte
	//         (0x04 SetCursor / 0x0f ExtendCursor).
	// Output: $033b nonzero if a command was actually published.
	//
	// The re-render with $0340 raised runs the hit-test, which records the
	// document character index drawn at the greatest output position not past
	// the target -- so a target beyond the end of a short line naturally clamps
	// back onto that line's last character.
	//
	// NOTE: the offset lookup reads $0700/$0900, the per-character UTF-16 offset
	// tables the viewport decode actually fills. It used to read $0400, which
	// nothing in the ROM ever writes, so every click published offset 0 and the
	// caret jumped to the start of the viewport no matter where you clicked. The
	// high byte was discarded too (`STZ $1801`), which would have capped
	// addressable offsets at 255 even once the base was right.
	resolveCellCommand := len(p)
	patch16(pointerResolveCall, resolveCellCommand)
	// Bring the target back into layout space first. Centre and right aligned
	// rows are drawn translated right within their plane row, while the hit-test
	// below walks the layout flush left, so the cell the user actually pointed at
	// is not the cell the layout will report. Both callers -- the pointer and
	// wrap-aware Up/Down -- work in visible cells, so this is the one place that
	// has to know.
	b(0xc2, 0x20)                   // REP #$20 (16-bit A)
	b(0xad, 0x39, 0x03)             // LDA $0339 (target cell)
	b(0x8d, 0x22, 0x0f)             // STA $0f22 -- the visible target, restored below
	b(0xa2, 0x00)                   // LDX #0 (row; index registers are 8-bit here)
	unshiftRow := len(p)
	b(0xc9, 0x1e, 0x00) // CMP #30
	bccUnshiftHaveRow := len(p)
	b(0x90, 0)
	b(0x38, 0xe9, 0x1e, 0x00) // SEC; SBC #30
	b(0xe8)                   // INX
	braUnshiftRow := len(p)
	b(0x80, 0)
	unshiftHaveRow := len(p)
	branch(bccUnshiftHaveRow, unshiftHaveRow)
	branch(braUnshiftRow, unshiftRow)
	b(0xe2, 0x20)       // SEP #$20 (8-bit A; A is the column within the row)
	b(0xdd, 0x00, 0x0f) // CMP $0f00,X (this row's shift)
	bccUnshiftClamp := len(p)
	b(0x90, 0)          // BCC: pointed left of the shifted text
	b(0xbd, 0x00, 0x0f) // LDA $0f00,X (take the whole shift back)
	braUnshiftApply := len(p)
	b(0x80, 0)
	unshiftClamp := len(p)
	branch(bccUnshiftClamp, unshiftClamp)
	// Left of the row's first character: give back only the column, so the
	// target lands on that character instead of before the row.
	unshiftApply := len(p)
	branch(braUnshiftApply, unshiftApply)
	b(0x8d, 0x1e, 0x0f, 0x9c, 0x1f, 0x0f) // STA $0f1e; STZ $0f1f
	b(0xc2, 0x20)                         // REP #$20
	b(0xad, 0x39, 0x03, 0x38, 0xed, 0x1e, 0x0f, 0x8d, 0x39, 0x03)
	b(0xe2, 0x20)                   // SEP #$20
	b(0xa9, 0x01, 0x8d, 0x40, 0x03) // LDA #1; STA $0340 (hit active)
	b(0x9c, 0x3b, 0x03)             // STZ $033b (found = 0)
	resolveRenderCall := len(p)
	b(0x20, 0, 0)       // JSR render (re-lays out and runs the hit-test)
	b(0x9c, 0x40, 0x03) // STZ $0340 (hit inactive)
	// Give the caller back the cell it asked for. The pointer keeps this value as
	// its "already published" key, so leaving the un-shifted one here would make a
	// held button re-publish an extend command on every frame over a shifted row.
	b(0xc2, 0x20, 0xad, 0x22, 0x0f, 0x8d, 0x39, 0x03, 0xe2, 0x20)
	b(0xad, 0x3b, 0x03) // LDA $033b (found?)
	beqResolveOut := len(p)
	b(0xf0, 0)                            // BEQ resolveOut
	b(0xc2, 0x10)                         // REP #$10 (16-bit index: character indices reach 509)
	b(0xae, 0x41, 0x03)                   // LDX $0341 (document character index at the hit cell)
	b(0xbd, 0x00, 0x07)                   // LDA $0700,X (viewport-relative UTF-16 offset, low)
	b(0x8d, 0x00, 0x18)                   // STA $1800 (payload[0])
	b(0xbd, 0x00, 0x09)                   // LDA $0900,X (offset, high)
	b(0x8d, 0x01, 0x18)                   // STA $1801 (payload[1])
	b(0xe2, 0x10)                         // SEP #$10
	b(0xad, 0x44, 0x03, 0x8d, 0x16, 0x03) // kind low from $0344 (SetCursor/ExtendCursor)
	b(0xa9, 0x01, 0x8d, 0x17, 0x03)       // kind high = 0x01
	b(0xa9, 0x02, 0x8d, 0x15, 0x03)       // payload count low = 2
	b(0x9c, 0x18, 0x03)                   // payload count high = 0
	b(0x9c, 0x19, 0x03)                   // flags = 0
	resolveCommandCall := len(p)
	b(0x20, 0, 0) // JSR commandWrite
	resolveOut := len(p)
	branch(beqResolveOut, resolveOut)
	b(0x60) // RTS

	// verticalMove implements wrap-aware Up/Down. The host's MoveUp/MoveDown walk
	// the *host* document's layout, which knows nothing about the cartridge's
	// 30-column wrap, so they cannot follow what is actually on screen. The
	// cartridge owns the layout, so it resolves the target cell itself and
	// publishes an absolute caret position through the same path the pointer uses.
	//
	// Input:  A = 0 for up, nonzero for down; shift state is read from $04.
	// Output: A = 0 when a command was published and the caller is done;
	//         A = 1 when the caller should fall back to the semantic
	//         MoveUp/MoveDown, which is what still scrolls the viewport at the
	//         top and bottom of the plane and when the caret is off screen.
	verticalMove := len(p)
	b(0xe2, 0x20)       // SEP #$20 (8-bit A)
	b(0x8d, 0x62, 0x03) // STA $0362 (direction)
	// $01FE is the caret sentinel for "this render pass never found the caret",
	// so there is no on-screen row to move from.
	b(0xad, 0x66, 0x03, 0xc9, 0x01) // LDA $0366 ; CMP #$01
	bneCellOk := len(p)
	b(0xd0, 0)
	b(0xad, 0x65, 0x03, 0xc9, 0xfe) // LDA $0365 ; CMP #$fe
	beqVertFallback := len(p)
	b(0xf0, 0)
	cellOk := len(p)
	branch(bneCellOk, cellOk)
	// Desired column: keep the column the vertical run started from when the
	// previous key was also a vertical move, otherwise adopt the caret's current
	// column. $ff means "no sticky column". This is what lets Up/Down pass
	// through a short line and come back out at the original column.
	b(0xad, 0x3d, 0x03, 0xc9, 0xff) // LDA $033d (previous sticky) ; CMP #$ff
	bneHaveSticky := len(p)
	b(0xd0, 0)
	b(0xad, 0x67, 0x03) // LDA $0367 (snapshot caret column)
	haveSticky := len(p)
	branch(bneHaveSticky, haveSticky)
	b(0x8d, 0x3c, 0x03) // STA $033c (sticky column, staged for the next key)
	b(0x8d, 0x5e, 0x03) // STA $035e (16-bit temp, low)
	b(0x9c, 0x5f, 0x03) // STZ $035f (high)
	// base = caretCell - caretColumn + desiredColumn: the start of the caret's
	// own row, shifted across to the desired column. No division by 30 is needed
	// because the render captured the column directly.
	b(0xc2, 0x20)             // REP #$20 (16-bit A)
	b(0xad, 0x65, 0x03)       // LDA $0365 (16-bit snapshot caret cell)
	b(0x38, 0xed, 0x67, 0x03) // SEC ; SBC $0367 (snapshot caret column)
	b(0x18, 0x6d, 0x5e, 0x03) // CLC ; ADC $035e (desired column)
	b(0x8d, 0x60, 0x03)       // STA $0360 (base)
	b(0xe2, 0x20)             // SEP #$20
	b(0xad, 0x62, 0x03)       // LDA $0362 (direction)
	bneGoDown := len(p)
	b(0xd0, 0)
	// Up from row 0 has no row above it on screen.
	b(0xc2, 0x20)                         // REP #$20
	b(0xad, 0x60, 0x03, 0xc9, 0x1e, 0x00) // LDA $0360 ; CMP #30
	bccVertFallbackRep := len(p)
	b(0x90, 0)
	b(0x38, 0xe9, 0x1e, 0x00) // SEC ; SBC #30
	braHaveTarget := len(p)
	b(0x80, 0)
	goDown := len(p)
	branch(bneGoDown, goDown)
	// Down past the last row of the 510-cell plane likewise has nowhere to go.
	b(0xc2, 0x20)                               // REP #$20
	b(0xad, 0x60, 0x03, 0x18, 0x69, 0x1e, 0x00) // LDA $0360 ; CLC ; ADC #30
	b(0xc9, 0xfe, 0x01)                         // CMP #$01FE
	bcsVertFallbackRep := len(p)
	b(0xb0, 0)
	haveTarget := len(p)
	branch(braHaveTarget, haveTarget)
	b(0x8d, 0x39, 0x03) // STA $0339 (16-bit target -> $0339/$033a)
	b(0xe2, 0x20)       // SEP #$20
	// Shift extends the selection rather than moving the caret, mirroring the
	// pointer's press/drag split.
	b(0xa5, 0x04) // LDA $04 (shift state)
	beqVertSet := len(p)
	b(0xf0, 0)
	b(0xa9, 0x0f) // LDA #$0f (ExtendCursor)
	braVertKind := len(p)
	b(0x80, 0)
	vertSet := len(p)
	branch(beqVertSet, vertSet)
	b(0xa9, 0x04) // LDA #$04 (SetCursor)
	vertKind := len(p)
	branch(braVertKind, vertKind)
	b(0x8d, 0x44, 0x03) // STA $0344 (command kind low)
	jsrTo(resolveCellCommand)
	b(0xad, 0x3b, 0x03) // LDA $033b (did it publish?)
	beqVertFallback2 := len(p)
	b(0xf0, 0)
	// Point the guest's own cursor at the character we actually resolved, so the
	// optimistic local frame matches the position just published rather than the
	// old +/-30-characters guess. 16-bit: the guest cursor is $00/$01 and the
	// resolved index reaches 509.
	b(0xc2, 0x20)       // REP #$20
	b(0xad, 0x41, 0x03) // LDA $0341 (resolved character index)
	b(0x85, 0x00)       // STA $00
	b(0xe2, 0x20)       // SEP #$20
	b(0xa9, 0x00, 0x60) // LDA #0 ; RTS (handled here)
	vertFallbackRep := len(p)
	branch(bccVertFallbackRep, vertFallbackRep)
	branch(bcsVertFallbackRep, vertFallbackRep)
	b(0xe2, 0x20) // SEP #$20 (the plane-bound checks above run with 16-bit A)
	vertFallback := len(p)
	branch(beqVertFallback, vertFallback)
	branch(beqVertFallback2, vertFallback)
	b(0xa9, 0x01, 0x60) // LDA #1 ; RTS (caller falls back to the semantic move)

	// editor_dispatch owns the bounded guest document at $0200. Cursor and
	// length are independent, so insertion/deletion work in the middle rather
	// than degenerating into an append-only tile demo.
	var insertCall, backspaceCall, deleteCall, homeCall, endCall, leftCall, rightCall, upCall, downCall int
	var menuInputCall, browserInputCall int
	viewportRenderCalls := []int{}
	browserRenderCalls := []int{}
	viewport := len(p)
	patch16(viewportCall, viewport)
	b(0xaf, 0x0a, 0x00, 0x70)       // LDA.l $70000a
	b(0xcf, 0x10, 0x03, 0x7e)       // CMP.l $7e0310
	b(0xd0, 0x01, 0x60)             // unchanged generation returns immediately
	b(0x8f, 0x10, 0x03, 0x7e)       // STA.l $7e0310
	b(0x8b, 0xa9, 0x7e, 0x48, 0xab) // PHB; LDA #0x7e; PHA; PLB
	// A committed host viewport is the save acknowledgement: it ends the
	// transient SAVING screen and the document redraw below replaces it.
	b(0xad, 0x1d, 0x03, 0xc9, 0x0c, 0xd0, 0x03, 0x9c, 0x1d, 0x03)
	scrollThumbCall := len(p)
	b(0x20, 0, 0)             // update document-position thumb from viewport metadata
	b(0xc2, 0x20)             // REP #$20
	b(0xaf, 0x20, 0x41, 0x70) // UTF-8 byte count in the fixed first viewport slot
	b(0xd0, 0x03)
	viewportEmptyJump := len(p)
	b(0x4c, 0, 0)
	b(0x85, 0x0e)                                                       // trusted host byte count is bounded to 510 before publication
	b(0xc2, 0x20)                                                       // REP #$20
	b(0xaf, 0x08, 0x41, 0x70, 0x38, 0xef, 0x14, 0x41, 0x70, 0x85, 0x40) // cursor UTF-16 units from viewport start
	b(0xaf, 0x0c, 0x41, 0x70, 0x38, 0xef, 0x14, 0x41, 0x70, 0x85, 0x42) // selection start from viewport start
	b(0xaf, 0x10, 0x41, 0x70, 0x38, 0xef, 0x14, 0x41, 0x70, 0x85, 0x44) // selection end from viewport start
	b(0x64, 0x00, 0x64, 0x46, 0x64, 0x50, 0x64, 0x52)                   // display cursor/progress/selection cells
	b(0xc2, 0x10)                                                       // REP #$10 (16-bit X/Y)
	b(0xa2, 0x00, 0x00, 0xa0, 0x00, 0x00)                               // input X, output Y
	b(0xe2, 0x20)                                                       // SEP #$20 (8-bit A)
	// Status bit 4 means the caret is not inside this window at all, which only
	// happens once the scrollbar has moved the view away from it. The relative
	// cursor computed above then matches no drawn character, and without this
	// flag the draw loop's end-of-text fallback would park a caret at the end of
	// the visible text instead of showing none.
	b(0xaf, 0x5b, 0x41, 0x70, 0x29, 0x10) // LDA.l $70415b ; AND #$10
	b(0x8d, 0x3e, 0x03)                   // STA $033e (caret hidden)
	viewportDecodedJumps := []int{}
	appendJumps := []int{}
	viewportDecode := len(p)
	b(0xe4, 0x0e, 0x90, 0x03) // input byte remains; otherwise jump to the completed plane
	viewportDecodedJumps = append(viewportDecodedJumps, len(p))
	b(0x4c, 0, 0)
	b(0xbf, 0x00, 0x43, 0x70) // UTF-8 lead byte after the fixed metadata header
	b(0xc9, 0x80, 0xb0, 0x03)
	asciiJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0xc0, 0xb0, 0x03)
	invalidJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0xe0, 0xb0, 0x03)
	twoByteJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0xf0, 0xb0, 0x03)
	threeByteJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0xf8, 0xb0, 0x03)
	fourByteJump := len(p)
	b(0x4c, 0, 0)
	invalidLeadJump := len(p)
	b(0x4c, 0, 0)

	// Every valid UTF-8 scalar becomes exactly one resident cartridge cell.
	// ASCII is direct, line-feed is the cartridge's carriage-return marker,
	// common typographic punctuation has a readable resident fallback, and all
	// other scalars become one deterministic '?' rather than several mojibake
	// continuation-byte tiles.
	asciiScalar := len(p)
	jump(asciiJump, asciiScalar)
	b(0xe8, 0xc9, 0x0a)
	bneAsciiReady := len(p)
	b(0xd0, 0)
	b(0xa9, 0x0d)
	asciiReady := len(p)
	branch(bneAsciiReady, asciiReady)
	b(0x85, 0x13, 0xa9, 0x01, 0x85, 0x12)
	appendJumps = append(appendJumps, len(p))
	b(0x4c, 0, 0)

	invalidScalar := len(p)
	jump(invalidJump, invalidScalar)
	jump(invalidLeadJump, invalidScalar)
	b(0xe8, 0xa9, '?', 0x85, 0x13, 0xa9, 0x01, 0x85, 0x12)
	appendJumps = append(appendJumps, len(p))
	b(0x4c, 0, 0)

	twoByteScalar := len(p)
	jump(twoByteJump, twoByteScalar)
	b(0x85, 0x14, 0xe8, 0xbf, 0x00, 0x43, 0x70, 0xe8)
	b(0xc9, 0xa0)
	bneTwoByteMissing := len(p)
	b(0xd0, 0)
	b(0xa5, 0x14, 0xc9, 0xc2)
	bneTwoByteMissing2 := len(p)
	b(0xd0, 0)
	b(0xa9, 0x20)
	braTwoByteReady := len(p)
	b(0x80, 0)
	twoByteMissing := len(p)
	branch(bneTwoByteMissing, twoByteMissing)
	branch(bneTwoByteMissing2, twoByteMissing)
	b(0xa9, '?')
	twoByteReady := len(p)
	branch(braTwoByteReady, twoByteReady)
	b(0x85, 0x13, 0xa9, 0x01, 0x85, 0x12)
	appendJumps = append(appendJumps, len(p))
	b(0x4c, 0, 0)

	threeByteScalar := len(p)
	jump(threeByteJump, threeByteScalar)
	b(0x85, 0x14, 0xe8, 0xbf, 0x00, 0x43, 0x70, 0x85, 0x15, 0xe8)
	b(0xbf, 0x00, 0x43, 0x70, 0x85, 0x16, 0xe8)
	b(0xa5, 0x14, 0xc9, 0xe2)
	bneThreeByteMissing := len(p)
	b(0xd0, 0)
	b(0xa5, 0x15, 0xc9, 0x80)
	bneThreeByteMissing2 := len(p)
	b(0xd0, 0)
	b(0xa5, 0x16, 0xc9, 0x98)
	beqSingleQuote := len(p)
	b(0xf0, 0)
	b(0xc9, 0x99)
	beqSingleQuote2 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x9c)
	beqDoubleQuote := len(p)
	b(0xf0, 0)
	b(0xc9, 0x9d)
	beqDoubleQuote2 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x93)
	beqDash := len(p)
	b(0xf0, 0)
	b(0xc9, 0x94)
	beqDash2 := len(p)
	b(0xf0, 0)
	b(0xc9, 0xa6)
	beqPeriod := len(p)
	b(0xf0, 0)
	b(0xc9, 0xa8)
	beqNewlineScalar := len(p)
	b(0xf0, 0)
	b(0xc9, 0xa9)
	beqNewlineScalar2 := len(p)
	b(0xf0, 0)
	threeByteMissing := len(p)
	branch(bneThreeByteMissing, threeByteMissing)
	branch(bneThreeByteMissing2, threeByteMissing)
	b(0xa9, '?')
	braThreeByteReady := len(p)
	b(0x80, 0)
	singleQuote := len(p)
	branch(beqSingleQuote, singleQuote)
	branch(beqSingleQuote2, singleQuote)
	b(0xa9, '\'')
	braSingleQuoteReady := len(p)
	b(0x80, 0)
	doubleQuote := len(p)
	branch(beqDoubleQuote, doubleQuote)
	branch(beqDoubleQuote2, doubleQuote)
	b(0xa9, '"')
	braDoubleQuoteReady := len(p)
	b(0x80, 0)
	dashScalar := len(p)
	branch(beqDash, dashScalar)
	branch(beqDash2, dashScalar)
	b(0xa9, '-')
	braDashReady := len(p)
	b(0x80, 0)
	periodScalar := len(p)
	branch(beqPeriod, periodScalar)
	b(0xa9, '.')
	braPeriodReady := len(p)
	b(0x80, 0)
	newlineScalar := len(p)
	branch(beqNewlineScalar, newlineScalar)
	branch(beqNewlineScalar2, newlineScalar)
	b(0xa9, 0x0d)
	threeByteReady := len(p)
	branch(braThreeByteReady, threeByteReady)
	branch(braSingleQuoteReady, threeByteReady)
	branch(braDoubleQuoteReady, threeByteReady)
	branch(braDashReady, threeByteReady)
	branch(braPeriodReady, threeByteReady)
	b(0x85, 0x13, 0xa9, 0x01, 0x85, 0x12)
	appendJumps = append(appendJumps, len(p))
	b(0x4c, 0, 0)

	fourByteScalar := len(p)
	jump(fourByteJump, fourByteScalar)
	b(0xe8, 0xe8, 0xe8, 0xe8, 0xa9, '?', 0x85, 0x13, 0xa9, 0x02, 0x85, 0x12)

	appendScalar := len(p)
	for _, at := range appendJumps {
		jump(at, appendScalar)
	}
	// Check cursor, selection start, and selection end against current unit offset $46
	b(0xc2, 0x20)             // REP #$20
	b(0xa5, 0x46, 0xc5, 0x40) // CMP $40
	bneViewportCursor := len(p)
	b(0xd0, 0)
	b(0x84, 0x00)
	viewportCursorDone := len(p)
	branch(bneViewportCursor, viewportCursorDone)
	b(0xa5, 0x46, 0xc5, 0x42)
	bneViewportSelectionStart := len(p)
	b(0xd0, 0)
	// $19/$1b are draw-loop scratch (word-wrap measurement) and title-render
	// scratch (resident title byte count) respectively, both reused within the
	// same commit/render pass, so selection bounds cannot survive there until
	// the draw loop reads them. $50/$52 (16-bit each, matching this routine's
	// 16-bit Y) are dedicated and unused elsewhere in the cartridge.
	b(0x84, 0x50)
	viewportSelectionStartDone := len(p)
	branch(bneViewportSelectionStart, viewportSelectionStartDone)
	b(0xa5, 0x46, 0xc5, 0x44)
	bneViewportSelectionEnd := len(p)
	b(0xd0, 0)
	b(0x84, 0x52)
	viewportSelectionEndDone := len(p)
	branch(bneViewportSelectionEnd, viewportSelectionEndDone)
	b(0xa5, 0x46)                                                 // LDA $46
	b(0xe2, 0x20)                                                 // SEP #$20
	b(0x99, 0x00, 0x07)                                           // STA $0700,Y
	b(0xeb, 0x99, 0x00, 0x09)                                     // XBA; STA $0900,Y
	b(0xa5, 0x13, 0x99, 0x00, 0x05)                               // LDA $13; STA $0500,Y
	b(0xc8)                                                       // INY
	b(0xc2, 0x20)                                                 // REP #$20
	b(0xa5, 0x12, 0x29, 0xff, 0x00, 0x18, 0x65, 0x46, 0x85, 0x46) // LDA $12; AND #$00FF; CLC; ADC $46; STA $46
	b(0x90, 0x02, 0xe6, 0x47)                                     // BCC +2; INC $47
	b(0xe2, 0x20)                                                 // SEP #$20
	b(0xc0, 0xfe, 0x01, 0x90, 0x03)                               // CPY #510
	viewportDecodedJumps = append(viewportDecodedJumps, len(p))
	b(0x4c, 0, 0)
	jmpTo(viewportDecode)

	viewportDecoded := len(p)
	for _, at := range viewportDecodedJumps {
		jump(at, viewportDecoded)
	}
	b(0x84, 0x08) // persist decoded cell count before proof-map projection loops
	// Project host format-run flags (bit 0 bold, bit 1 italic, bit 2 underline,
	// bit 3 spelling, bit 4 grammar) into one byte per rendered cell at $0b00.
	// This is cartridge-owned staging: render can consume it when visuals are
	// enabled, while the default disabled gate keeps visible output unchanged.
	b(0xc2, 0x20)                               // REP #$20
	b(0xaf, 0x7c, 0x41, 0x70, 0x29, 0x1f, 0x00) // LDA.l $70417c; AND #$001f (table count low)
	b(0xc9, 0x10, 0x00)
	bccRunCountClamp := len(p)
	b(0x90, 0)
	b(0xa9, 0x10, 0x00) // clamp to mailbox cap (16 runs)
	runCountClamp := len(p)
	branch(bccRunCountClamp, runCountClamp)
	b(0x85, 0x1f)                   // staged run count for draw-loop fast-path flagging
	b(0x0a, 0x0a, 0x0a, 0x85, 0x1a) // run bytes = count * 8 (DIAG: was followed by STZ $1b)
	b(0xe2, 0x20)                   // SEP #$20
	b(0xa5, 0x1f)
	beqNoProofRuns := len(p)
	b(0xf0, 0)
	b(0xa9, 0x80, 0x85, 0x1f)
	braProofFlagReady := len(p)
	b(0x80, 0)
	noProofRuns := len(p)
	branch(beqNoProofRuns, noProofRuns)
	b(0x64, 0x1f)
	proofFlagReady := len(p)
	branch(braProofFlagReady, proofFlagReady)
	// A non-empty selection must also route the draw loop into the
	// attribute-processing path below, even when there are zero proofing
	// format runs, so selected-but-otherwise-plain text still highlights.
	b(0xc2, 0x20)             // REP #$20
	b(0xa5, 0x50, 0xc5, 0x52) // LDA $50; CMP $52
	beqNoSelectionFlag := len(p)
	b(0xf0, 0)
	b(0xe2, 0x20) // SEP #$20
	b(0xa9, 0x80, 0x85, 0x1f)
	braSelectionFlagDone := len(p)
	b(0x80, 0)
	noSelectionFlag := len(p)
	branch(beqNoSelectionFlag, noSelectionFlag)
	b(0xe2, 0x20) // SEP #$20
	selectionFlagDone := len(p)
	branch(braSelectionFlagDone, selectionFlagDone)
	b(0xa0, 0x00, 0x00)
	proofCellLoop := len(p)
	b(0xc4, 0x08) // CPY $08
	bcsProofDone := len(p)
	b(0xb0, 0)
	b(0xa9, 0x00, 0x99, 0x00, 0x0b) // LDA #0; STA $0b00,Y
	b(0x99, 0x00, 0x0d)             // STA $0d00,Y (paragraph alignment: 0 = left)
	b(0xb9, 0x00, 0x07, 0x85, 0x1c) // LDA $0700,Y; STA $1c
	b(0xb9, 0x00, 0x09, 0x85, 0x1d) // LDA $0900,Y; STA $1d
	b(0xa2, 0x00, 0x00)
	proofRunLoop := len(p)
	b(0xe4, 0x1a) // CPX $1a
	bcsProofNextCell := len(p)
	b(0xb0, 0)
	// This mask is the single real gate for richStyleVisualsEnabled: with it
	// disabled, bits 0-2 (bold|italic|underline) never reach $0b00,X, so the
	// draw loop's rich-style computation always sees shape=0 and no tile-id
	// page bits are ever set, regardless of what format runs the host sends.
	formatRunMask := byte(0x18) // spell|grammar only
	if richStyleVisualsEnabled {
		formatRunMask = 0x1f // + bold|italic|underline
	}
	b(0xbf, 0x84, 0x42, 0x70, 0x29, formatRunMask) // LDA.l $704284,X; AND #formatRunMask
	b(0x85, 0x1e)                                  // save proof bits
	// A run can carry nothing but its paragraph's alignment -- a centred plain
	// paragraph is exactly that -- so the "is this run worth matching" test has
	// to consider alignment as well, or centring a paragraph that has no bold,
	// italic, underline or proofing anywhere in it would be dropped here.
	b(0xbf, 0x86, 0x42, 0x70, 0x29, 0x03, 0x05, 0x1e) // LDA.l $704286,X; AND #3; ORA $1e
	beqProofNextRun := len(p)
	b(0xf0, 0)
	b(0xc2, 0x20)             // REP #$20
	b(0xa5, 0x1c)             // cell UTF-16 offset
	b(0xdf, 0x80, 0x42, 0x70) // CMP.l $704280,X (run start)
	bccProofNoMatch := len(p)
	b(0x90, 0)
	b(0x38, 0xff, 0x80, 0x42, 0x70) // SEC; SBC.l $704280,X (delta)
	b(0xdf, 0x82, 0x42, 0x70)       // CMP.l $704282,X (run length)
	bcsProofNoMatch := len(p)
	b(0xb0, 0)
	b(0xe2, 0x20)                                     // SEP #$20
	b(0xb9, 0x00, 0x0b, 0x05, 0x1e, 0x99, 0x00, 0x0b) // ORA into $0b00,Y
	// Alignment is a property of the whole paragraph, not an accumulating bit
	// field: the last run covering this character wins, and left (0) never
	// overwrites a centre or right already recorded for it.
	b(0xbf, 0x86, 0x42, 0x70, 0x29, 0x03) // LDA.l $704286,X; AND #3
	beqProofAlignKept := len(p)
	b(0xf0, 0)
	b(0x99, 0x00, 0x0d) // STA $0d00,Y
	proofAlignKept := len(p)
	branch(beqProofAlignKept, proofAlignKept)
	braProofAfterMatch := len(p)
	b(0x80, 0)
	proofNoMatch := len(p)
	branch(bccProofNoMatch, proofNoMatch)
	branch(bcsProofNoMatch, proofNoMatch)
	b(0xe2, 0x20) // SEP #$20
	proofAfterMatch := len(p)
	branch(braProofAfterMatch, proofAfterMatch)
	proofNextRun := len(p)
	branch(beqProofNextRun, proofNextRun)
	b(0x8a, 0x18, 0x69, 0x08, 0xaa) // TXA; CLC; ADC #8; TAX
	jmpTo(proofRunLoop)
	proofNextCell := len(p)
	branch(bcsProofNextCell, proofNextCell)
	b(0xc8)
	jmpTo(proofCellLoop)
	proofDone := len(p)
	branch(bcsProofDone, proofDone)
	b(0xc2, 0x20)                                     // REP #$20
	b(0xa5, 0x46, 0xc5, 0x40, 0xd0, 0x02, 0x84, 0x00) // LDA $46; CMP $40; BNE +2; STY $00
	b(0x84, 0x08)
	b(0xe2, 0x20) // SEP #$20: restore 8-bit A before render (mirrors the empty path); render's PHA/PLB assumes 8-bit A.
	viewportRenderCalls = append(viewportRenderCalls, len(p))
	b(0x20, 0, 0)
	braViewportDone := len(p)
	b(0x80, 0)
	viewportEmpty := len(p)
	jump(viewportEmptyJump, viewportEmpty)
	b(0xc2, 0x20) // REP #$20
	b(0x64, 0x00, 0x64, 0x08)
	b(0xe2, 0x20) // SEP #$20
	viewportRenderCalls = append(viewportRenderCalls, len(p))
	b(0x20, 0, 0)
	viewportDone := len(p)
	branch(braViewportDone, viewportDone)
	b(0xe2, 0x10)
	b(0xab, 0x60) // PLB; RTS

	// update_scroll_thumb maps bytes_before / total_document_bytes from the
	// committed 32-bit viewport fields to an 8-bit OBJ X coordinate. Shift both
	// operands together until total fits in one byte, then use the SNES's own
	// 8x8 multiply and 16/8 divide registers for:
	//
	//   trackStart + floor(normalizedBefore * trackTravel / normalizedTotal)
	//
	// Common normalization preserves large-document ratios without a general
	// 32-bit division routine. Only low-order precision is discarded, keeping
	// large-document positioning stable to a small number of track pixels.
	updateScrollThumb := len(p)
	patch16(scrollThumbCall, updateScrollThumb)
	b(0xe2, 0x30) // SEP #$30: byte-oriented metadata normalization
	for i := 0; i < 4; i++ {
		b(0xaf, byte(0x6c+i), 0x41, 0x70, 0x8d, byte(0x54+i), 0x03) // total -> $0354..$0357
		b(0xaf, byte(0x70+i), 0x41, 0x70, 0x8d, byte(0x58+i), 0x03) // before -> $0358..$035b
	}
	thumbNormalize := len(p)
	b(0xad, 0x55, 0x03, 0x0d, 0x56, 0x03, 0x0d, 0x57, 0x03) // any total byte above low nonzero?
	beqThumbNormalized := len(p)
	b(0xf0, 0)
	b(0x4e, 0x57, 0x03, 0x6e, 0x56, 0x03, 0x6e, 0x55, 0x03, 0x6e, 0x54, 0x03) // total >>= 1
	b(0x4e, 0x5b, 0x03, 0x6e, 0x5a, 0x03, 0x6e, 0x59, 0x03, 0x6e, 0x58, 0x03) // before >>= 1
	braThumbNormalize := len(p)
	b(0x80, 0)
	branch(braThumbNormalize, thumbNormalize)
	thumbNormalized := len(p)
	branch(beqThumbNormalized, thumbNormalized)
	b(0xad, 0x54, 0x03)
	bneThumbHasTotal := len(p)
	b(0xd0, 0)
	b(0xa9, scrollThumbTrackStart, 0x8d, 0x52, 0x03)
	b(0xa9, 0x01, 0x8d, 0x53, 0x03, 0x60) // empty document -> track origin, dirty
	thumbHasTotal := len(p)
	branch(bneThumbHasTotal, thumbHasTotal)
	// WRMPYA/WRMPYB: normalizedBefore * 226 -> RDMPYL/H.
	b(0xad, 0x58, 0x03, 0x8f, 0x02, 0x42, 0x00)
	b(0xa9, scrollThumbTravel, 0x8f, 0x03, 0x42, 0x00)
	b(0xea, 0xea, 0xea, 0xea) // hardware multiply settle
	// WRDIVL/H + WRDIVB: 16-bit product / normalizedTotal -> RDDIVL/H.
	b(0xaf, 0x16, 0x42, 0x00, 0x8f, 0x04, 0x42, 0x00)
	b(0xaf, 0x17, 0x42, 0x00, 0x8f, 0x05, 0x42, 0x00)
	b(0xad, 0x54, 0x03, 0x8f, 0x06, 0x42, 0x00)
	b(0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea) // hardware divide settle
	b(0xaf, 0x14, 0x42, 0x00)
	b(0xc9, scrollThumbTravel+1)
	bccThumbQuotientReady := len(p)
	b(0x90, 0)
	b(0xa9, scrollThumbTravel) // keep inconsistent metadata inside the track
	thumbQuotientReady := len(p)
	branch(bccThumbQuotientReady, thumbQuotientReady)
	b(0x18, 0x69, scrollThumbTrackStart, 0x8d, 0x52, 0x03)
	b(0xa9, 0x01, 0x8d, 0x53, 0x03, 0x60) // publish X and mark sprite 1 dirty

	editor := len(p)
	patch16(editCall, editor)
	b(0xc2, 0x10)
	// F3 owns persistence settings globally. $0314 preserves the exact origin
	// screen, matching the F2 help-card contract.
	b(0xa5, 0x5b, 0xc9, 0x19)
	bneSettingsToggle := len(p)
	b(0xd0, 0)
	b(0xad, 0x1d, 0x03, 0xc9, 0x10)
	beqSettingsClose := len(p)
	b(0xf0, 0)
	b(0x8d, 0x14, 0x03)
	b(0xa9, 0x10, 0x8d, 0x1d, 0x03, 0x9c, 0x1a, 0x03)
	braSettingsToggled := len(p)
	b(0x80, 0)
	settingsClose := len(p)
	branch(beqSettingsClose, settingsClose)
	b(0xad, 0x14, 0x03, 0x8d, 0x1d, 0x03)
	settingsToggled := len(p)
	branch(braSettingsToggled, settingsToggled)
	settingsToggleRenderCall := len(p)
	b(0x20, 0, 0, 0x64, 0x5b, 0x60)
	settingsNotToggle := len(p)
	branch(bneSettingsToggle, settingsNotToggle)
	// F2 opens the help card from any mode. $0313 retains the exact origin so F2,
	// F1, or Backspace returns to the screen the user was on, including a live
	// browser or menu selection. Help handling comes before the generic F1 menu
	// toggle and mode dispatch so neither can swallow its dismissal keys.
	b(0xa5, 0x5b, 0xc9, 0x1c)
	beqHelpToggle := len(p)
	b(0xf0, 0)
	b(0xad, 0x1d, 0x03, 0xc9, 0x0f)
	bneHelpNotHandled1 := len(p)
	b(0xd0, 0)
	b(0xa5, 0x5b, 0xc9, 0x18)
	beqHelpClose1 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x08)
	bneHelpNotHandled2 := len(p)
	b(0xd0, 0)
	helpToggle := len(p)
	branch(beqHelpToggle, helpToggle)
	b(0xad, 0x1d, 0x03, 0xc9, 0x0f)
	beqHelpClose2 := len(p)
	b(0xf0, 0)
	b(0x8d, 0x13, 0x03) // preserve the origin mode
	b(0xa9, 0x0f, 0x8d, 0x1d, 0x03)
	braHelpToggled := len(p)
	b(0x80, 0)
	helpClose := len(p)
	branch(beqHelpClose1, helpClose)
	branch(beqHelpClose2, helpClose)
	b(0xad, 0x13, 0x03, 0x8d, 0x1d, 0x03)
	helpToggled := len(p)
	branch(braHelpToggled, helpToggled)
	helpToggleRenderCall := len(p)
	b(0x20, 0, 0, 0x64, 0x5b, 0x60)
	helpNotHandled := len(p)
	branch(bneHelpNotHandled1, helpNotHandled)
	branch(bneHelpNotHandled2, helpNotHandled)
	b(0xa5, 0x5b, 0xc9, 0x18) // F1 toggles the reusable cartridge menu.
	bneMenuToggle := len(p)
	b(0xd0, 0)
	b(0xad, 0x1d, 0x03)
	beqOpenMenu := len(p)
	b(0xf0, 0)
	b(0x9c, 0x1d, 0x03)
	braMenuToggled := len(p)
	b(0x80, 0)
	openMenu := len(p)
	branch(beqOpenMenu, openMenu)
	b(0xa9, 0x01)
	sta(0x031d)
	menuToggled := len(p)
	branch(braMenuToggled, menuToggled)
	b(0x9c, 0x1e, 0x03)
	menuToggleRenderCall := len(p)
	b(0x20, 0, 0, 0x64, 0x5b, 0x60)
	menuNotToggle := len(p)
	branch(bneMenuToggle, menuNotToggle)

	var dispatchExit int
	var renderCalls []int
	var emitDocumentRenderCall func()
	emitDocumentRenderCall = func() {
		b(0x64, 0x1f) // local edits render plain baseline until host viewport repopulates proofing runs
		renderCalls = append(renderCalls, len(p))
		b(0x20, 0, 0)
	}

	b(0xad, 0x1d, 0x03)
	beqEditorDocument := len(p)
	b(0xf0, 0)
	menuInputCall = len(p)
	b(0x20, 0, 0, 0x64, 0x5b, 0x60)
	editorDocument := len(p)
	branch(beqEditorDocument, editorDocument)
	// editor_dispatch entered with 16-bit X/Y (REP #$10 above) for menu/browser
	// navigation. Document editing indexes the bounded <=239-byte guest buffer
	// with 8-bit X, and reads single-byte cursor/length at $00/$08 via LDX; a
	// 16-bit LDX would pull the adjacent byte ($09, now nonzero) into the high
	// byte and corrupt the insert/shift loop. Restore 8-bit X/Y here. Paths that
	// need 16-bit index (e.g. command navigation) set it themselves.
	b(0xe2, 0x10) // SEP #$10
	// F4 opens the cartridge find screen; the query is guest-owned text until
	// Enter publishes one CommandFindNext record.
	b(0xa5, 0x5b, 0xc9, 0x1d)
	bneEditNotFind := len(p)
	b(0xd0, 0)
	b(0xa9, 0x0e, 0x8d, 0x1d, 0x03)
	emitDocumentRenderCall()
	b(0x64, 0x5b, 0x60)
	editNotFind := len(p)
	branch(bneEditNotFind, editNotFind)
	b(0xc9, 0x08)
	bneEditEnter := len(p)
	b(0xd0, 0)
	backspaceCall = len(p)
	b(0x20, 0, 0, 0x60)
	editEnter := len(p)
	branch(bneEditEnter, editEnter)
	b(0xc9, 0x0d)
	bneEditLeft := len(p)
	b(0xd0, 0)
	insertCall = len(p)
	b(0x20, 0, 0, 0x60)
	editLeft := len(p)
	branch(bneEditLeft, editLeft)
	b(0xc9, 0x11)
	bneEditRight := len(p)
	b(0xd0, 0)
	leftCall = len(p)
	b(0x20, 0, 0, 0x60)
	editRight := len(p)
	branch(bneEditRight, editRight)
	b(0xc9, 0x12)
	bneEditUp := len(p)
	b(0xd0, 0)
	rightCall = len(p)
	b(0x20, 0, 0, 0x60)
	editUp := len(p)
	branch(bneEditUp, editUp)
	b(0xc9, 0x13)
	bneEditDown := len(p)
	b(0xd0, 0)
	upCall = len(p)
	b(0x20, 0, 0, 0x60)
	editDown := len(p)
	branch(bneEditDown, editDown)
	b(0xc9, 0x14)
	bneEditDelete := len(p)
	b(0xd0, 0)
	downCall = len(p)
	b(0x20, 0, 0, 0x60)
	editDeleteCheck := len(p)
	branch(bneEditDelete, editDeleteCheck)
	b(0xc9, 0x15)
	bneEditHome := len(p)
	b(0xd0, 0)
	deleteCall = len(p)
	b(0x20, 0, 0, 0x60)
	editHome := len(p)
	branch(bneEditHome, editHome)
	b(0xc9, 0x16)
	bneEditEnd := len(p)
	b(0xd0, 0)
	homeCall = len(p)
	b(0x20, 0, 0, 0x60)
	editEnd := len(p)
	branch(bneEditEnd, editEnd)
	b(0xc9, 0x17)
	bneEditPrintable := len(p)
	b(0xd0, 0)
	endCall = len(p)
	b(0x20, 0, 0, 0x60)
	editPrintable := len(p)
	branch(bneEditPrintable, editPrintable)
	b(0xc9, 0x20)
	bccEditDone := len(p)
	b(0x90, 0)
	insertPrintableCall := len(p)
	b(0x20, 0, 0)
	editDone := len(p)
	branch(bccEditDone, editDone)
	b(0x60)

	insert := len(p)
	for _, at := range []int{insertCall, insertPrintableCall} {
		patch16(at, insert)
	}
	b(0xa5, 0x08, 0xc9, 0xef) // bounded 239-byte guest working set
	bcsInsertDone := len(p)
	b(0xb0, 0)
	b(0xa6, 0x08)
	shiftRight := len(p)
	b(0xe4, 0x00)
	beqPlace := len(p)
	b(0xf0, 0)
	b(0xca, 0xbd, 0x00, 0x05, 0xe8, 0x9d, 0x00, 0x05, 0xca)
	braShiftRight := len(p)
	b(0x80, 0)
	branch(braShiftRight, shiftRight)
	place := len(p)
	branch(beqPlace, place)
	b(0xa6, 0x00, 0xa5, 0x5b, 0x9d, 0x00, 0x05, 0xc2, 0x20, 0xe6, 0x00, 0xe6, 0x08, 0xe2, 0x20)
	// dispatchExit is defined later in this function; its Go value is still 0 at
	// this point, so patch these forward JMP targets once it is known (below).
	dispatchExitJumps := []int{}
	dispatchExitJumps = append(dispatchExitJumps, len(p))
	b(0x4c, 0, 0)

	editDelete := len(p)
	patch16(deleteCall, editDelete)
	b(0xa5, 0x00, 0xd0, 0x03)
	dispatchExitJumps = append(dispatchExitJumps, len(p))
	b(0x4c, 0, 0)
	b(0xc2, 0x20, 0xc6, 0x00, 0xc6, 0x08, 0xe2, 0x20)
	b(0xa6, 0x00)
	shiftLeft := len(p)
	b(0xe4, 0x08)
	beqExit := len(p)
	b(0xf0, 0)
	b(0xbd, 0x01, 0x05, 0x9d, 0x00, 0x05, 0xe8)
	braShiftLeft := len(p)
	b(0x80, 0)
	branch(braShiftLeft, shiftLeft)
	exit := len(p)
	branch(beqExit, exit)
	dispatchExitJumps = append(dispatchExitJumps, len(p))
	b(0x4c, 0, 0)

	insertDone := len(p)
	branch(bcsInsertDone, insertDone)
	b(0x60)

	backspace := len(p)
	patch16(backspaceCall, backspace)
	b(0xa5, 0x00)
	beqBackspaceDone := len(p)
	b(0xf0, 0)
	b(0xc2, 0x20, 0xc6, 0x00, 0xe2, 0x20, 0xa6, 0x00)
	shiftLeftBk := len(p)
	b(0xe8, 0xe4, 0x08)
	beqShifted := len(p)
	b(0xf0, 0)
	b(0xbd, 0x00, 0x05, 0xca, 0x9d, 0x00, 0x05, 0xe8) // LDA $0500,X; DEX; STA $0500,X; INX (was STA $0200,X)
	braShiftLeftBk := len(p)
	b(0x80, 0)
	branch(braShiftLeftBk, shiftLeftBk)
	shifted := len(p)
	branch(beqShifted, shifted)
	b(0xc2, 0x20, 0xc6, 0x08, 0xe2, 0x20)
	dispatchExit = len(p)
	for _, at := range dispatchExitJumps {
		jump(at, dispatchExit)
	}
	emitDocumentRenderCall()
	b(0xe2, 0x10)
	b(0x60)
	backspaceDone := len(p)
	branch(beqBackspaceDone, backspaceDone)
	b(0x60)

	delete := len(p)
	patch16(deleteCall, delete)
	b(0xa5, 0x00, 0xc5, 0x08)
	bcsDeleteDone := len(p)
	b(0xb0, 0)
	b(0xa6, 0x00)
	deleteShift := len(p)
	b(0xe4, 0x08)
	bcsDeleteLast := len(p)
	b(0xb0, 0)
	b(0xbd, 0x01, 0x02, 0x9d, 0x00, 0x02, 0xe8)
	braDeleteShift := len(p)
	b(0x80, 0)
	branch(braDeleteShift, deleteShift)
	deleteLast := len(p)
	branch(bcsDeleteLast, deleteLast)
	b(0xc2, 0x20, 0xc6, 0x08, 0xe2, 0x20)
	emitDocumentRenderCall()
	deleteDone := len(p)
	branch(bcsDeleteDone, deleteDone)
	b(0x60)

	home := len(p)
	patch16(homeCall, home)
	b(0x64, 0x00)
	emitDocumentRenderCall()
	b(0x60)

	end := len(p)
	patch16(endCall, end)
	b(0xa5, 0x08, 0x85, 0x00)
	emitDocumentRenderCall()
	b(0x60)

	left := len(p)
	patch16(leftCall, left)
	b(0xa5, 0x00)
	beqLeftDone := len(p)
	b(0xf0, 0)
	b(0xc6, 0x00)
	emitDocumentRenderCall()
	leftDone := len(p)
	branch(beqLeftDone, leftDone)
	b(0x60)

	right := len(p)
	patch16(rightCall, right)
	b(0xa5, 0x00, 0xc5, 0x08)
	bcsRightDone := len(p)
	b(0xb0, 0)
	b(0xe6, 0x00)
	emitDocumentRenderCall()
	rightDone := len(p)
	branch(bcsRightDone, rightDone)
	b(0x60)

	// Up/Down deliberately do not move the guest cursor here. They used to add or
	// subtract 30 *characters*, which equals one screen row only when the layout
	// happens to be exactly 30 characters per row -- no word wrap, no CRs. On any
	// real document that lands somewhere else entirely, and since this local edit
	// runs before commandEnqueue and its render waits for VBlank, the wrong
	// position was actually displayed for a frame before the host's viewport
	// corrected it. Measured on a ragged three-line fixture: the local move
	// clamped the cursor to character 45 (end of document) while the wrap-aware
	// resolution published character 23.
	//
	// `verticalMove` sets the guest cursor to the cell it actually resolved, so
	// the optimistic frame agrees with what was published instead of guessing.
	// Redrawing here still keeps the caret blinking on the unchanged position
	// until then, which is stale by a frame but never wrong.
	up := len(p)
	patch16(upCall, up)
	emitDocumentRenderCall()
	b(0x60)

	down := len(p)
	patch16(downCall, down)
	emitDocumentRenderCall()
	b(0x60)

	// command_space keeps the controller-port queue lossless. The cartridge
	// does not dequeue another XBAND byte unless the SPSC command ring can
	// admit the largest cartridge command record (20-byte header plus a
	// 64-byte opaque file identifier). Browser actions must not be allowed to
	// consume an input byte if their complete backend command cannot fit.
	commandSpace := len(p)
	patch16(commandSpaceCall, commandSpace)
	b(0xc2, 0x20)                   // REP #$20: 16-bit accumulator
	b(0xaf, 0x04, 0x00, 0x70, 0x38) // consumer - producer - 1
	b(0xef, 0x02, 0x00, 0x70, 0x3a)
	b(0x29, 0xff, 0x1f) // command ring capacity is 0x2000
	b(0xc9, 0x54, 0x00, 0xe2, 0x20, 0x60)

	// command_enqueue translates guest-owned edit semantics into the public
	// DocumentEngine command numbers. The local edit is only an optimistic
	// frame; the host commits this record and the next viewport replaces it.
	commandEnqueue := len(p)
	patch16(commandEnqueueCall, commandEnqueue)
	b(0xad, 0x1d, 0x03)
	beqCommandEditorMode := len(p)
	b(0xf0, 0)
	b(0x60)
	commandEditorMode := len(p)
	branch(beqCommandEditorMode, commandEditorMode)
	// Stage the sticky vertical column, then clear it. Only Up/Down write $033c
	// back, so any other key leaves it $ff and the next vertical run restarts
	// from the caret's real column. Staging into $033d first is what lets a run
	// of Up/Down still see the previous value -- clearing in place would wipe the
	// sticky column on the very keypress that wants to read it.
	b(0xad, 0x3c, 0x03, 0x8d, 0x3d, 0x03) // LDA $033c ; STA $033d
	b(0xa9, 0xff, 0x8d, 0x3c, 0x03)       // LDA #$ff ; STA $033c
	equalJump := func() int {
		b(0xd0, 0x03) // BNE skips the absolute jump.
		at := len(p)
		b(0x4c, 0x00, 0x00)
		return at
	}
	b(0xa5, 0x5b, 0xc9, 0x08)
	beqCommandBackspace := equalJump()
	b(0xc9, 0x0d)
	beqCommandInsert := equalJump()
	b(0xc9, 0x11)
	beqCommandLeft := equalJump()
	b(0xc9, 0x12)
	beqCommandRight := equalJump()
	b(0xc9, 0x13)
	beqCommandUp := equalJump()
	b(0xc9, 0x14)
	beqCommandDown := equalJump()
	b(0xc9, 0x15)
	beqCommandDelete := equalJump()
	b(0xc9, 0x16)
	beqCommandHome := equalJump()
	b(0xc9, 0x17)
	beqCommandEnd := equalJump()
	b(0xc9, 0x1a)
	beqCommandPageUp := equalJump()
	b(0xc9, 0x1b)
	beqCommandPageDown := equalJump()
	b(0xc9, 0x20)
	b(0xb0, 0x01, 0x60) // non-printable command completes without a record
	// Keyboard arrow/Home/End navigation publishes a semantic DocumentEngine
	// command with no payload (a 20-byte header-only record). Shift held selects
	// the selection-extending variant; the host owns the authoritative caret and
	// selection. (The pointer path publishes SetCursor/ExtendCursor separately.)
	emitSemanticNav := func(moveKind, extendKind byte) {
		b(0xe2, 0x20) // SEP #$20 (8-bit A)
		b(0xa5, 0x04) // LDA $04 (shift key state)
		beqMove := len(p)
		b(0xf0, 0)
		b(0xa9, extendKind) // LDA #extendKind
		braStore := len(p)
		b(0x80, 0)
		moveLabel := len(p)
		branch(beqMove, moveLabel)
		b(0xa9, moveKind) // LDA #moveKind
		storeLabel := len(p)
		branch(braStore, storeLabel)
		b(0x8d, 0x16, 0x03) // STA $0316 (kind low)
		b(0x9c, 0x17, 0x03) // STZ $0317 (kind high)
		b(0x9c, 0x15, 0x03) // STZ $0315 (payload length low = 0)
		b(0x9c, 0x18, 0x03) // STZ $0318 (payload length high = 0)
	}
	commandInsert := len(p)
	jump(beqCommandInsert, commandInsert)
	b(0xa5, 0x5b, 0x8d, 0x00, 0x18) // stage UTF-8 byte for generic payload writer
	b(0xa9, 0x01, 0x8d, 0x16, 0x03, 0x8d, 0x15, 0x03, 0x9c, 0x17, 0x03, 0x9c, 0x18, 0x03)
	braCommandWrite1 := len(p)
	b(0x4c, 0, 0)
	commandBackspace := len(p)
	jump(beqCommandBackspace, commandBackspace)
	b(0xa9, 0x25, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03, 0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03)
	braCommandWrite2 := len(p)
	b(0x4c, 0, 0)
	commandLeft := len(p)
	jump(beqCommandLeft, commandLeft)
	emitSemanticNav(7, 12) // MoveLeft / ExtendLeft
	braCommandWrite3 := len(p)
	b(0x4c, 0, 0)
	commandRight := len(p)
	jump(beqCommandRight, commandRight)
	emitSemanticNav(8, 13) // MoveRight / ExtendRight
	braCommandWrite4 := len(p)
	b(0x4c, 0, 0)
	// Up/Down first try the cartridge's own 30-column layout. verticalMove
	// publishes an absolute caret position when it can resolve a target row on
	// screen, and returns nonzero to ask for the old semantic move when it
	// cannot -- at the top and bottom of the plane, where falling through to
	// MoveUp/MoveDown is exactly what keeps the viewport scrolling.
	commandUp := len(p)
	jump(beqCommandUp, commandUp)
	b(0xe2, 0x20, 0xa9, 0x00) // SEP #$20 ; LDA #0 (up)
	jsrTo(verticalMove)
	beqUpHandled := len(p)
	b(0xf0, 0)              // BEQ upHandled (verticalMove returns A=0 when it published)
	emitSemanticNav(14, 39) // MoveUp / ExtendUp
	braCommandWrite5 := len(p)
	b(0x4c, 0, 0)
	upHandled := len(p)
	branch(beqUpHandled, upHandled)
	b(0x60) // RTS -- the command is already on the ring
	commandDown := len(p)
	jump(beqCommandDown, commandDown)
	b(0xe2, 0x20, 0xa9, 0x01) // SEP #$20 ; LDA #1 (down)
	jsrTo(verticalMove)
	beqDownHandled := len(p)
	b(0xf0, 0)              // BEQ downHandled
	emitSemanticNav(15, 40) // MoveDown / ExtendDown
	braCommandWrite6 := len(p)
	b(0x4c, 0, 0)
	downHandled := len(p)
	branch(beqDownHandled, downHandled)
	b(0x60) // RTS
	commandDelete := len(p)
	jump(beqCommandDelete, commandDelete)
	b(0xa9, 0x26, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03, 0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03)
	braCommandWrite7 := len(p)
	b(0x4c, 0, 0)
	commandHome := len(p)
	jump(beqCommandHome, commandHome)
	emitSemanticNav(16, 41) // MoveLineStart / ExtendLineStart
	braCommandWrite8 := len(p)
	b(0x4c, 0, 0)
	commandEnd := len(p)
	jump(beqCommandEnd, commandEnd)
	emitSemanticNav(17, 42) // MoveLineEnd / ExtendLineEnd
	braCommandWrite9 := len(p)
	b(0x4c, 0, 0)
	// MovePageUp/MovePageDown have no Extend* counterpart in DocumentEngine's
	// command enum, so (unlike the arrow/Home/End keys above) shift state is
	// simply ignored here and the kind is staged directly.
	commandPageUp := len(p)
	jump(beqCommandPageUp, commandPageUp)
	b(0xa9, 18, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03, 0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03) // MovePageUp
	braCommandWrite10 := len(p)
	b(0x4c, 0, 0)
	commandPageDown := len(p)
	jump(beqCommandPageDown, commandPageDown)
	b(0xa9, 19, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03, 0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03) // MovePageDown
	commandWriteLabel := len(p)
	for _, at := range []int{
		braCommandWrite1, braCommandWrite2, braCommandWrite3, braCommandWrite4,
		braCommandWrite5, braCommandWrite6, braCommandWrite7, braCommandWrite8,
		braCommandWrite9, braCommandWrite10,
	} {
		jump(at, commandWriteLabel)
	}
	commandWriteCall := len(p)
	b(0x20, 0, 0)
	b(0x60)

	// command_write serializes one record while X is a 16-bit ring cursor.
	// Payload bytes are staged in cartridge WRAM at $1800. This keeps keyboard
	// insertion and opaque FileCatalog IDs on exactly one wire path. The
	// producer index is committed only after every header/payload byte.
	var ringWriteCalls []int
	ringByte := func(v ...byte) {
		b(v...)
		ringWriteCalls = append(ringWriteCalls, len(p))
		b(0x20, 0, 0)
	}
	commandWrite := len(p)
	patch16(commandWriteCall, commandWrite)
	patch16(resolveCommandCall, commandWrite)
	patch16(scrollCommandCall, commandWrite)
	b(0xc2, 0x30, 0xaf, 0x02, 0x00, 0x70, 0xaa, 0xe2, 0x20)
	b(0xad, 0x15, 0x03, 0x8d, 0x2c, 0x03) // contiguous 16-bit payload count
	b(0xad, 0x18, 0x03, 0x8d, 0x2d, 0x03)
	ringByte(0xa9, 0x01)
	ringByte(0xa9, 0x00) // protocol
	ringByte(0xad, 0x16, 0x03)
	ringByte(0xad, 0x17, 0x03) // kind
	ringByte(0xad, 0x15, 0x03)
	ringByte(0xad, 0x18, 0x03) // payload byte count
	ringByte(0xad, 0x19, 0x03)
	ringByte(0xad, 0x4c, 0x03) // flags high = page number for list paging
	ringByte(0xad, 0x11, 0x03)
	ringByte(0xad, 0x12, 0x03)
	ringByte(0xa9, 0x00)
	ringByte(0xa9, 0x00) // 32-bit sequence
	for revisionByte := 0; revisionByte < 8; revisionByte++ {
		ringByte(0xaf, byte(revisionByte), 0x41, 0x70)
	}
	b(0xa0, 0x00, 0x00) // Y is a 16-bit byte index into $1800 payload staging.
	payloadWrite := len(p)
	b(0xcc, 0x2c, 0x03)
	bcsPayloadDone := len(p)
	b(0xb0, 0)
	ringByte(0xb9, 0x00, 0x18)
	b(0xc8)
	braPayloadWrite := len(p)
	b(0x80, 0)
	payloadDone := len(p)
	branch(bcsPayloadDone, payloadDone)
	branch(braPayloadWrite, payloadWrite)
	b(0xc2, 0x20, 0x8a, 0x8f, 0x02, 0x00, 0x70, 0x9c, 0x19, 0x03)
	// The page byte is cleared in 8-bit mode: a 16-bit STZ here would also wipe
	// the adjacent directory-back depth at $034d.
	b(0xee, 0x11, 0x03, 0xe2, 0x30, 0x9c, 0x4c, 0x03, 0x60)

	// emitToolbarCommand: A holds a zero-payload DocumentEngine command kind
	// (bold/italic/underline toggle, alignment). Every one of these commands
	// takes no payload, so this is the same "stage kind, zero payload,
	// commandWrite" shape the menu's New Document entry uses.
	emitToolbarCommand := len(p)
	b(0x8d, 0x16, 0x03) // STA $0316 (kind low)
	b(0x9c, 0x17, 0x03) // STZ $0317 (kind high)
	b(0x9c, 0x15, 0x03) // STZ $0315 (payload length low)
	b(0x9c, 0x18, 0x03) // STZ $0318 (payload length high)
	jsrTo(commandWrite)
	b(0x60) // RTS

	// toolbarClick: mouse Y/X (packet-derived, $0335/$0334) already confirmed
	// within the toolbar card's screen rectangle by the caller. Row 0
	// (y 8-15) is bold/italic/underline; row 1 (y 16-23) is align
	// left/center/right. Column ranges match the 11-char " [X][X][X] "
	// label layout uploaded to the toolbar plane (see "Upload the 2x11
	// toolbar plane"): each bracketed button is 3 tiles (24px) wide, at
	// x 168-191 / 192-215 / 216-239, with 8px of padding on each side that
	// simply does nothing when clicked.
	toolbarClick := len(p)
	patch16(toolbarClickCall, toolbarClick)
	b(0xad, 0x35, 0x03) // LDA $0335 (mouse Y)
	b(0xc9, 16)
	bccToolbarRow0 := len(p)
	b(0x90, 0)
	// Row 1: alignment.
	b(0xad, 0x34, 0x03) // LDA $0334 (mouse X)
	b(0xc9, 192)
	bccToolbarLeft := len(p)
	b(0x90, 0)
	b(0xc9, 216)
	bccToolbarCenter := len(p)
	b(0x90, 0)
	b(0xc9, 240)
	bccToolbarRight := len(p)
	b(0x90, 0)
	b(0x60) // in the row-1 padding margin: no-op
	toolbarRight := len(p)
	branch(bccToolbarRight, toolbarRight)
	b(0xa9, 25) // AlignRight
	braEmitAlign1 := len(p)
	b(0x80, 0)
	toolbarCenter := len(p)
	branch(bccToolbarCenter, toolbarCenter)
	b(0xa9, 24) // AlignCenter
	braEmitAlign2 := len(p)
	b(0x80, 0)
	toolbarLeft := len(p)
	branch(bccToolbarLeft, toolbarLeft)
	b(0xa9, 23) // AlignLeft
	emitAlign := len(p)
	branch(braEmitAlign1, emitAlign)
	branch(braEmitAlign2, emitAlign)
	jmpTo(emitToolbarCommand)
	// Row 0: bold/italic/underline.
	toolbarClickRow0 := len(p)
	branch(bccToolbarRow0, toolbarClickRow0)
	b(0xad, 0x34, 0x03) // LDA $0334 (mouse X)
	b(0xc9, 192)
	bccToolbarBold := len(p)
	b(0x90, 0)
	b(0xc9, 216)
	bccToolbarItalic := len(p)
	b(0x90, 0)
	b(0xc9, 240)
	bccToolbarUnderline := len(p)
	b(0x90, 0)
	b(0x60) // in the row-0 padding margin: no-op
	toolbarUnderline := len(p)
	branch(bccToolbarUnderline, toolbarUnderline)
	b(0xa9, 22) // ToggleUnderline
	braEmitStyle1 := len(p)
	b(0x80, 0)
	toolbarItalic := len(p)
	branch(bccToolbarItalic, toolbarItalic)
	b(0xa9, 21) // ToggleItalic
	braEmitStyle2 := len(p)
	b(0x80, 0)
	toolbarBold := len(p)
	branch(bccToolbarBold, toolbarBold)
	b(0xa9, 20) // ToggleBold
	emitStyle := len(p)
	branch(braEmitStyle1, emitStyle)
	branch(braEmitStyle2, emitStyle)
	jmpTo(emitToolbarCommand)

	ringWrite := len(p)
	for _, at := range ringWriteCalls {
		patch16(at, ringWrite)
	}
	b(0x9f, 0x00, 0x01, 0x70, 0xe8, 0xe0, 0x00, 0x20)
	b(0x90, 0x03, 0xa2, 0x00, 0x00, 0x60)

	// menu_input owns mode and selection in cartridge WRAM. It emits ordinary
	// revisioned mailbox commands; the host never decides what a visible menu
	// item means or where it is drawn.
	menuInput := len(p)
	patch16(menuInputCall, menuInput)
	b(0xad, 0x1d, 0x03, 0xc9, 0x10, 0xd0, 0x03)
	settingsInputJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x11, 0xd0, 0x03)
	saveFormatInputJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x12, 0xd0, 0x03)
	transitionInputJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x05)
	bccBrowserNotReady := len(p)
	b(0x90, 0)
	browserInputCall = len(p)
	b(0x20, 0, 0, 0x60)
	browserNotReady := len(p)
	branch(bccBrowserNotReady, browserNotReady)
	b(0xc9, 0x01)
	beqMainMenuInput := len(p)
	b(0xf0, 0)
	b(0x60) // Loading modes accept only the global F1 toggle.
	mainMenuInput := len(p)
	branch(beqMainMenuInput, mainMenuInput)
	b(0xa5, 0x5b, 0xc9, 0x13) // Up
	bneMenuDown := len(p)
	b(0xd0, 0)
	b(0xad, 0x1e, 0x03)
	beqMenuUpDone := len(p)
	b(0xf0, 0)
	b(0xce, 0x1e, 0x03)
	menuUpDone := len(p)
	branch(beqMenuUpDone, menuUpDone)
	menuRenderCall1 := len(p)
	b(0x20, 0, 0, 0x60)
	menuDown := len(p)
	branch(bneMenuDown, menuDown)
	b(0xc9, 0x14) // Down
	bneMenuEnter := len(p)
	b(0xd0, 0)
	b(0xad, 0x1e, 0x03, 0xc9, 0x06)
	bcsMenuDownDone := len(p)
	b(0xb0, 0)
	b(0xee, 0x1e, 0x03)
	menuDownDone := len(p)
	branch(bcsMenuDownDone, menuDownDone)
	menuRenderCall2 := len(p)
	b(0x20, 0, 0, 0x60)
	menuEnter := len(p)
	branch(bneMenuEnter, menuEnter)
	b(0xc9, 0x0d)
	b(0xf0, 0x03)
	bneMenuDismiss := len(p)
	b(0x4c, 0, 0)
	b(0xad, 0x1e, 0x03)
	beqMenuNew := len(p)
	b(0xf0, 0)
	b(0xc9, 0x01)
	beqMenuOpen := len(p)
	b(0xf0, 0)
	b(0xc9, 0x02)
	beqMenuSave := len(p)
	b(0xf0, 0)
	b(0xc9, 0x03, 0xd0, 0x03)
	menuSaveAsJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x04, 0xd0, 0x03)
	menuRecentJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x05, 0xd0, 0x03)
	menuStructureJump := len(p)
	b(0x4c, 0, 0)
	// The remaining index is Statistics: a cartridge dialog rendered entirely
	// from the committed viewport's word/character/line metadata. F1 and
	// Backspace remain the menu dismissal paths.
	b(0xa9, 0x0d, 0x8d, 0x1d, 0x03)
	menuRenderCallStats := len(p)
	b(0x20, 0, 0, 0x64, 0x5b, 0x60)
	menuNew := len(p)
	branch(beqMenuNew, menuNew)
	b(0xa9, 0x21, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03)
	braMenuCommandClose1 := len(p)
	b(0x80, 0)
	menuOpen := len(p)
	branch(beqMenuOpen, menuOpen)
	b(0xa9, 0x07, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x02, 0x8d, 0x1d, 0x03, 0x9c, 0x1f, 0x03, 0x9c, 0x20, 0x03)
	braMenuCommandWait1 := len(p)
	b(0x80, 0)
	menuSave := len(p)
	branch(beqMenuSave, menuSave)
	// Save shows the transient cartridge SAVING screen (mode $0c). The next
	// committed host viewport or a save-failure outcome dialog clears it.
	b(0xa9, 0x20, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03)
	b(0xa9, 0x0c, 0x8d, 0x1d, 0x03)
	braMenuCommandSaving := len(p)
	b(0x80, 0)
	menuSaveAs := len(p)
	jump(menuSaveAsJump, menuSaveAs)
	b(0xa9, 0x01, 0x8d, 0x14, 0x03) // Back returns to the main menu.
	b(0xa9, 0x11, 0x8d, 0x1d, 0x03)
	// Default to the current offered format: ODT=0, DOCX=1, RTF=2,
	// Markdown=3. Compatibility-only FODT/text fall back to ODT.
	b(0xad, 0x69, 0x03)
	beqMenuFormatReady := len(p)
	b(0xf0, 0)
	b(0xc9, 0x02)
	beqMenuFormatDocx := len(p)
	b(0xf0, 0)
	b(0xc9, 0x03)
	beqMenuFormatRtf := len(p)
	b(0xf0, 0)
	b(0xc9, 0x04)
	beqMenuFormatMarkdown := len(p)
	b(0xf0, 0)
	b(0xa9, 0x00)
	braMenuFormatReady1 := len(p)
	b(0x80, 0)
	menuFormatDocx := len(p)
	branch(beqMenuFormatDocx, menuFormatDocx)
	b(0xa9, 0x01)
	braMenuFormatReady2 := len(p)
	b(0x80, 0)
	menuFormatRtf := len(p)
	branch(beqMenuFormatRtf, menuFormatRtf)
	b(0xa9, 0x02)
	braMenuFormatReady3 := len(p)
	b(0x80, 0)
	menuFormatMarkdown := len(p)
	branch(beqMenuFormatMarkdown, menuFormatMarkdown)
	b(0xa9, 0x03)
	menuFormatReady := len(p)
	branch(beqMenuFormatReady, menuFormatReady)
	branch(braMenuFormatReady1, menuFormatReady)
	branch(braMenuFormatReady2, menuFormatReady)
	branch(braMenuFormatReady3, menuFormatReady)
	b(0x8d, 0x2f, 0x03)
	menuRenderCallSaveFormat := len(p)
	b(0x20, 0, 0, 0x60)
	menuRecent := len(p)
	jump(menuRecentJump, menuRecent)
	b(0xa9, 0x06, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x04, 0x8d, 0x1d, 0x03, 0x9c, 0x1f, 0x03, 0x9c, 0x20, 0x03)
	braMenuCommandWait3 := len(p)
	b(0x80, 0)
	menuStructure := len(p)
	jump(menuStructureJump, menuStructure)
	b(0xa9, 0x2b, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03)
	menuCommandClose := len(p)
	branch(braMenuCommandClose1, menuCommandClose)
	b(0x9c, 0x1d, 0x03)
	braMenuCommandReady := len(p)
	b(0x80, 0)
	menuCommandWait := len(p)
	for _, at := range []int{braMenuCommandWait1, braMenuCommandWait3} {
		branch(at, menuCommandWait)
	}
	// The three list menus (Open/Save As/Recent) converge here. Snapshot the
	// pagination context: source kind from the staged $0316, page 0, no parent.
	// Also record it as the root kind and reset the directory-back depth.
	b(0xad, 0x16, 0x03, 0x8d, 0x4b, 0x03, 0x8d, 0x4e, 0x03)                                                       // context + root kind = $0316
	b(0x9c, 0x68, 0x03, 0x9c, 0x4a, 0x03, 0x9c, 0x4c, 0x03, 0x9c, 0x48, 0x03, 0x9c, 0x49, 0x03, 0x9c, 0x4d, 0x03) // filter/page/flags/len/has_more/depth = 0
	menuCommandReady := len(p)
	branch(braMenuCommandReady, menuCommandReady)
	branch(braMenuCommandSaving, menuCommandReady)
	b(0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03)
	jsrTo(commandWrite)
	menuRenderCall3 := len(p)
	b(0x20, 0, 0, 0x60)
	menuDismiss := len(p)
	jump(bneMenuDismiss, menuDismiss)
	b(0xc9, 0x08) // Backspace also dismisses the menu.
	bneMenuIgnore := len(p)
	b(0xd0, 0)
	b(0x9c, 0x1d, 0x03)
	menuRenderCall5 := len(p)
	b(0x20, 0, 0)
	menuIgnore := len(p)
	branch(bneMenuIgnore, menuIgnore)
	b(0x60)

	// browser_input turns the current visible file page into revisioned backend
	// commands. The cartridge retains both the selection and the opaque IDs;
	// the host only interprets the command that crosses the SRAM boundary.
	browserInput := len(p)
	patch16(browserInputCall, browserInput)
	emitBrowserRenderCall := func() {
		browserRenderCalls = append(browserRenderCalls, len(p))
		b(0x20, 0, 0)
	}
	// pageRequest re-issues the current listing command ($034b) at page $034a. It
	// carries the page in flags-high and, for a directory listing (kind 0), the
	// saved parent id at $1900, then drops to the matching loading mode. Normal
	// browser entry branches past it.
	braBrowserDispatch := len(p)
	b(0x80, 0) // BRA browserDispatch
	pageRequest := len(p)
	b(0xad, 0x4b, 0x03, 0x8d, 0x16, 0x03) // kind low = $034b
	b(0xa9, 0x01, 0x8d, 0x17, 0x03)       // kind high = 1
	b(0xad, 0x4a, 0x03, 0x8d, 0x4c, 0x03) // flags high = page $034a
	b(0xad, 0x68, 0x03, 0x8d, 0x19, 0x03) // preserve Save As format filter
	b(0xad, 0x4b, 0x03)                   // A = source kind
	bnePageNoParent := len(p)
	b(0xd0, 0)                            // BNE pageNoParent (roots/recent -> empty payload)
	b(0xad, 0x48, 0x03, 0x8d, 0x15, 0x03) // payload count low = parent len $0348
	b(0x9c, 0x18, 0x03)                   // count high = 0
	b(0xa0, 0x00)                         // LDY #0
	pageCopyParent := len(p)
	b(0xcc, 0x48, 0x03) // CPY $0348
	bcsPageCopyDone := len(p)
	b(0xb0, 0)                                  // BCS pageCopyDone
	b(0xb9, 0x00, 0x19, 0x99, 0x00, 0x18, 0xc8) // LDA $1900,Y; STA $1800,Y; INY
	braPageCopyParent := len(p)
	b(0x80, 0)
	pageCopyDone := len(p)
	branch(bcsPageCopyDone, pageCopyDone)
	branch(braPageCopyParent, pageCopyParent)
	braPageMode := len(p)
	b(0x80, 0) // BRA pageMode
	pageNoParent := len(p)
	branch(bnePageNoParent, pageNoParent)
	b(0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03) // empty payload
	pageMode := len(p)
	branch(braPageMode, pageMode)
	b(0xad, 0x1d, 0x03, 0x38, 0xe9, 0x03, 0x8d, 0x1d, 0x03) // mode = ready-3 (loading)
	b(0x9c, 0x1f, 0x03, 0x9c, 0x20, 0x03, 0x9c, 0x49, 0x03) // reset count/selection/has_more
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60) // RTS
	browserDispatch := len(p)
	branch(braBrowserDispatch, browserDispatch)
	// Ready browser modes are 5-7. Mode 8 waits for the host's overwrite
	// decision; modes 9-11 are cartridge-owned dialogs.
	b(0xad, 0x1d, 0x03, 0xc9, 0x08)
	bccBrowserReadyInput := len(p)
	b(0x90, 0)
	b(0xc9, 0x09, 0xd0, 0x03)
	browserConfirmJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0a, 0xd0, 0x03)
	browserFilenameJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0b, 0xd0, 0x03)
	browserOutcomeJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0d, 0xd0, 0x03)
	browserStatsJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0e, 0xd0, 0x03)
	browserFindJump := len(p)
	b(0x4c, 0, 0)
	b(0x60)
	browserReadyInput := len(p)
	branch(bccBrowserReadyInput, browserReadyInput)
	b(0xa5, 0x5b, 0xc9, 'n')
	beqBrowserNewFilename := len(p)
	b(0xf0, 0)
	b(0xc9, 'N')
	bneBrowserNormalInput := len(p)
	b(0xd0, 0)
	browserNewFilename := len(p)
	branch(beqBrowserNewFilename, browserNewFilename)
	b(0xad, 0x1d, 0x03, 0xc9, 0x06)
	bneBrowserNormalInput2 := len(p)
	b(0xd0, 0)
	// The current listing parent is the save destination. Do not retain a
	// second "last directory entered" copy: Back must change where N saves.
	b(0xad, 0x48, 0x03)
	beqBrowserNormalInput3 := len(p)
	b(0xf0, 0)
	b(0xa9, 0x0a, 0x8d, 0x1d, 0x03, 0x9c, 0x31, 0x03, 0x9c, 0x6d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	browserNormalInput := len(p)
	branch(bneBrowserNormalInput, browserNormalInput)
	branch(bneBrowserNormalInput2, browserNormalInput)
	branch(beqBrowserNormalInput3, browserNormalInput)
	b(0xe2, 0x30, 0xa5, 0x5b, 0xc9, 0x13) // 8-bit A/X/Y; Up
	bneBrowserDown := len(p)
	b(0xd0, 0)
	b(0xad, 0x20, 0x03) // LDA $0320 (selection row)
	bneBrowserUpMove := len(p)
	b(0xd0, 0)          // BNE browserUpMove (not at top)
	b(0xad, 0x4a, 0x03) // LDA $034a (page)
	beqBrowserUpDone := len(p)
	b(0xf0, 0)          // BEQ browserUpDone (already first page)
	b(0xce, 0x4a, 0x03) // DEC $034a (previous page)
	jsrTo(pageRequest)
	b(0x60)
	browserUpMove := len(p)
	branch(bneBrowserUpMove, browserUpMove)
	b(0xce, 0x20, 0x03) // DEC $0320
	emitBrowserRenderCall()
	browserUpDone := len(p)
	branch(beqBrowserUpDone, browserUpDone)
	b(0x60)
	browserDown := len(p)
	branch(bneBrowserDown, browserDown)
	b(0xc9, 0x14) // Down
	bneBrowserPageUp := len(p)
	b(0xd0, 0)
	b(0xee, 0x20, 0x03, 0xad, 0x20, 0x03, 0xcd, 0x1f, 0x03)
	bccBrowserDownRender := len(p)
	b(0x90, 0)
	// Past the last row: advance to the next page when the host reports more.
	b(0xce, 0x20, 0x03) // DEC $0320 (undo the over-step)
	b(0xad, 0x49, 0x03) // LDA $0349 (has_more)
	beqBrowserDownNoMore := len(p)
	b(0xf0, 0)          // BEQ browserDownNoMore
	b(0xee, 0x4a, 0x03) // INC $034a (next page)
	jsrTo(pageRequest)
	b(0x60)
	browserDownNoMore := len(p)
	branch(beqBrowserDownNoMore, browserDownNoMore)
	browserDownRender := len(p)
	branch(bccBrowserDownRender, browserDownRender)
	emitBrowserRenderCall()
	b(0x60)
	browserPageUp := len(p)
	branch(bneBrowserPageUp, browserPageUp)
	b(0xc9, 0x1a) // PageUp
	bneBrowserPageDown := len(p)
	b(0xd0, 0)
	b(0xad, 0x4a, 0x03) // LDA $034a (page)
	beqBrowserPageUpDone := len(p)
	b(0xf0, 0)          // BEQ browserPageUpDone (already first page)
	b(0xce, 0x4a, 0x03) // DEC $034a (previous page)
	jsrTo(pageRequest)
	browserPageUpDone := len(p)
	branch(beqBrowserPageUpDone, browserPageUpDone)
	b(0x60)
	browserPageDown := len(p)
	branch(bneBrowserPageDown, browserPageDown)
	b(0xc9, 0x1b) // PageDown
	bneBrowserBackspace := len(p)
	b(0xd0, 0)
	b(0xad, 0x49, 0x03) // LDA $0349 (has_more)
	beqBrowserPageDownDone := len(p)
	b(0xf0, 0)          // BEQ browserPageDownDone (no more pages)
	b(0xee, 0x4a, 0x03) // INC $034a (next page)
	jsrTo(pageRequest)
	browserPageDownDone := len(p)
	branch(beqBrowserPageDownDone, browserPageDownDone)
	b(0x60)

	browserBackspace := len(p)
	branch(bneBrowserBackspace, browserBackspace)
	b(0xc9, 0x08)
	bneBrowserEnter := len(p)
	b(0xd0, 0)
	// Back: close the browser at the root listing; otherwise pop the parent
	// directory off the back stack and re-list it at page 0.
	b(0xad, 0x4d, 0x03) // LDA $034d (depth)
	bneBackPop := len(p)
	b(0xd0, 0) // BNE backPop
	b(0xad, 0x4e, 0x03, 0xc9, 0x08)
	bneBackCloseBrowser := len(p)
	b(0xd0, 0)
	b(0xa9, 0x10, 0x8d, 0x1d, 0x03) // Recovery History returns to F3 settings.
	braBackRender := len(p)
	b(0x80, 0)
	backCloseBrowser := len(p)
	branch(bneBackCloseBrowser, backCloseBrowser)
	b(0x9c, 0x1d, 0x03) // Other root listings close to the document.
	backRender := len(p)
	branch(braBackRender, backRender)
	emitBrowserRenderCall()
	b(0x60)
	backPop := len(p)
	branch(bneBackPop, backPop)
	b(0xce, 0x4d, 0x03)                   // DEC $034d (pop)
	b(0xae, 0x4d, 0x03)                   // LDX $034d (new depth)
	b(0xbd, 0x4f, 0x03, 0x8d, 0x48, 0x03) // $0348 = len[depth]
	beqBackKind := len(p)
	b(0xf0, 0)                                                    // BEQ backKind (empty root parent)
	b(0x8a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0xaa, 0xa0, 0x00) // X = depth*64; Y = 0
	backCopy := len(p)
	b(0xcc, 0x48, 0x03) // CPY $0348
	bcsBackKind := len(p)
	b(0xb0, 0)                                        // BCS backKind
	b(0xbd, 0x00, 0x1a, 0x99, 0x00, 0x19, 0xe8, 0xc8) // LDA $1a00,X; STA $1900,Y; INX; INY
	braBackCopy := len(p)
	b(0x80, 0)
	backKind := len(p)
	branch(beqBackKind, backKind)
	branch(bcsBackKind, backKind)
	branch(braBackCopy, backCopy)
	b(0xad, 0x4d, 0x03) // LDA $034d (new depth)
	bneBackDir := len(p)
	b(0xd0, 0)                            // BNE backDir
	b(0xad, 0x4e, 0x03, 0x8d, 0x4b, 0x03) // context kind = root kind $034e
	braBackReq := len(p)
	b(0x80, 0)
	backDir := len(p)
	branch(bneBackDir, backDir)
	b(0xa9, 0x00, 0x8d, 0x4b, 0x03) // context kind = listFiles
	backReq := len(p)
	branch(braBackReq, backReq)
	b(0x9c, 0x4a, 0x03, 0x9c, 0x4c, 0x03, 0x9c, 0x49, 0x03) // page 0, flags 0, has_more 0
	jsrTo(pageRequest)
	b(0x60)
	browserEnter := len(p)
	branch(bneBrowserEnter, browserEnter)
	b(0xc9, 0x0d, 0xf0, 0x01, 0x60)
	b(0xad, 0x1f, 0x03, 0xd0, 0x01, 0x60)
	b(0xad, 0x1d, 0x03, 0x85, 0x18) // preserve ready mode across ID copy
	// The per-row flags ($17e0), opaque-ID length ($17e8) and name length
	// ($17f0) tables hold one byte per visible row, exactly as the file-entry
	// decode writes them, so the selected row indexes them directly. Only the
	// $1600 opaque-ID table is 32 bytes per row, and the copy loop below walks
	// it with its own shifted index.
	b(0xae, 0x20, 0x03)                                     // LDX $0320 (selected row)
	b(0xbd, 0xe8, 0x17, 0x8d, 0x15, 0x03, 0x9c, 0x18, 0x03) // payload count = its ID length
	b(0xad, 0x20, 0x03)
	for i := 0; i < 5; i++ {
		b(0x0a)
	}
	b(0xaa)
	b(0xa0, 0x00)
	copyBrowserId := len(p)
	b(0xcc, 0x15, 0x03)
	beqBrowserIdCopied := len(p)
	b(0xf0, 0)
	b(0xbd, 0x00, 0x16, 0x99, 0x00, 0x18, 0xe8, 0xc8)
	braCopyBrowserId := len(p)
	b(0x80, 0)
	browserIdCopied := len(p)
	branch(beqBrowserIdCopied, browserIdCopied)
	branch(braCopyBrowserId, copyBrowserId)
	// Reload the selected row's flags after X walked its ID bytes. This table is
	// also one byte per row, so it must not use the ID table's shifted index.
	b(0xae, 0x20, 0x03) // LDX $0320 (selected row)
	b(0xbd, 0xe0, 0x17, 0x29, 0x01)
	b(0xd0, 0x03) // BNE +3: a directory continues below; a file jumps (range)
	beqBrowserFile := len(p)
	b(0x4c, 0, 0)

	// A directory keeps its browser role (open/save-as/recent) and moves to a
	// matching loading mode while the host lists that opaque catalog ID.
	b(0xa9, 0x00, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa5, 0x18, 0x38, 0xe9, 0x03, 0x8d, 0x1d, 0x03)
	b(0x9c, 0x1f, 0x03, 0x9c, 0x20, 0x03)
	// Push the current listing (parent $1900/$0348) onto the directory-back
	// stack, bounded to three levels, before it is replaced by the new folder.
	b(0xe2, 0x30)       // SEP #$30 (8-bit A/X/Y for the stack arithmetic)
	b(0xae, 0x4d, 0x03) // LDX $034d (depth)
	b(0xe0, 0x03)       // CPX #3
	bcsSkipDirPush := len(p)
	b(0xb0, 0)                            // BCS skipDirPush (stack full)
	b(0xad, 0x48, 0x03, 0x9d, 0x4f, 0x03) // len[depth] = $0348
	beqDirPushInc := len(p)
	b(0xf0, 0)                                                    // BEQ dirPushInc (empty root parent)
	b(0x8a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0xaa, 0xa0, 0x00) // X = depth*64; Y = 0
	dirPushCopy := len(p)
	b(0xcc, 0x48, 0x03) // CPY $0348
	bcsDirPushInc := len(p)
	b(0xb0, 0)                                        // BCS dirPushInc
	b(0xb9, 0x00, 0x19, 0x9d, 0x00, 0x1a, 0xc8, 0xe8) // LDA $1900,Y; STA $1a00,X; INY; INX
	braDirPushCopy := len(p)
	b(0x80, 0)
	dirPushInc := len(p)
	branch(beqDirPushInc, dirPushInc)
	branch(bcsDirPushInc, dirPushInc)
	branch(braDirPushCopy, dirPushCopy)
	b(0xee, 0x4d, 0x03) // INC $034d (depth++)
	skipDirPush := len(p)
	branch(bcsSkipDirPush, skipDirPush)
	// Snapshot this directory as the pagination context: kind listFiles, page 0,
	// and persist its opaque id ($1800 staging) into $1900 for later re-listing.
	b(0xa9, 0x00, 0x8d, 0x4b, 0x03)                         // context kind = listFiles
	b(0x9c, 0x4a, 0x03, 0x9c, 0x4c, 0x03, 0x9c, 0x49, 0x03) // page 0, flags 0, has_more 0
	b(0xad, 0x15, 0x03, 0x8d, 0x48, 0x03)                   // parent len = $0315
	b(0xa0, 0x00)                                           // LDY #0
	dirSnapCopy := len(p)
	b(0xcc, 0x48, 0x03) // CPY $0348
	bcsDirSnapDone := len(p)
	b(0xb0, 0)                                  // BCS dirSnapDone
	b(0xb9, 0x00, 0x18, 0x99, 0x00, 0x19, 0xc8) // LDA $1800,Y; STA $1900,Y; INY
	braDirSnapCopy := len(p)
	b(0x80, 0)
	dirSnapDone := len(p)
	branch(bcsDirSnapDone, dirSnapDone)
	branch(braDirSnapCopy, dirSnapCopy)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	browserFile := len(p)
	jump(beqBrowserFile, browserFile)
	b(0xa5, 0x18, 0xc9, 0x06)
	beqBrowserSaveAs := len(p)
	b(0xf0, 0)
	// Recovery-history rows contain opaque recovery tokens, not catalog IDs.
	b(0xad, 0x4e, 0x03, 0xc9, 0x08)
	bneBrowserOpenFile := len(p)
	b(0xd0, 0)
	b(0xa9, 0x09, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0x9c, 0x1d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)
	browserOpenFile := len(p)
	branch(bneBrowserOpenFile, browserOpenFile)
	// Open and Recent both open the selected file and return to the document.
	b(0xa9, 0x01, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0x9c, 0x1d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)
	browserSaveAs := len(p)
	branch(beqBrowserSaveAs, browserSaveAs)
	// Existing files require a host-issued overwrite event before committing.
	b(0xa9, 0x02, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0x9c, 0x19, 0x03, 0xa9, 0x08, 0x8d, 0x1d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	// Save As New keeps a bounded filename and dialog focus in cartridge WRAM.
	// Name, Save and Cancel are one focus ring shared by keyboard and mouse:
	// Tab/Down/Right advance, Up/Left move back, Enter activates, and typing
	// always returns focus to the name field. It constructs parent-id + NUL +
	// name only at submission time, never exposing a path.
	browserFilenameInput := len(p)
	jump(browserFilenameJump, browserFilenameInput)
	b(0xe2, 0x30, 0xa5, 0x5b)
	b(0xc9, 0x1e) // private mouse-field refresh key
	beqFilenameRefresh := equalJump()
	b(0xc9, 0x09) // Tab
	beqFilenameNext := equalJump()
	b(0xc9, 0x12) // Right
	beqFilenameNext2 := equalJump()
	b(0xc9, 0x14) // Down
	beqFilenameNext3 := equalJump()
	b(0xc9, 0x11) // Left
	beqFilenamePrevious := equalJump()
	b(0xc9, 0x13) // Up
	beqFilenamePrevious2 := equalJump()
	b(0xc9, 0x08) // Backspace / Back
	beqFilenameBack := equalJump()
	b(0xc9, 0x0d) // Enter
	beqFilenameEnter := equalJump()
	b(0xc9, 0x20, 0xb0, 0x01, 0x60) // printable lower bound or return
	b(0xc9, 0x7f, 0x90, 0x01, 0x60) // printable upper bound or return
	b(0x85, 0x1b, 0x9c, 0x6d, 0x03, 0xad, 0x31, 0x03, 0xc9, 24)
	b(0x90, 0x01, 0x60) // visible, bounded filename buffer or return
	b(0xae, 0x31, 0x03, 0xa5, 0x1b, 0x9d, 0x80, 0x18, 0xee, 0x31, 0x03)
	filenameRefresh := len(p)
	jump(beqFilenameRefresh, filenameRefresh)
	emitBrowserRenderCall()
	b(0x60)
	filenameNext := len(p)
	jump(beqFilenameNext, filenameNext)
	jump(beqFilenameNext2, filenameNext)
	jump(beqFilenameNext3, filenameNext)
	b(0xee, 0x6d, 0x03, 0xad, 0x6d, 0x03, 0xc9, 0x03)
	bccFilenameFocusReady := len(p)
	b(0x90, 0)
	b(0x9c, 0x6d, 0x03)
	filenameFocusReady := len(p)
	branch(bccFilenameFocusReady, filenameFocusReady)
	emitBrowserRenderCall()
	b(0x60)
	filenamePrevious := len(p)
	jump(beqFilenamePrevious, filenamePrevious)
	jump(beqFilenamePrevious2, filenamePrevious)
	b(0xad, 0x6d, 0x03)
	bneFilenamePreviousMove := len(p)
	b(0xd0, 0)
	b(0xa9, 0x02, 0x8d, 0x6d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	filenamePreviousMove := len(p)
	branch(bneFilenamePreviousMove, filenamePreviousMove)
	b(0xce, 0x6d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	filenameBack := len(p)
	jump(beqFilenameBack, filenameBack)
	// Backspace edits only while the name field owns focus. From either button,
	// or from an already-empty field, Back is the dialog-wide Cancel action.
	b(0xad, 0x6d, 0x03)
	bneFilenameCancel := len(p)
	b(0xd0, 0)
	b(0xad, 0x31, 0x03)
	beqFilenameCancel := len(p)
	b(0xf0, 0)
	b(0xce, 0x31, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	filenameCancel := len(p)
	branch(bneFilenameCancel, filenameCancel)
	branch(beqFilenameCancel, filenameCancel)
	b(0xa9, 0x06, 0x8d, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	filenameEnter := len(p)
	jump(beqFilenameEnter, filenameEnter)
	b(0xad, 0x6d, 0x03, 0xc9, 0x02)
	beqFilenameCancelFromEnter := len(p)
	b(0xf0, 0)
	branch(beqFilenameCancelFromEnter, filenameCancel)
	b(0xad, 0x31, 0x03)
	bneFilenameHasName := len(p)
	b(0xd0, 0)
	emitBrowserRenderCall() // empty Save still reveals its selected-button highlight
	b(0x60)
	filenameHasName := len(p)
	branch(bneFilenameHasName, filenameHasName)
	b(0x9c, 0x18, 0x03, 0xa0, 0x00)
	copyFilenameParent := len(p)
	b(0xcc, 0x48, 0x03)
	bcsFilenameParentDone := len(p)
	b(0xb0, 0)
	b(0xb9, 0x00, 0x19, 0x99, 0x00, 0x18, 0xc8)
	braCopyFilenameParent := len(p)
	b(0x80, 0)
	filenameParentDone := len(p)
	branch(bcsFilenameParentDone, filenameParentDone)
	branch(braCopyFilenameParent, copyFilenameParent)
	b(0xa9, 0x00, 0x99, 0x00, 0x18, 0xc8, 0xa2, 0x00)
	copyFilenameBytes := len(p)
	b(0xec, 0x31, 0x03)
	bcsFilenameBytesDone := len(p)
	b(0xb0, 0)
	b(0xbd, 0x80, 0x18, 0x99, 0x00, 0x18, 0xe8, 0xc8)
	braCopyFilenameBytes := len(p)
	b(0x80, 0)
	filenameBytesDone := len(p)
	branch(bcsFilenameBytesDone, filenameBytesDone)
	branch(braCopyFilenameBytes, copyFilenameBytes)
	// Save As names are base names. The cartridge appends the selected
	// extension, so the host never has to guess the intended format.
	b(0xad, 0x2f, 0x03)
	beqFilenameExtOdt := len(p)
	b(0xf0, 0)
	b(0xc9, 0x01, 0xd0, 0x03)
	filenameExtDocxJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x02, 0xd0, 0x03)
	filenameExtRtfJump := len(p)
	b(0x4c, 0, 0)
	filenameExtMarkdownJump := len(p)
	b(0x4c, 0, 0)
	appendFilenameExtension := func(extension string) {
		for _, ch := range []byte(extension) {
			b(0xa9, ch, 0x99, 0x00, 0x18, 0xc8)
		}
	}
	filenameExtOdt := len(p)
	branch(beqFilenameExtOdt, filenameExtOdt)
	appendFilenameExtension(".odt")
	filenameExtDoneJump1 := len(p)
	b(0x4c, 0, 0)
	filenameExtDocx := len(p)
	jump(filenameExtDocxJump, filenameExtDocx)
	appendFilenameExtension(".docx")
	filenameExtDoneJump2 := len(p)
	b(0x4c, 0, 0)
	filenameExtRtf := len(p)
	jump(filenameExtRtfJump, filenameExtRtf)
	appendFilenameExtension(".rtf")
	filenameExtDoneJump3 := len(p)
	b(0x4c, 0, 0)
	filenameExtMarkdown := len(p)
	jump(filenameExtMarkdownJump, filenameExtMarkdown)
	appendFilenameExtension(".md")
	filenameExtDone := len(p)
	jump(filenameExtDoneJump1, filenameExtDone)
	jump(filenameExtDoneJump2, filenameExtDone)
	jump(filenameExtDoneJump3, filenameExtDone)
	b(0x8c, 0x15, 0x03) // payload count is final Y: parent + NUL + base + extension
	b(0xa9, 0x0a, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0x9c, 0x19, 0x03, 0x9c, 0x1d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)
	// The saved opaque ID remains staged at $1800 while the host verifies an
	// existing target. Confirmation sends the same command with flag bit zero
	// set; cancellation keeps the visible Save As page intact.
	browserOverwriteConfirm := len(p)
	jump(browserConfirmJump, browserOverwriteConfirm)
	b(0xe2, 0x30, 0xa5, 0x5b, 0xc9, 0x08)
	bneOverwriteConfirmEnter := len(p)
	b(0xd0, 0)
	b(0xa9, 0x06, 0x8d, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	overwriteConfirmEnter := len(p)
	branch(bneOverwriteConfirmEnter, overwriteConfirmEnter)
	b(0xc9, 0x0d, 0xf0, 0x01, 0x60)
	b(0xa9, 0x02, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x01, 0x8d, 0x19, 0x03, 0x9c, 0x1d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	// Outcome dialog for host failures. The host only supplies an event kind;
	// the cartridge keeps the return mode and owns all visible wording.
	browserOutcome := len(p)
	jump(browserOutcomeJump, browserOutcome)
	b(0xe2, 0x30, 0xa5, 0x5b, 0xc9, 0x08)
	beqOutcomeDismiss := len(p)
	b(0xf0, 0)
	b(0xc9, 0x0d)
	bneOutcomeIgnore := len(p)
	b(0xd0, 0)
	// Enter on the recovery-available dialog requests the restore. The opaque
	// token "current" is the only recovery handle the host exposes; the host
	// answers with EventRecoveryRestored and a fresh authoritative viewport.
	b(0xad, 0x37, 0x03, 0xc9, 0x04)
	bneOutcomeDismiss2 := len(p)
	b(0xd0, 0)
	for i, ch := range []byte("current") {
		ldaSta(ch, uint16(0x1800+i))
	}
	b(0xa9, 0x09, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x07, 0x8d, 0x15, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x19, 0x03)
	jsrTo(commandWrite)
	outcomeDismiss := len(p)
	branch(beqOutcomeDismiss, outcomeDismiss)
	branch(bneOutcomeDismiss2, outcomeDismiss)
	b(0xad, 0x38, 0x03, 0x8d, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	outcomeIgnore := len(p)
	branch(bneOutcomeIgnore, outcomeIgnore)
	b(0x60)

	// Statistics is a read-only dialog: Enter or Back returns to the document.
	browserStats := len(p)
	jump(browserStatsJump, browserStats)
	b(0xe2, 0x30, 0xa5, 0x5b, 0xc9, 0x08)
	beqStatsDismiss := len(p)
	b(0xf0, 0)
	b(0xc9, 0x0d)
	bneStatsIgnore := len(p)
	b(0xd0, 0)
	statsDismiss := len(p)
	branch(beqStatsDismiss, statsDismiss)
	b(0x9c, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	statsIgnore := len(p)
	branch(bneStatsIgnore, statsIgnore)
	b(0x60)

	// Find entry keeps the query in guest WRAM at $18c0 until Enter publishes
	// one CommandFindNext record; the host selects the match and the committed
	// viewport renders that selection. Backspace edits, or cancels when empty.
	browserFind := len(p)
	jump(browserFindJump, browserFind)
	b(0xe2, 0x30, 0xa5, 0x5b, 0xc9, 0x08)
	bneFindEnter := len(p)
	b(0xd0, 0)
	b(0xad, 0x32, 0x03)
	beqFindCancel := len(p)
	b(0xf0, 0)
	b(0xce, 0x32, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	findCancel := len(p)
	branch(beqFindCancel, findCancel)
	b(0x9c, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	findEnter := len(p)
	branch(bneFindEnter, findEnter)
	b(0xc9, 0x0d)
	bneFindPrintable := len(p)
	b(0xd0, 0)
	b(0xad, 0x32, 0x03)
	bneFindSubmit := len(p)
	b(0xd0, 0)
	b(0x60) // an empty query has nothing to submit
	findSubmit := len(p)
	branch(bneFindSubmit, findSubmit)
	b(0x8d, 0x15, 0x03, 0x9c, 0x18, 0x03) // payload count = query length
	b(0xa0, 0x00)
	copyFindQuery := len(p)
	b(0xcc, 0x32, 0x03)
	bcsFindQueryDone := len(p)
	b(0xb0, 0)
	b(0xb9, 0xc0, 0x18, 0x99, 0x00, 0x18, 0xc8)
	braCopyFindQuery := len(p)
	b(0x80, 0)
	findQueryDone := len(p)
	branch(bcsFindQueryDone, findQueryDone)
	branch(braCopyFindQuery, copyFindQuery)
	b(0xa9, 0x23, 0x8d, 0x16, 0x03, 0x9c, 0x17, 0x03) // kind = FindNext (35)
	b(0x9c, 0x19, 0x03, 0x9c, 0x1d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)
	findPrintable := len(p)
	branch(bneFindPrintable, findPrintable)
	b(0xc9, 0x20)
	bccFindIgnore := len(p)
	b(0x90, 0)
	b(0xc9, 0x7f)
	bcsFindIgnore := len(p)
	b(0xb0, 0)
	b(0x85, 0x1b, 0xad, 0x32, 0x03, 0xc9, 30)
	bcsFindIgnore2 := len(p)
	b(0xb0, 0)
	b(0xae, 0x32, 0x03, 0xa5, 0x1b, 0x9d, 0xc0, 0x18, 0xee, 0x32, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	findIgnore := len(p)
	branch(bccFindIgnore, findIgnore)
	branch(bcsFindIgnore, findIgnore)
	branch(bcsFindIgnore2, findIgnore)
	b(0x60)

	// Save/Recovery settings (mode $10) are cartridge state projected from the
	// typed host settings event. Changes cross the mailbox as one complete
	// three-byte settings value, so mode/interval/retention cannot tear.
	settingsInput := len(p)
	jump(settingsInputJump, settingsInput)
	b(0xe2, 0x30, 0xa5, 0x5b)
	b(0xc9, 0x08, 0xd0, 0x03)
	settingsBackJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x13, 0xd0, 0x03)
	settingsUpJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x14, 0xd0, 0x03)
	settingsDownJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x11, 0xd0, 0x03)
	settingsLeftJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x12, 0xd0, 0x03)
	settingsRightJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0d, 0xd0, 0x03)
	settingsEnterJump := len(p)
	b(0x4c, 0, 0)
	b(0x60)

	settingsBack := len(p)
	jump(settingsBackJump, settingsBack)
	b(0xad, 0x14, 0x03, 0x8d, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)

	settingsUp := len(p)
	jump(settingsUpJump, settingsUp)
	b(0xad, 0x1a, 0x03)
	beqSettingsUpRender := len(p)
	b(0xf0, 0)
	b(0xce, 0x1a, 0x03)
	settingsUpRender := len(p)
	branch(beqSettingsUpRender, settingsUpRender)
	emitBrowserRenderCall()
	b(0x60)

	settingsDown := len(p)
	jump(settingsDownJump, settingsDown)
	b(0xad, 0x1a, 0x03, 0xc9, 0x04)
	bcsSettingsDownRender := len(p)
	b(0xb0, 0)
	b(0xee, 0x1a, 0x03)
	settingsDownRender := len(p)
	branch(bcsSettingsDownRender, settingsDownRender)
	emitBrowserRenderCall()
	b(0x60)

	settingsLeft := len(p)
	jump(settingsLeftJump, settingsLeft)
	b(0x64, 0x1b)
	settingsChangeJump1 := len(p)
	b(0x4c, 0, 0)
	settingsRight := len(p)
	jump(settingsRightJump, settingsRight)
	b(0xa9, 0x01, 0x85, 0x1b)
	settingsChange := len(p)
	jump(settingsChangeJump1, settingsChange)
	b(0xad, 0x1a, 0x03)
	bneSettingsNotMode := len(p)
	b(0xd0, 0)
	b(0xad, 0x1b, 0x03, 0x49, 0x01, 0x8d, 0x1b, 0x03)
	settingsEmitConfigJump1 := len(p)
	b(0x4c, 0, 0)
	settingsNotMode := len(p)
	branch(bneSettingsNotMode, settingsNotMode)
	b(0xc9, 0x01)
	bneSettingsNotInterval := len(p)
	b(0xd0, 0)
	b(0xa5, 0x1b)
	bneSettingsIntervalInc := len(p)
	b(0xd0, 0)
	b(0xad, 0x1c, 0x03, 0xc9, 0x01)
	bneSettingsIntervalDec := len(p)
	b(0xd0, 0)
	b(0xa9, 0xff, 0x8d, 0x1c, 0x03)
	settingsEmitConfigJump2 := len(p)
	b(0x4c, 0, 0)
	settingsIntervalDec := len(p)
	branch(bneSettingsIntervalDec, settingsIntervalDec)
	b(0xce, 0x1c, 0x03)
	settingsEmitConfigJump3 := len(p)
	b(0x4c, 0, 0)
	settingsIntervalInc := len(p)
	branch(bneSettingsIntervalInc, settingsIntervalInc)
	b(0xee, 0x1c, 0x03)
	bneSettingsEmitConfig1 := len(p)
	b(0xd0, 0)
	b(0xee, 0x1c, 0x03) // 255+1 wraps; zero is not a legal interval.
	settingsEmitConfigJump4 := len(p)
	b(0x4c, 0, 0)
	settingsNotInterval := len(p)
	branch(bneSettingsNotInterval, settingsNotInterval)
	b(0xc9, 0x02)
	bneSettingsNotCopies := len(p)
	b(0xd0, 0)
	b(0xa5, 0x1b)
	beqSettingsCopiesDec := len(p)
	b(0xf0, 0)
	b(0xee, 0x2b, 0x03)
	settingsEmitConfigJump5 := len(p)
	b(0x4c, 0, 0)
	settingsCopiesDec := len(p)
	branch(beqSettingsCopiesDec, settingsCopiesDec)
	b(0xce, 0x2b, 0x03)
	settingsEmitConfigJump6 := len(p)
	b(0x4c, 0, 0)
	settingsNotCopies := len(p)
	branch(bneSettingsNotCopies, settingsNotCopies)
	b(0xc9, 0x04)
	bneSettingsChangeDone := len(p)
	b(0xd0, 0)
	b(0xad, 0x69, 0x03, 0xc9, 0x04)
	bneSettingsChangeDone2 := len(p)
	b(0xd0, 0)
	b(0xad, 0x2e, 0x03, 0x49, 0x01, 0x8d, 0x2e, 0x03)
	settingsEmitMarkdownJump := len(p)
	b(0x4c, 0, 0)
	settingsChangeDone := len(p)
	branch(bneSettingsChangeDone, settingsChangeDone)
	branch(bneSettingsChangeDone2, settingsChangeDone)
	emitBrowserRenderCall()
	b(0x60)

	settingsEnter := len(p)
	jump(settingsEnterJump, settingsEnter)
	b(0xad, 0x1a, 0x03, 0xc9, 0x03)
	bneSettingsEnterDone := len(p)
	b(0xd0, 0)
	// Recovery History reuses the paged cartridge browser with source kind 8.
	// Loading mode 4 becomes ready mode 7 when the host completes the page; the
	// root kind distinguishes recovery selection from Recent/Open.
	b(0xa9, 0x08, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x04, 0x8d, 0x1d, 0x03)
	b(0xa9, 0x08, 0x8d, 0x4b, 0x03, 0x8d, 0x4e, 0x03)
	b(0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x19, 0x03)
	b(0x9c, 0x4a, 0x03, 0x9c, 0x4c, 0x03, 0x9c, 0x48, 0x03)
	b(0x9c, 0x49, 0x03, 0x9c, 0x4d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)
	settingsEnterDone := len(p)
	branch(bneSettingsEnterDone, settingsEnterDone)
	b(0x60)

	settingsEmitConfig := len(p)
	for _, at := range []int{
		settingsEmitConfigJump1, settingsEmitConfigJump2, settingsEmitConfigJump3,
		settingsEmitConfigJump4, settingsEmitConfigJump5, settingsEmitConfigJump6,
	} {
		jump(at, settingsEmitConfig)
	}
	branch(bneSettingsEmitConfig1, settingsEmitConfig)
	b(0xad, 0x1b, 0x03, 0x8d, 0x00, 0x18)
	b(0xad, 0x1c, 0x03, 0x8d, 0x01, 0x18)
	b(0xad, 0x2b, 0x03, 0x8d, 0x02, 0x18)
	b(0xa9, 0x12, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x03, 0x8d, 0x15, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x19, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	settingsEmitMarkdown := len(p)
	jump(settingsEmitMarkdownJump, settingsEmitMarkdown)
	b(0xad, 0x2e, 0x03, 0x8d, 0x00, 0x18)
	b(0xa9, 0x13, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x01, 0x8d, 0x15, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x19, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	// Save As format selection (mode $11). Enter begins a format-filtered file
	// listing; the selected format is also used to append the extension later.
	saveFormatInput := len(p)
	jump(saveFormatInputJump, saveFormatInput)
	b(0xe2, 0x30, 0xa5, 0x5b)
	b(0xc9, 0x08, 0xd0, 0x03)
	saveFormatBackJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x13, 0xd0, 0x03)
	saveFormatUpJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x14, 0xd0, 0x03)
	saveFormatDownJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0d, 0xd0, 0x03)
	saveFormatEnterJump := len(p)
	b(0x4c, 0, 0)
	b(0x60)
	saveFormatBack := len(p)
	jump(saveFormatBackJump, saveFormatBack)
	b(0xad, 0x14, 0x03, 0x8d, 0x1d, 0x03)
	emitBrowserRenderCall()
	b(0x60)
	saveFormatUp := len(p)
	jump(saveFormatUpJump, saveFormatUp)
	b(0xad, 0x2f, 0x03)
	beqSaveFormatUpDone := len(p)
	b(0xf0, 0)
	b(0xce, 0x2f, 0x03)
	saveFormatUpDone := len(p)
	branch(beqSaveFormatUpDone, saveFormatUpDone)
	emitBrowserRenderCall()
	b(0x60)
	saveFormatDown := len(p)
	jump(saveFormatDownJump, saveFormatDown)
	b(0xad, 0x2f, 0x03, 0xc9, 0x03)
	bcsSaveFormatDownDone := len(p)
	b(0xb0, 0)
	b(0xee, 0x2f, 0x03)
	saveFormatDownDone := len(p)
	branch(bcsSaveFormatDownDone, saveFormatDownDone)
	emitBrowserRenderCall()
	b(0x60)
	saveFormatEnter := len(p)
	jump(saveFormatEnterJump, saveFormatEnter)
	b(0xa9, 0x07, 0x8d, 0x16, 0x03, 0x8d, 0x4b, 0x03, 0x8d, 0x4e, 0x03)
	b(0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xad, 0x2f, 0x03, 0x1a, 0x0a, 0x8d, 0x68, 0x03, 0x8d, 0x19, 0x03)
	b(0xa9, 0x03, 0x8d, 0x1d, 0x03)
	b(0x9c, 0x15, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x1f, 0x03, 0x9c, 0x20, 0x03)
	b(0x9c, 0x4a, 0x03, 0x9c, 0x4c, 0x03, 0x9c, 0x48, 0x03, 0x9c, 0x49, 0x03, 0x9c, 0x4d, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	// One transition dialog owns every operation that would replace a dirty
	// document. The decision byte is Checkpoint/Save/Discard/Cancel = 0..3.
	transitionInput := len(p)
	jump(transitionInputJump, transitionInput)
	b(0xe2, 0x30, 0xa5, 0x5b)
	b(0xc9, 0x08, 0xd0, 0x03)
	transitionCancelJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x13, 0xd0, 0x03)
	transitionUpJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x14, 0xd0, 0x03)
	transitionDownJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0d, 0xd0, 0x03)
	transitionSubmitJump := len(p)
	b(0x4c, 0, 0)
	b(0x60)
	transitionUp := len(p)
	jump(transitionUpJump, transitionUp)
	b(0xad, 0x1a, 0x03)
	beqTransitionUpDone := len(p)
	b(0xf0, 0)
	b(0xce, 0x1a, 0x03)
	transitionUpDone := len(p)
	branch(beqTransitionUpDone, transitionUpDone)
	emitBrowserRenderCall()
	b(0x60)
	transitionDown := len(p)
	jump(transitionDownJump, transitionDown)
	b(0xad, 0x1a, 0x03, 0xc9, 0x03)
	bcsTransitionDownDone := len(p)
	b(0xb0, 0)
	b(0xee, 0x1a, 0x03)
	transitionDownDone := len(p)
	branch(bcsTransitionDownDone, transitionDownDone)
	emitBrowserRenderCall()
	b(0x60)
	transitionCancel := len(p)
	jump(transitionCancelJump, transitionCancel)
	b(0xa9, 0x03)
	transitionEmitJump := len(p)
	b(0x4c, 0, 0)
	transitionSubmit := len(p)
	jump(transitionSubmitJump, transitionSubmit)
	b(0xad, 0x1a, 0x03)
	transitionEmit := len(p)
	jump(transitionEmitJump, transitionEmit)
	b(0x8d, 0x00, 0x18)
	b(0xc9, 0x01)
	bneTransitionNotSave := len(p)
	b(0xd0, 0)
	b(0xa9, 0x01, 0x8d, 0x6a, 0x03)
	braTransitionSaveFlagDone := len(p)
	b(0x80, 0)
	transitionNotSave := len(p)
	branch(bneTransitionNotSave, transitionNotSave)
	b(0x9c, 0x6a, 0x03)
	transitionSaveFlagDone := len(p)
	branch(braTransitionSaveFlagDone, transitionSaveFlagDone)
	b(0x9c, 0x1d, 0x03)
	b(0xa9, 0x14, 0x8d, 0x16, 0x03, 0xa9, 0x01, 0x8d, 0x17, 0x03)
	b(0xa9, 0x01, 0x8d, 0x15, 0x03, 0x9c, 0x18, 0x03, 0x9c, 0x19, 0x03)
	jsrTo(commandWrite)
	emitBrowserRenderCall()
	b(0x60)

	// event_consume accepts one complete host record per main-loop pass.
	// Unknown or malformed records advance the committed consumer without
	// touching browser state. File entries are bounded to one seven-row page.
	var eventReadCalls []int
	eventRead := func() {
		eventReadCalls = append(eventReadCalls, len(p))
		b(0x20, 0, 0)
	}
	eventConsume := len(p)
	patch16(eventCall, eventConsume)
	b(0xc2, 0x30, 0xaf, 0x08, 0x00, 0x70, 0xc9, 0x00, 0x20)
	bccEventConsumerValid := len(p)
	b(0x90, 0)
	b(0xa9, 0x00, 0x00)
	eventConsumerValid := len(p)
	branch(bccEventConsumerValid, eventConsumerValid)
	// Keep the 16-bit event cursor away from the persistent settings bytes at
	// $032a/$032b. The earlier scratch word overlapped retained-copy count at
	// $032b, so parsing the Settings event changed the cursor's high byte and
	// committed an impossible consumer index.
	b(0xaa, 0x8e, 0x6b, 0x03, 0xcf, 0x06, 0x00, 0x70, 0xe2, 0x20)
	bneEventAvailable := len(p)
	b(0xd0, 0)
	b(0xe2, 0x10, 0x60)
	eventAvailable := len(p)
	branch(bneEventAvailable, eventAvailable)
	b(0xa9, 0x01, 0x8d, 0x29, 0x03)
	checkEventByte := func(want byte) {
		eventRead()
		b(0xc9, want, 0xf0, 0x03, 0x9c, 0x29, 0x03)
	}
	checkEventByte(0x01)
	checkEventByte(0x00)
	eventRead()
	sta(0x0321)
	eventRead()
	sta(0x0322)
	eventRead()
	sta(0x0323)
	eventRead()
	sta(0x0324)
	b(0xa0, 0x0e, 0x00)
	skipEventHeader := len(p)
	eventRead()
	b(0x88)
	bneSkipEventHeader := len(p)
	b(0xd0, 0)
	branch(bneSkipEventHeader, skipEventHeader)
	b(0xad, 0x29, 0x03)
	b(0xd0, 0x03)
	eventCommitJump1 := len(p)
	b(0x4c, 0, 0)
	b(0xad, 0x22, 0x03, 0xc9, 0x82, 0xf0, 0x03)
	eventCommitJump2 := len(p)
	b(0x4c, 0, 0)
	b(0xad, 0x21, 0x03, 0xc9, 0x00)
	bneEventCompleteCheck := len(p)
	b(0xd0, 0x03)
	fileEventJump := len(p)
	b(0x4c, 0, 0)
	eventCompleteCheck := len(p)
	branch(bneEventCompleteCheck, eventCompleteCheck)
	// Host file replies are 0x8200/0x820f; 0x8206 confirms an overwrite.
	// 0x8207-09 are cartridge-owned visible outcomes, never native alerts.
	b(0xc9, 0x0f)
	bneCompleteKind := len(p)
	b(0xd0, 0)
	completeEventJump := len(p)
	b(0x4c, 0, 0)
	completeKindDone := len(p)
	branch(bneCompleteKind, completeKindDone)
	b(0xc9, 0x06)
	bneOverwriteKind := len(p)
	b(0xd0, 0)
	overwriteEventJump := len(p)
	b(0x4c, 0, 0)
	overwriteKindDone := len(p)
	branch(bneOverwriteKind, overwriteKindDone)
	// Kinds 04/05 (recovery available/restored) and 07/08/09 (save conflict,
	// read-only, open failed) all become the cartridge outcome dialog. The
	// earlier version only routed 09: the 07/08 equality branches skipped past
	// the dialog jump straight to the commit, so their dialog texts were
	// unreachable.
	b(0xc9, 0x04)
	beqOutcomeKind3 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x05)
	beqOutcomeKind4 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x07)
	beqOutcomeKind := len(p)
	b(0xf0, 0)
	b(0xc9, 0x08)
	beqOutcomeKind2 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x09)
	beqOutcomeKind5 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x11)
	beqOutcomeKind6 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x10, 0xd0, 0x03)
	saveAsRequiredEventJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x12, 0xd0, 0x03)
	settingsEventJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x13, 0xd0, 0x03)
	transitionEventJump := len(p)
	b(0x4c, 0, 0)
	outcomeKindJump := len(p)
	b(0x4c, 0, 0)
	outcomeEventJump := len(p)
	b(0x4c, 0, 0)
	branch(beqOutcomeKind, outcomeEventJump)
	branch(beqOutcomeKind2, outcomeEventJump)
	branch(beqOutcomeKind3, outcomeEventJump)
	branch(beqOutcomeKind4, outcomeEventJump)
	branch(beqOutcomeKind5, outcomeEventJump)
	branch(beqOutcomeKind6, outcomeEventJump)
	outcomeKind := len(p)
	jump(outcomeKindJump, outcomeKind)
	eventCommitJump3 := len(p)
	b(0x4c, 0, 0)

	fileEvent := len(p)
	jump(fileEventJump, fileEvent)
	b(0xad, 0x1f, 0x03, 0xc9, 0x07, 0x90, 0x03)
	fileCommitJump1 := len(p)
	b(0x4c, 0, 0)
	eventRead()
	sta(0x0325) // opaque ID bytes
	eventRead()
	sta(0x0326) // UTF-8 name bytes, low
	eventRead()
	sta(0x0327) // UTF-8 name bytes, high
	eventRead()
	sta(0x0328) // directory/writable flags
	b(0xad, 0x25, 0x03, 0xd0, 0x03)
	invalidFileEventJump1 := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x21, 0x90, 0x03)
	invalidFileEventJump2 := len(p)
	b(0x4c, 0, 0)
	b(0xad, 0x27, 0x03, 0xf0, 0x03)
	invalidFileEventJump3 := len(p)
	b(0x4c, 0, 0)
	b(0xc2, 0x20, 0xad, 0x25, 0x03, 0x29, 0xff, 0x00, 0x18)
	b(0x6d, 0x26, 0x03, 0x69, 0x14, 0x00, 0xcd, 0x23, 0x03, 0xe2, 0x20)
	b(0xf0, 0x03)
	invalidFileEventJump4 := len(p)
	b(0x4c, 0, 0)
	b(0xa0, 0x10, 0x00)
	skipFileMetadata := len(p)
	eventRead()
	b(0x88)
	bneSkipFileMetadata := len(p)
	b(0xd0, 0)
	branch(bneSkipFileMetadata, skipFileMetadata)

	b(0xad, 0x25, 0x03, 0x85, 0x19, 0xc2, 0x20)
	b(0xad, 0x1f, 0x03, 0x29, 0xff, 0x00)
	for i := 0; i < 5; i++ {
		b(0x0a)
	}
	b(0xa8, 0xe2, 0x20)
	copyFileId := len(p)
	eventRead()
	b(0x99, 0x00, 0x16, 0xc8, 0xc6, 0x19)
	bneCopyFileId := len(p)
	b(0xd0, 0)
	branch(bneCopyFileId, copyFileId)

	b(0xad, 0x26, 0x03, 0x85, 0x19, 0x64, 0x1a, 0xc2, 0x20)
	b(0xad, 0x1f, 0x03, 0x29, 0xff, 0x00, 0x0a, 0x85, 0x17)
	for i := 0; i < 4; i++ {
		b(0x0a)
	}
	b(0x38, 0xe5, 0x17, 0xa8, 0xe2, 0x20)
	b(0xa5, 0x19)
	beqFileNameCopied := len(p)
	b(0xf0, 0)
	copyFileName := len(p)
	eventRead()
	b(0x48, 0xa5, 0x1a, 0xc9, 29)
	bcsSkipFileNameStore := len(p)
	b(0xb0, 0)
	b(0x68, 0x99, 0x00, 0x17, 0xc8, 0xe6, 0x1a)
	braFileNameByteDone := len(p)
	b(0x80, 0)
	skipFileNameStore := len(p)
	branch(bcsSkipFileNameStore, skipFileNameStore)
	b(0x68)
	fileNameByteDone := len(p)
	branch(braFileNameByteDone, fileNameByteDone)
	b(0xc6, 0x19)
	bneCopyFileName := len(p)
	b(0xd0, 0)
	branch(bneCopyFileName, copyFileName)
	fileNameCopied := len(p)
	branch(beqFileNameCopied, fileNameCopied)
	b(0xc2, 0x20, 0xad, 0x1f, 0x03, 0x29, 0xff, 0x00, 0xa8, 0xe2, 0x20)
	b(0xad, 0x28, 0x03, 0x99, 0xe0, 0x17)
	b(0xad, 0x25, 0x03, 0x99, 0xe8, 0x17)
	b(0xa5, 0x1a, 0x99, 0xf0, 0x17)
	b(0xee, 0x1f, 0x03)
	fileEventDoneJump := len(p)
	b(0x4c, 0, 0)

	invalidFileEvent := len(p)
	for _, at := range []int{
		invalidFileEventJump1, invalidFileEventJump2,
		invalidFileEventJump3, invalidFileEventJump4,
	} {
		jump(at, invalidFileEvent)
	}
	invalidFileCommitJump := len(p)
	b(0x4c, 0, 0)

	completeEvent := len(p)
	jump(completeEventJump, completeEvent)
	b(0xad, 0x24, 0x03)
	bneCompleteEventInvalid := len(p)
	b(0xd0, 0)
	b(0xad, 0x23, 0x03, 0xc9, 0x0b)
	bneCompleteEventInvalid2 := len(p)
	b(0xd0, 0)
	b(0xad, 0x1d, 0x03, 0xc9, 0x02)
	bccCompleteEventInvalid := len(p)
	b(0x90, 0)
	b(0xc9, 0x05)
	bcsCompleteEventInvalid := len(p)
	b(0xb0, 0)
	// Capture the has-more flag (payload byte 10) so the browser can page. The
	// 11-byte payload is total(4), begin(4), count, source, has_more.
	for skip := 0; skip < 10; skip++ {
		eventRead()
	}
	eventRead()
	sta(0x0349)
	b(0xad, 0x1d, 0x03) // reload mode (the payload reads clobbered A)
	b(0x18, 0x69, 0x03, 0x8d, 0x1d, 0x03, 0x9c, 0x20, 0x03, 0xe2, 0x10)
	eventRenderCall := len(p)
	b(0x20, 0, 0, 0xc2, 0x10)
	completeEventInvalid := len(p)
	branch(bneCompleteEventInvalid, completeEventInvalid)
	branch(bneCompleteEventInvalid2, completeEventInvalid)
	branch(bccCompleteEventInvalid, completeEventInvalid)
	branch(bcsCompleteEventInvalid, completeEventInvalid)

	overwriteEvent := len(p)
	jump(overwriteEventJump, overwriteEvent)
	// The host echo is advisory: the outstanding staged ID is the exact command
	// payload already selected by the cartridge. Accept only the bounded opaque
	// ID shape expected from FileCatalog and only while Save As is waiting.
	b(0xad, 0x1d, 0x03, 0xc9, 0x08)
	bneOverwriteEventInvalid := len(p)
	b(0xd0, 0)
	b(0xad, 0x24, 0x03)
	bneOverwriteEventInvalid2 := len(p)
	b(0xd0, 0)
	b(0xad, 0x23, 0x03)
	beqOverwriteEventInvalid := len(p)
	b(0xf0, 0)
	b(0xc9, 0x21)
	bcsOverwriteEventInvalid := len(p)
	b(0xb0, 0)
	b(0xa9, 0x09, 0x8d, 0x1d, 0x03, 0xe2, 0x10)
	overwriteRenderCall := len(p)
	b(0x20, 0, 0, 0xc2, 0x10)
	overwriteEventCommitJump := len(p)
	b(0x4c, 0, 0)
	overwriteEventInvalid := len(p)
	branch(bneOverwriteEventInvalid, overwriteEventInvalid)
	branch(bneOverwriteEventInvalid2, overwriteEventInvalid)
	branch(beqOverwriteEventInvalid, overwriteEventInvalid)
	branch(bcsOverwriteEventInvalid, overwriteEventInvalid)
	overwriteEventInvalidCommitJump := len(p)
	b(0x4c, 0, 0)

	outcomeEvent := len(p)
	jump(outcomeEventJump, outcomeEvent)
	// The record header already supplies the low event byte at $0321.  Keep
	// the pre-outcome mode so Enter/Back restores exactly the cartridge screen
	// the user was using when the host rejected the operation.
	b(0xad, 0x1d, 0x03, 0x8d, 0x38, 0x03)
	// A dialog that interrupts the transient SAVING screen returns to the
	// document; SAVING itself is never a restore target.
	b(0xc9, 0x0c, 0xd0, 0x03, 0x9c, 0x38, 0x03)
	b(0xad, 0x21, 0x03, 0x8d, 0x37, 0x03)
	b(0xa9, 0x0b, 0x8d, 0x1d, 0x03, 0xe2, 0x10)
	outcomeRenderCall := len(p)
	b(0x20, 0, 0, 0xc2, 0x10)
	outcomeEventCommitJump := len(p)
	b(0x4c, 0, 0)

	saveAsRequiredEvent := len(p)
	jump(saveAsRequiredEventJump, saveAsRequiredEvent)
	// First Save is the same format/location flow as Save As. Untitled
	// documents are ODT by invariant.
	b(0xad, 0x6a, 0x03)
	beqSaveAsNormalOrigin := len(p)
	b(0xf0, 0)
	b(0xa9, 0x12, 0x8d, 0x14, 0x03, 0x9c, 0x6a, 0x03)
	braSaveAsOriginReady := len(p)
	b(0x80, 0)
	saveAsNormalOrigin := len(p)
	branch(beqSaveAsNormalOrigin, saveAsNormalOrigin)
	b(0x9c, 0x14, 0x03)
	saveAsOriginReady := len(p)
	branch(braSaveAsOriginReady, saveAsOriginReady)
	b(0x9c, 0x2f, 0x03)
	b(0xa9, 0x11, 0x8d, 0x1d, 0x03, 0xe2, 0x10)
	saveAsRequiredRenderCall := len(p)
	b(0x20, 0, 0, 0xc2, 0x10)
	saveAsRequiredCommitJump := len(p)
	b(0x4c, 0, 0)

	settingsEvent := len(p)
	jump(settingsEventJump, settingsEvent)
	b(0xad, 0x24, 0x03)
	bneSettingsEventInvalid := len(p)
	b(0xd0, 0)
	b(0xad, 0x23, 0x03, 0xc9, 0x05)
	bneSettingsEventInvalid2 := len(p)
	b(0xd0, 0)
	eventRead()
	sta(0x031b)
	eventRead()
	sta(0x031c)
	eventRead()
	sta(0x032b)
	eventRead()
	sta(0x032e)
	eventRead()
	sta(0x0369)
	b(0xad, 0x1d, 0x03, 0xc9, 0x10)
	bneSettingsEventCommit := len(p)
	b(0xd0, 0)
	b(0xe2, 0x10)
	settingsEventRenderCall := len(p)
	b(0x20, 0, 0, 0xc2, 0x10)
	settingsEventCommit := len(p)
	branch(bneSettingsEventCommit, settingsEventCommit)
	settingsEventCommitJump := len(p)
	b(0x4c, 0, 0)
	settingsEventInvalid := len(p)
	branch(bneSettingsEventInvalid, settingsEventInvalid)
	branch(bneSettingsEventInvalid2, settingsEventInvalid)
	settingsEventInvalidCommitJump := len(p)
	b(0x4c, 0, 0)

	transitionEvent := len(p)
	jump(transitionEventJump, transitionEvent)
	b(0x9c, 0x1a, 0x03)
	b(0xa9, 0x12, 0x8d, 0x1d, 0x03, 0xe2, 0x10)
	transitionEventRenderCall := len(p)
	b(0x20, 0, 0, 0xc2, 0x10)
	transitionEventCommitJump := len(p)
	b(0x4c, 0, 0)

	eventCommit := len(p)
	for _, at := range []int{
		eventCommitJump1, eventCommitJump2, eventCommitJump3, fileCommitJump1,
		fileEventDoneJump, invalidFileCommitJump, overwriteEventCommitJump,
		overwriteEventInvalidCommitJump, outcomeEventCommitJump,
		saveAsRequiredCommitJump, settingsEventCommitJump,
		settingsEventInvalidCommitJump, transitionEventCommitJump,
	} {
		jump(at, eventCommit)
	}
	b(0xc2, 0x30, 0xad, 0x6b, 0x03, 0x18, 0x69, 0x14, 0x00)
	b(0x6d, 0x23, 0x03, 0x29, 0xff, 0x1f, 0xaa, 0x8f, 0x08, 0x00, 0x70)
	b(0xe2, 0x30, 0x60)

	eventReadByte := len(p)
	for _, at := range eventReadCalls {
		patch16(at, eventReadByte)
	}
	b(0xbf, 0x00, 0x21, 0x70, 0x48, 0xe8, 0xe0, 0x00, 0x20)
	b(0x90, 0x03, 0xa2, 0x00, 0x00, 0x68, 0x60)

	// render_document builds a 30x17 low-byte tile plane in WRAM, then uses
	// two short DMA transfers during one real VBlank. CPU editing never races
	// the PPU and a full redraw cannot spill slow per-character VRAM writes into
	// active display.
	render := len(p)
	patch16(initialRenderCall, render)
	patch16(resolveRenderCall, render)
	for _, at := range renderCalls {
		patch16(at, render)
	}
	for _, at := range viewportRenderCalls {
		patch16(at, render)
	}
	for _, at := range []int{
		menuToggleRenderCall, settingsToggleRenderCall, helpToggleRenderCall,
		menuRenderCall1, menuRenderCall2, menuRenderCall3,
		menuRenderCallSaveFormat, menuRenderCallStats, menuRenderCall5,
		eventRenderCall, overwriteRenderCall, outcomeRenderCall,
		saveAsRequiredRenderCall, settingsEventRenderCall,
		transitionEventRenderCall,
	} {
		patch16(at, render)
	}
	for _, at := range browserRenderCalls {
		patch16(at, render)
	}
	b(0x8b, 0xa9, 0x7e, 0x48, 0xab) // PHB; LDA #0x7e; PHA; PLB
	// render is entered from both 8-bit and 16-bit X callers (viewport decode and
	// editor_dispatch both leave 16-bit X). The stage-clear, menu, and browser
	// copy loops all index with 8-bit X; renderDocument re-enables 16-bit X for
	// itself. Force a known 8-bit X/Y here so those LDX #imm loops stay aligned.
	b(0xe2, 0x30) // SEP #$30
	// The document field is 30 columns x 17 rows (510 cells), so its clear
	// cannot use the 8-bit X width required by the smaller presentation loops
	// below. Widen A and X/Y only for this loop and clear two cells per store;
	// 255 iterations preserve the old loop's throughput while covering the
	// complete plane. Restore 8-bit widths before any shared presentation loop.
	b(0xc2, 0x30)       // REP #$30
	b(0xa2, 0x00, 0x00) // LDX #0
	clearStage := len(p)
	b(0xa9, 0x20, 0x20, 0x9d, 0x00, 0x10) // LDA #$2020; STA $1000,X
	b(0xa9, documentBaseAttr, documentBaseAttr, 0x9d, 0x00, 0x12)
	b(0xe8, 0xe8)                                                       // INX; INX
	b(0xe0, byte(documentPlaneCells&0xff), byte(documentPlaneCells>>8)) // CPX #510
	bneClearStage := len(p)
	b(0xd0, 0)
	branch(bneClearStage, clearStage)
	b(0xe2, 0x30) // SEP #$30

	// Header presentation is data-driven by the same committed viewport as the
	// document. The ROM owns layout and decimal formatting; the host supplies
	// only resident title bytes plus chapter and word metadata.
	b(0xa2, 0x00)
	clearTitleStage := len(p)
	b(0xa9, 0x20, 0x9d, 0x00, 0x14, 0xa9, 0x08, 0x9d, 0x00, 0x15, 0xe8, 0xe0, 0x48)
	bneClearTitleStage := len(p)
	b(0xd0, 0)
	branch(bneClearTitleStage, clearTitleStage)
	b(0xa2, 0x00)
	clearStatusStage := len(p)
	b(0xa9, 0x20, 0x9d, 0x80, 0x14, 0x9e, 0x80, 0x15, 0xe8, 0xe0, 0x1e)
	bneClearStatusStage := len(p)
	b(0xd0, 0)
	branch(bneClearStatusStage, clearStatusStage)

	// Toolbar plane (2 rows x 11 cols at $1c00/$1c20): bold/italic/underline
	// buttons on row 0, alignment buttons on row 1. Fixed labels and a
	// uniform base attribute for now (mouse click routing and active-state
	// highlighting land separately); each byte is a plain absolute store
	// since the layout is compile-time constant.
	toolbarRow0 := " [B][I][U] "
	toolbarRow1 := " [L][C][R] "
	if len(toolbarRow0) != 11 || len(toolbarRow1) != 11 {
		panic("toolbar row labels must be exactly 11 characters")
	}
	for i, ch := range []byte(toolbarRow0 + toolbarRow1) {
		b(0xa9, ch, 0x8d, byte(0x1c00+i), byte((0x1c00+i)>>8))
		b(0xa9, documentBaseAttr, 0x8d, byte(0x1c20+i), byte((0x1c20+i)>>8))
	}

	b(0xaf, 0x24, 0x41, 0x70, 0xc9, 55)
	bccTitleCountReady := len(p)
	b(0x90, 0)
	b(0xa9, 54)
	titleCountReady := len(p)
	branch(bccTitleCountReady, titleCountReady)
	b(0x85, 0x1b, 0xa2, 0x00) // bounded resident title byte count
	copyTitle := len(p)
	b(0xe4, 0x1b)
	bcsTitleCopied := len(p)
	b(0xb0, 0)
	b(0xbf, 0x25, 0x41, 0x70, 0x9d, 0x00, 0x14, 0xe8)
	braCopyTitle := len(p)
	b(0x80, 0)
	branch(braCopyTitle, copyTitle)
	titleCopied := len(p)
	branch(bcsTitleCopied, titleCopied)

	// Host status flags (slot byte 91): bit 0 renders the modified marker and
	// bit 1 the read-only marker on the title card's otherwise empty fourth row.
	b(0xaf, 0x5b, 0x41, 0x70, 0x29, 0x01)
	beqTitleClean := len(p)
	b(0xf0, 0)
	b(0xa9, '*', 0x8d, 0x36, 0x14)
	titleClean := len(p)
	branch(beqTitleClean, titleClean)
	b(0xaf, 0x5b, 0x41, 0x70, 0x29, 0x02)
	beqTitleWritable := len(p)
	b(0xf0, 0)
	b(0xa9, 'R', 0x8d, 0x38, 0x14)
	titleWritable := len(p)
	branch(beqTitleWritable, titleWritable)

	for i, ch := range []byte("CHAPTER ") {
		ldaSta(ch, uint16(0x1480+i))
	}
	for i, ch := range []byte("WORDS ") {
		ldaSta(ch, uint16(0x1480+20+i))
	}
	for i := 0; i < 10; i++ {
		ldaSta(0x0c, uint16(0x1580+i)) // palette 3: yellow chapter label
	}

	// Clamp the chapter to two visible digits, then divide by ten through a
	// bounded subtraction loop (at most nine iterations).
	b(0xaf, 0x23, 0x41, 0x70)
	bneChapterClamp := len(p)
	b(0xd0, 0)
	b(0xaf, 0x22, 0x41, 0x70, 0xc9, 100)
	bccChapterRange := len(p)
	b(0x90, 0)
	chapterClamp := len(p)
	branch(bneChapterClamp, chapterClamp)
	b(0xa9, 99)
	chapterRange := len(p)
	branch(bccChapterRange, chapterRange)
	b(0xc9, 1)
	bcsChapterNonzero := len(p)
	b(0xb0, 0)
	b(0xa9, 1)
	chapterNonzero := len(p)
	branch(bcsChapterNonzero, chapterNonzero)
	b(0xa2, 0x00)
	chapterTens := len(p)
	b(0xc9, 10)
	bccChapterDigits := len(p)
	b(0x90, 0)
	b(0x38, 0xe9, 10, 0xe8)
	braChapterTens := len(p)
	b(0x80, 0)
	branch(braChapterTens, chapterTens)
	chapterDigits := len(p)
	branch(bccChapterDigits, chapterDigits)
	b(0x85, 0x1c, 0x8a, 0x18, 0x69, '0')
	sta(0x1488)
	b(0xa5, 0x1c, 0x18, 0x69, '0')
	sta(0x1489)

	// Clamp the 32-bit word count to 9999 and emit each decimal place from the
	// 16-bit remainder. This keeps numeric presentation cartridge-owned.
	b(0xc2, 0x20, 0xaf, 0x1e, 0x41, 0x70)
	bneWordClamp := len(p)
	b(0xd0, 0)
	b(0xaf, 0x1c, 0x41, 0x70, 0xc9, 0x10, 0x27)
	bccWordRange := len(p)
	b(0x90, 0)
	wordClamp := len(p)
	branch(bneWordClamp, wordClamp)
	b(0xa9, 0x0f, 0x27)
	wordRange := len(p)
	branch(bccWordRange, wordRange)
	b(0x85, 0x1c, 0xe2, 0x20)
	emitWordDigit := func(divisor uint16, address uint16) {
		b(0xa2, 0x00, 0xc2, 0x20)
		digitLoop := len(p)
		b(0xa5, 0x1c, 0xc9, byte(divisor), byte(divisor>>8))
		bccDigitDone := len(p)
		b(0x90, 0)
		b(0x38, 0xe9, byte(divisor), byte(divisor>>8), 0x85, 0x1c, 0xe8)
		braDigitLoop := len(p)
		b(0x80, 0)
		branch(braDigitLoop, digitLoop)
		digitDone := len(p)
		branch(bccDigitDone, digitDone)
		b(0xe2, 0x20, 0x8a, 0x18, 0x69, '0')
		sta(address)
	}
	emitWordDigit(1000, 0x149a)
	emitWordDigit(100, 0x149b)
	emitWordDigit(10, 0x149c)
	b(0xa5, 0x1c, 0x18, 0x69, '0')
	sta(0x149d)

	// Menu and browser screens reuse the same 30x8 tile plane as the document.
	// Their text comes from source-owned ROM data and their selected row is a
	// cartridge-owned palette decision.
	b(0xad, 0x1d, 0x03)
	beqRenderDocument := len(p)
	b(0xd0, 0x03, 0x4c, 0, 0)
	b(0xc9, 0x01)
	b(0xd0, 0x03)
	renderMainMenuJump := len(p)
	b(0x4c, 0, 0)
	// Help ($0f) is a whole plane, not a dialog overlay, so it is dispatched
	// here rather than in the browser-ready overlay chain -- which every mode
	// >= 5 otherwise falls into.
	b(0xc9, 0x0f)
	b(0xd0, 0x03)
	renderHelpJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x10)
	b(0xd0, 0x03)
	renderSettingsJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x11)
	b(0xd0, 0x03)
	renderSaveFormatJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x12)
	b(0xd0, 0x03)
	renderTransitionJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0a)
	b(0xd0, 0x03)
	renderFilenameJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x05)
	bccRenderBrowserLoading := len(p)
	b(0x90, 0x03)
	renderBrowserReadyJump := len(p)
	b(0x4c, 0, 0)

	renderBrowserLoading := len(p)
	branch(bccRenderBrowserLoading, renderBrowserLoading)
	b(0xa2, 0x00)
	copyBrowserPlane := len(p)
	b(0xbf, byte(loromAddr(browserReadyOffset)), byte(loromAddr(browserReadyOffset)>>8), loromBank(browserReadyOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0) // loading uses the neutral FILES plane
	bneCopyBrowserPlane := len(p)
	b(0xd0, 0)
	branch(bneCopyBrowserPlane, copyBrowserPlane)
	browserLoadingReadyJump := len(p)
	b(0x4c, 0, 0)

	renderBrowserReady := len(p)
	jump(renderBrowserReadyJump, renderBrowserReady)
	b(0xa2, 0x00)
	copyBrowserReadyPlane := len(p)
	b(0xbf, byte(loromAddr(browserReadyOffset)), byte(loromAddr(browserReadyOffset)>>8), loromBank(browserReadyOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0) // LDA long $00:plane,X
	bneCopyBrowserReadyPlane := len(p)
	b(0xd0, 0)
	branch(bneCopyBrowserReadyPlane, copyBrowserReadyPlane)
	// Save As must expose both steps that were previously implicit. At the
	// roots page Enter chooses a folder; inside a folder N creates the file.
	// The current parent at $1900/$0348 is also the one serialized on submit.
	b(0xad, 0x1d, 0x03, 0xc9, 0x06)
	bneSaveBrowserTitleDone := len(p)
	b(0xd0, 0)
	b(0xad, 0x48, 0x03)
	beqSaveRootTitle := len(p)
	b(0xf0, 0)
	b(0xa2, 0x00)
	copySaveFolderTitle := len(p)
	b(0xbf, byte(loromAddr(saveFolderTitleOffset)), byte(loromAddr(saveFolderTitleOffset)>>8), loromBank(saveFolderTitleOffset),
		0x9d, 0x00, 0x10, 0xe8, 0xe0, 30)
	bneCopySaveFolderTitle := len(p)
	b(0xd0, 0)
	branch(bneCopySaveFolderTitle, copySaveFolderTitle)
	braSaveBrowserTitleDone := len(p)
	b(0x80, 0)
	saveRootTitle := len(p)
	branch(beqSaveRootTitle, saveRootTitle)
	b(0xa2, 0x00)
	copySaveRootTitle := len(p)
	b(0xbf, byte(loromAddr(saveRootTitleOffset)), byte(loromAddr(saveRootTitleOffset)>>8), loromBank(saveRootTitleOffset),
		0x9d, 0x00, 0x10, 0xe8, 0xe0, 30)
	bneCopySaveRootTitle := len(p)
	b(0xd0, 0)
	branch(bneCopySaveRootTitle, copySaveRootTitle)
	saveBrowserTitleDone := len(p)
	branch(bneSaveBrowserTitleDone, saveBrowserTitleDone)
	branch(braSaveBrowserTitleDone, saveBrowserTitleDone)
	// Recovery rows use the bounded browser entry plane, but keep their own
	// cartridge-owned title.
	b(0xad, 0x4e, 0x03, 0xc9, 0x08)
	bneRecoveryHistoryTitle := len(p)
	b(0xd0, 0)
	for i, ch := range []byte("RECOVERY HISTORY") {
		ldaSta(ch, uint16(0x1007+i))
	}
	recoveryHistoryTitle := len(p)
	branch(bneRecoveryHistoryTitle, recoveryHistoryTitle)
	// Confirmation is an overlay on the cartridge browser plane, not a host
	// alert. The selected row remains visible behind it for context.
	b(0xad, 0x1d, 0x03, 0xc9, 0x09, 0xf0, 0x03)
	overwritePromptSkipJump := len(p)
	b(0x4c, 0, 0)
	for i, ch := range []byte("OVERWRITE FILE?") {
		ldaSta(ch, uint16(0x1096+i))
	}
	for i, ch := range []byte("ENTER YES") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	for i, ch := range []byte("BACK CANCEL") {
		ldaSta(ch, uint16(0x10d2+i))
	}
	overwritePromptDone := len(p)
	jump(overwritePromptSkipJump, overwritePromptDone)
	// Error/state outcomes never escape to a native alert. $0337 stores the
	// event low byte: 04 recovery available, 05 recovery restored, 07 save
	// conflict, 08 read-only, 09 open failed. Mode $0c is the transient
	// cartridge SAVING screen.
	b(0xad, 0x1d, 0x03, 0xc9, 0x0b, 0xd0, 0x03)
	outcomeIsDialogJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x0c, 0xf0, 0x03)
	outcomePromptSkipJump := len(p)
	b(0x4c, 0, 0)
	for i, ch := range []byte("SAVING DOCUMENT") {
		ldaSta(ch, uint16(0x1096+i))
	}
	for i, ch := range []byte("PLEASE WAIT") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	savingTextDoneJump := len(p)
	b(0x4c, 0, 0)
	outcomeDialogText := len(p)
	jump(outcomeIsDialogJump, outcomeDialogText)
	b(0xad, 0x37, 0x03, 0xc9, 0x04, 0xd0, 0x03)
	recoveryAvailableJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x05, 0xd0, 0x03)
	recoveryRestoredJump := len(p)
	b(0x4c, 0, 0)
	for i, ch := range []byte("OPERATION FAILED") {
		ldaSta(ch, uint16(0x1096+i))
	}
	b(0xad, 0x37, 0x03, 0xc9, 0x07)
	bneOutcomeReadOnly := len(p)
	b(0xd0, 0)
	for i, ch := range []byte("SAVE CONFLICT") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	outcomeTextDoneJump := len(p)
	b(0x4c, 0, 0)
	outcomeReadOnly := len(p)
	branch(bneOutcomeReadOnly, outcomeReadOnly)
	b(0xc9, 0x08)
	bneOutcomeOpenFailed := len(p)
	b(0xd0, 0)
	for i, ch := range []byte("READ ONLY FILE") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	outcomeReadOnlyDoneJump := len(p)
	b(0x4c, 0, 0)
	outcomeOpenFailed := len(p)
	branch(bneOutcomeOpenFailed, outcomeOpenFailed)
	b(0xc9, 0x11)
	bneOutcomeReallyOpenFailed := len(p)
	b(0xd0, 0)
	for i, ch := range []byte("SAVE FAILED") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	outcomeSaveFailedDoneJump := len(p)
	b(0x4c, 0, 0)
	outcomeReallyOpenFailed := len(p)
	branch(bneOutcomeReallyOpenFailed, outcomeReallyOpenFailed)
	for i, ch := range []byte("CANNOT OPEN FILE") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	outcomeTextDone := len(p)
	jump(outcomeTextDoneJump, outcomeTextDone)
	jump(outcomeReadOnlyDoneJump, outcomeTextDone)
	jump(outcomeSaveFailedDoneJump, outcomeTextDone)
	for i, ch := range []byte("ENTER OR BACK") {
		ldaSta(ch, uint16(0x10d2+i))
	}
	outcomeFailedDoneJump := len(p)
	b(0x4c, 0, 0)
	recoveryAvailableText := len(p)
	jump(recoveryAvailableJump, recoveryAvailableText)
	for i, ch := range []byte("RECOVERY AVAILABLE") {
		ldaSta(ch, uint16(0x1096+i))
	}
	for i, ch := range []byte("ENTER RESTORES") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	for i, ch := range []byte("BACK IGNORES") {
		ldaSta(ch, uint16(0x10d2+i))
	}
	recoveryAvailableDoneJump := len(p)
	b(0x4c, 0, 0)
	recoveryRestoredText := len(p)
	jump(recoveryRestoredJump, recoveryRestoredText)
	for i, ch := range []byte("RECOVERY COMPLETE") {
		ldaSta(ch, uint16(0x1096+i))
	}
	for i, ch := range []byte("DOCUMENT RESTORED") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	for i, ch := range []byte("ENTER OR BACK") {
		ldaSta(ch, uint16(0x10d2+i))
	}
	outcomePromptEnd := len(p)
	jump(outcomePromptSkipJump, outcomePromptEnd)
	jump(savingTextDoneJump, outcomePromptEnd)
	jump(outcomeFailedDoneJump, outcomePromptEnd)
	jump(recoveryAvailableDoneJump, outcomePromptEnd)
	// Statistics dialog (mode $0d): every number is authoritative committed
	// viewport metadata — words at slot 28, characters at 24, lines at 92.
	b(0xad, 0x1d, 0x03, 0xc9, 0x0d, 0xf0, 0x03)
	statsPromptSkipJump := len(p)
	b(0x4c, 0, 0)
	emitStatNumber := func(slotOff byte, addr uint16) {
		b(0xc2, 0x20, 0xaf, slotOff+2, 0x41, 0x70)
		bneStatClamp := len(p)
		b(0xd0, 0)
		b(0xaf, slotOff, 0x41, 0x70, 0xc9, 0x10, 0x27)
		bccStatRange := len(p)
		b(0x90, 0)
		statClamp := len(p)
		branch(bneStatClamp, statClamp)
		b(0xa9, 0x0f, 0x27)
		statRange := len(p)
		branch(bccStatRange, statRange)
		b(0x85, 0x1c, 0xe2, 0x20)
		emitWordDigit(1000, addr)
		emitWordDigit(100, addr+1)
		emitWordDigit(10, addr+2)
		b(0xa5, 0x1c, 0x18, 0x69, '0')
		sta(addr + 3)
	}
	for i, ch := range []byte("STATISTICS") {
		ldaSta(ch, uint16(0x1078+i))
	}
	for i, ch := range []byte("WORDS") {
		ldaSta(ch, uint16(0x1096+i))
	}
	emitStatNumber(0x1c, 0x109d)
	for i, ch := range []byte("CHARS") {
		ldaSta(ch, uint16(0x10b4+i))
	}
	emitStatNumber(0x18, 0x10bb)
	for i, ch := range []byte("LINES") {
		ldaSta(ch, uint16(0x10d2+i))
	}
	emitStatNumber(0x5c, 0x10d9)
	statsPromptEnd := len(p)
	jump(statsPromptSkipJump, statsPromptEnd)
	// Find entry (mode $0e): prompt plus the guest-owned query text.
	b(0xad, 0x1d, 0x03, 0xc9, 0x0e, 0xf0, 0x03)
	findPromptSkipJump := len(p)
	b(0x4c, 0, 0)
	for i, ch := range []byte("FIND TEXT:") {
		ldaSta(ch, uint16(0x1096+i))
	}
	b(0xa2, 0x00)
	copyFindPrompt := len(p)
	b(0xec, 0x32, 0x03)
	bcsFindPromptDone := len(p)
	b(0xb0, 0)
	b(0xbd, 0xc0, 0x18, 0x9d, 0xb4, 0x10, 0xe8)
	braCopyFindPrompt := len(p)
	b(0x80, 0)
	findPromptDone := len(p)
	branch(bcsFindPromptDone, findPromptDone)
	branch(braCopyFindPrompt, copyFindPrompt)
	findPromptEnd := len(p)
	jump(findPromptSkipJump, findPromptEnd)
	b(0x64, 0x19)
	renderBrowserEntry := len(p)
	b(0xa5, 0x19, 0xcd, 0x1f, 0x03)
	bcsBrowserEntriesDone := len(p)
	b(0xb0, 0)
	b(0xaa, 0xbd, 0xe0, 0x17, 0x29, 0x01)
	beqBrowserFilePrefix := len(p)
	b(0xf0, 0)
	b(0xa9, '/')
	braBrowserPrefixReady := len(p)
	b(0x80, 0)
	browserFilePrefix := len(p)
	branch(beqBrowserFilePrefix, browserFilePrefix)
	b(0xa9, ' ')
	browserPrefixReady := len(p)
	branch(braBrowserPrefixReady, browserPrefixReady)
	b(0x48, 0xa5, 0x19, 0x0a, 0x85, 0x17)
	for i := 0; i < 4; i++ {
		b(0x0a)
	}
	b(0x38, 0xe5, 0x17, 0xa8, 0x18, 0x69, 30, 0xaa, 0x68)
	b(0x9d, 0x01, 0x10, 0xe8, 0xe8)
	b(0xa5, 0x19, 0xa8, 0xb9, 0xf0, 0x17, 0x85, 0x1a)
	b(0xa5, 0x19, 0x0a, 0x85, 0x17)
	for i := 0; i < 4; i++ {
		b(0x0a)
	}
	b(0x38, 0xe5, 0x17, 0xa8)
	b(0xa5, 0x1a)
	beqBrowserNameDone := len(p)
	b(0xf0, 0)
	copyBrowserName := len(p)
	b(0xb9, 0x00, 0x17, 0xc9, 0x80)
	bccBrowserGlyphReady := len(p)
	b(0x90, 0)
	b(0xa9, '?')
	browserGlyphReady := len(p)
	branch(bccBrowserGlyphReady, browserGlyphReady)
	b(0x9d, 0x00, 0x10, 0xc8, 0xe8, 0xc6, 0x1a)
	bneCopyBrowserName := len(p)
	b(0xd0, 0)
	branch(bneCopyBrowserName, copyBrowserName)
	browserNameDone := len(p)
	branch(beqBrowserNameDone, browserNameDone)
	b(0xe6, 0x19)
	braRenderBrowserEntry := len(p)
	b(0x80, 0)
	branch(braRenderBrowserEntry, renderBrowserEntry)
	browserEntriesDone := len(p)
	branch(bcsBrowserEntriesDone, browserEntriesDone)
	b(0xad, 0x1f, 0x03)
	beqBrowserPlaneReady := len(p)
	b(0xf0, 0)
	b(0xac, 0x20, 0x03, 0xc8, 0xa2, 0x00)
	browserSelectedRowOffset := len(p)
	b(0x8a, 0x18, 0x69, 30, 0xaa, 0x88)
	bneBrowserSelectedRowOffset := len(p)
	b(0xd0, 0)
	branch(bneBrowserSelectedRowOffset, browserSelectedRowOffset)
	b(0xa9, '>', 0x9d, 0x00, 0x10, 0xa9, 0x04, 0xa0, 30)
	highlightBrowserRow := len(p)
	b(0x9d, 0x00, 0x12, 0xe8, 0x88)
	bneHighlightBrowserRow := len(p)
	b(0xd0, 0)
	branch(bneHighlightBrowserRow, highlightBrowserRow)
	browserPlaneReady := len(p)
	branch(beqBrowserPlaneReady, browserPlaneReady)
	browserReadyJump := len(p)
	b(0x4c, 0, 0)

	// Sits between two unconditional JMPs, so nothing falls into or out of it.
	// Same 8-bit-X 240-byte copy as the menu and browser planes (render entry
	// forces SEP #$30), but with no selected row to highlight, so the cleared
	// attribute plane is left as-is.
	renderHelp := len(p)
	jump(renderHelpJump, renderHelp)
	b(0xa2, 0x00)
	copyHelpPlane := len(p)
	b(0xbf, byte(loromAddr(helpOffset)), byte(loromAddr(helpOffset)>>8), loromBank(helpOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0) // LDA long $00:plane,X (render DB is $7e)
	bneCopyHelpPlane := len(p)
	b(0xd0, 0)
	branch(bneCopyHelpPlane, copyHelpPlane)
	helpPlaneReadyJump := len(p)
	b(0x4c, 0, 0)

	renderSettings := len(p)
	jump(renderSettingsJump, renderSettings)
	b(0xa2, 0x00)
	copySettingsPlane := len(p)
	b(0xbf, byte(loromAddr(settingsOffset)), byte(loromAddr(settingsOffset)>>8), loromBank(settingsOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0)
	bneCopySettingsPlane := len(p)
	b(0xd0, 0)
	branch(bneCopySettingsPlane, copySettingsPlane)
	// Clear and then write the variable mode field.
	for i := 0; i < 17; i++ {
		ldaSta(' ', uint16(0x1025+i))
	}
	b(0xad, 0x1b, 0x03)
	beqSettingsRecoveryOnly := len(p)
	b(0xf0, 0)
	for i, ch := range []byte("SAVE + RECOVERY") {
		ldaSta(ch, uint16(0x1025+i))
	}
	settingsModeDoneJump := len(p)
	b(0x4c, 0, 0)
	settingsRecoveryOnly := len(p)
	branch(beqSettingsRecoveryOnly, settingsRecoveryOnly)
	for i, ch := range []byte("RECOVERY ONLY") {
		ldaSta(ch, uint16(0x1025+i))
	}
	settingsModeDone := len(p)
	jump(settingsModeDoneJump, settingsModeDone)
	emitByteDigits := func(source uint16, address uint16) {
		b(0xad, byte(source), byte(source>>8), 0xa2, 0x00)
		hundreds := len(p)
		b(0xc9, 100)
		bccHundredsDone := len(p)
		b(0x90, 0)
		b(0x38, 0xe9, 100, 0xe8)
		braHundreds := len(p)
		b(0x80, 0)
		branch(braHundreds, hundreds)
		hundredsDone := len(p)
		branch(bccHundredsDone, hundredsDone)
		b(0x85, 0x1c, 0x8a, 0x18, 0x69, '0')
		sta(address)
		b(0xa5, 0x1c, 0xa2, 0x00)
		tens := len(p)
		b(0xc9, 10)
		bccTensDone := len(p)
		b(0x90, 0)
		b(0x38, 0xe9, 10, 0xe8)
		braTens := len(p)
		b(0x80, 0)
		branch(braTens, tens)
		tensDone := len(p)
		branch(bccTensDone, tensDone)
		b(0x85, 0x1c, 0x8a, 0x18, 0x69, '0')
		sta(address + 1)
		b(0xa5, 0x1c, 0x18, 0x69, '0')
		sta(address + 2)
	}
	emitByteDigits(0x031c, 0x104c)
	emitByteDigits(0x032b, 0x106a)
	for i := 0; i < 14; i++ {
		ldaSta(' ', uint16(0x10a0+i))
	}
	b(0xad, 0x69, 0x03, 0xc9, 0x04)
	bneSettingsNotMarkdownText := len(p)
	b(0xd0, 0)
	b(0xad, 0x2e, 0x03)
	beqSettingsRenderedText := len(p)
	b(0xf0, 0)
	for i, ch := range []byte("SOURCE") {
		ldaSta(ch, uint16(0x10a0+i))
	}
	settingsMarkdownTextDoneJump1 := len(p)
	b(0x4c, 0, 0)
	settingsRenderedText := len(p)
	branch(beqSettingsRenderedText, settingsRenderedText)
	for i, ch := range []byte("RENDERED") {
		ldaSta(ch, uint16(0x10a0+i))
	}
	settingsMarkdownTextDoneJump2 := len(p)
	b(0x4c, 0, 0)
	settingsNotMarkdownText := len(p)
	branch(bneSettingsNotMarkdownText, settingsNotMarkdownText)
	for i, ch := range []byte("NOT MARKDOWN") {
		ldaSta(ch, uint16(0x10a0+i))
	}
	settingsMarkdownTextDone := len(p)
	jump(settingsMarkdownTextDoneJump1, settingsMarkdownTextDone)
	jump(settingsMarkdownTextDoneJump2, settingsMarkdownTextDone)
	b(0xac, 0x1a, 0x03, 0xc8, 0xa2, 0x00)
	settingsRowOffset := len(p)
	b(0x8a, 0x18, 0x69, 30, 0xaa, 0x88)
	bneSettingsRowOffset := len(p)
	b(0xd0, 0)
	branch(bneSettingsRowOffset, settingsRowOffset)
	b(0xa9, '>', 0x9d, 0x00, 0x10, 0xa9, 0x04, 0xa0, 30)
	highlightSettingsRow := len(p)
	b(0x9d, 0x00, 0x12, 0xe8, 0x88)
	bneHighlightSettingsRow := len(p)
	b(0xd0, 0)
	branch(bneHighlightSettingsRow, highlightSettingsRow)
	settingsPlaneReadyJump := len(p)
	b(0x4c, 0, 0)

	renderSaveFormat := len(p)
	jump(renderSaveFormatJump, renderSaveFormat)
	b(0xa2, 0x00)
	copySaveFormatPlane := len(p)
	b(0xbf, byte(loromAddr(saveFormatOffset)), byte(loromAddr(saveFormatOffset)>>8), loromBank(saveFormatOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0)
	bneCopySaveFormatPlane := len(p)
	b(0xd0, 0)
	branch(bneCopySaveFormatPlane, copySaveFormatPlane)
	b(0xac, 0x2f, 0x03, 0xc8, 0xa2, 0x00)
	saveFormatRowOffset := len(p)
	b(0x8a, 0x18, 0x69, 30, 0xaa, 0x88)
	bneSaveFormatRowOffset := len(p)
	b(0xd0, 0)
	branch(bneSaveFormatRowOffset, saveFormatRowOffset)
	b(0xa9, '>', 0x9d, 0x00, 0x10, 0xa9, 0x04, 0xa0, 30)
	highlightSaveFormatRow := len(p)
	b(0x9d, 0x00, 0x12, 0xe8, 0x88)
	bneHighlightSaveFormatRow := len(p)
	b(0xd0, 0)
	branch(bneHighlightSaveFormatRow, highlightSaveFormatRow)
	saveFormatPlaneReadyJump := len(p)
	b(0x4c, 0, 0)

	renderFilename := len(p)
	jump(renderFilenameJump, renderFilename)
	b(0xa2, 0x00)
	copyFilenamePlane := len(p)
	b(0xbf, byte(loromAddr(filenameOffset)), byte(loromAddr(filenameOffset)>>8), loromBank(filenameOffset),
		0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0)
	bneCopyFilenamePlane := len(p)
	b(0xd0, 0)
	branch(bneCopyFilenamePlane, copyFilenamePlane)

	// Filename mode temporarily owns the complete header. The normal document
	// title and formatting glyphs would otherwise make the modal look like an
	// overlay on a still-active editor -- exactly the broken state this screen
	// replaces. The right card becomes two stacked, mouse-sized actions.
	b(0xa2, 0x00)
	clearFilenameTitle := len(p)
	b(0xa9, ' ', 0x9d, 0x00, 0x14, 0xa9, documentBaseAttr, 0x9d, 0x00, 0x15, 0xe8, 0xe0, 72)
	bneClearFilenameTitle := len(p)
	b(0xd0, 0)
	branch(bneClearFilenameTitle, clearFilenameTitle)
	for row, line := range []string{"SAVE NEW FILE", "TYPE A FILE NAME", "EXTENSION AUTO"} {
		for col, ch := range []byte(line) {
			ldaSta(ch, uint16(0x1400+row*18+col))
		}
	}
	for i, ch := range []byte("TAB/ARROWS MOVE  ENTER SELECTS") {
		ldaSta(ch, uint16(0x1480+i))
		ldaSta(documentBaseAttr, uint16(0x1580+i))
	}
	filenameToolbar := " [  SAVE  ]" + " [ CANCEL ]"
	if len(filenameToolbar) != 22 {
		panic("filename toolbar labels must be two 11-character rows")
	}
	for i, ch := range []byte(filenameToolbar) {
		ldaSta(ch, uint16(0x1c00+i))
		ldaSta(documentBaseAttr, uint16(0x1c20+i))
	}

	// The 24-byte buffer and its visible field are the same width. Keep an
	// underscore insertion marker only while the field owns focus and still
	// has room; no typed byte is ever hidden or wrapped into stale UI.
	b(0xa2, 0x00)
	copyFilenameBytesToField := len(p)
	b(0xec, 0x31, 0x03)
	bcsFilenameBytesInField := len(p)
	b(0xb0, 0)
	b(0xbd, 0x80, 0x18, 0x9d, 0x7b, 0x10, 0xe8)
	braCopyFilenameBytesToField := len(p)
	b(0x80, 0)
	filenameBytesInField := len(p)
	branch(bcsFilenameBytesInField, filenameBytesInField)
	branch(braCopyFilenameBytesToField, copyFilenameBytesToField)
	b(0xad, 0x6d, 0x03)
	bneFilenameFocusNotField := len(p)
	b(0xd0, 0)
	b(0xe0, 24)
	bcsFilenameFieldFull := len(p)
	b(0xb0, 0)
	b(0xa9, '_', 0x9d, 0x7b, 0x10)
	filenameFieldFull := len(p)
	branch(bcsFilenameFieldFull, filenameFieldFull)
	b(0xa2, 0x00)
	highlightFilenameField := len(p)
	b(0xa9, 0x04,
		0x9d, 0x5c, 0x12,
		0x9d, 0x7a, 0x12,
		0x9d, 0x98, 0x12,
		0xe8, 0xe0, 26)
	bneHighlightFilenameField := len(p)
	b(0xd0, 0)
	branch(bneHighlightFilenameField, highlightFilenameField)
	filenameFieldReadyJump := len(p)
	b(0x4c, 0, 0)
	filenameFocusNotField := len(p)
	branch(bneFilenameFocusNotField, filenameFocusNotField)
	b(0xc9, 0x01)
	bneFilenameFocusCancel := len(p)
	b(0xd0, 0)
	b(0xa2, 0x00)
	highlightFilenameSave := len(p)
	b(0xa9, 0x04, 0x9d, 0x20, 0x1c, 0xe8, 0xe0, 11)
	bneHighlightFilenameSave := len(p)
	b(0xd0, 0)
	branch(bneHighlightFilenameSave, highlightFilenameSave)
	filenameSaveReadyJump := len(p)
	b(0x4c, 0, 0)
	filenameFocusCancel := len(p)
	branch(bneFilenameFocusCancel, filenameFocusCancel)
	b(0xa2, 0x00)
	highlightFilenameCancel := len(p)
	b(0xa9, 0x04, 0x9d, 0x2b, 0x1c, 0xe8, 0xe0, 11)
	bneHighlightFilenameCancel := len(p)
	b(0xd0, 0)
	branch(bneHighlightFilenameCancel, highlightFilenameCancel)
	filenamePlaneReady := len(p)
	jump(filenameFieldReadyJump, filenamePlaneReady)
	jump(filenameSaveReadyJump, filenamePlaneReady)
	filenamePlaneReadyJump := len(p)
	b(0x4c, 0, 0)

	renderTransition := len(p)
	jump(renderTransitionJump, renderTransition)
	b(0xa2, 0x00)
	copyTransitionPlane := len(p)
	b(0xbf, byte(loromAddr(browserOffset)), byte(loromAddr(browserOffset)>>8), loromBank(browserOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0)
	bneCopyTransitionPlane := len(p)
	b(0xd0, 0)
	branch(bneCopyTransitionPlane, copyTransitionPlane)
	b(0xac, 0x1a, 0x03, 0xc8, 0xa2, 0x00)
	transitionRowOffset := len(p)
	b(0x8a, 0x18, 0x69, 30, 0xaa, 0x88)
	bneTransitionRowOffset := len(p)
	b(0xd0, 0)
	branch(bneTransitionRowOffset, transitionRowOffset)
	b(0xa9, '>', 0x9d, 0x00, 0x10, 0xa9, 0x04, 0xa0, 30)
	highlightTransitionRow := len(p)
	b(0x9d, 0x00, 0x12, 0xe8, 0x88)
	bneHighlightTransitionRow := len(p)
	b(0xd0, 0)
	branch(bneHighlightTransitionRow, highlightTransitionRow)
	transitionPlaneReadyJump := len(p)
	b(0x4c, 0, 0)

	renderMainMenu := len(p)
	jump(renderMainMenuJump, renderMainMenu)
	b(0xa2, 0x00)
	copyMenuPlane := len(p)
	b(0xbf, byte(loromAddr(menuOffset)), byte(loromAddr(menuOffset)>>8), loromBank(menuOffset), 0x9d, 0x00, 0x10, 0xe8, 0xe0, 0xf0) // LDA long $00:plane,X (render DB is $7e)
	bneCopyMenuPlane := len(p)
	b(0xd0, 0)
	branch(bneCopyMenuPlane, copyMenuPlane)
	b(0xac, 0x1e, 0x03, 0xc8, 0xa2, 0x00)
	menuRowOffset := len(p)
	b(0x8a, 0x18, 0x69, 30, 0xaa, 0x88)
	bneMenuRowOffset := len(p)
	b(0xd0, 0)
	branch(bneMenuRowOffset, menuRowOffset)
	b(0xa9, '>', 0x9d, 0x00, 0x10, 0xa9, 0x04, 0xa0, 30)
	highlightMenuRow := len(p)
	b(0x9d, 0x00, 0x12, 0xe8, 0x88)
	bneHighlightMenuRow := len(p)
	b(0xd0, 0)
	branch(bneHighlightMenuRow, highlightMenuRow)
	menuPlaneReady := len(p)
	for _, at := range []int{
		browserLoadingReadyJump, browserReadyJump, helpPlaneReadyJump,
		settingsPlaneReadyJump, saveFormatPlaneReadyJump,
		filenamePlaneReadyJump, transitionPlaneReadyJump,
	} {
		jump(at, menuPlaneReady)
	}
	menuUploadJump := len(p)
	b(0x4c, 0, 0)

	// finishRow applies paragraph alignment to the plane row the draw loop has
	// just completed. Alignment is deliberately not a property of the drawing
	// pass: a line's width is only known once the line ends, and the wrap
	// decisions that produced that width must not depend on where the line is
	// then placed. So each row is laid out flush left and translated right
	// afterwards, which leaves the loop's own wrap arithmetic, cursor capture and
	// hit-test walking one unshifted coordinate space.
	//
	// Input:  $0a = the finished row's width in cells, Y = one cell past its last
	//         drawn cell, X = the next character to draw, $0f14 = the row's
	//         alignment (0 left, 1 centre, 2 right, 3 justify -- rendered left).
	// Output: the row's cells are moved right in $1000/$1200, the caret cell
	//         $0b follows them, $0f00+row records the shift for the pointer, and
	//         $0f14 is reloaded for the row that starts at X.
	//
	// A, X and Y are preserved: every call site is mid-flow in the draw loop.
	// Index registers are 16-bit here, as renderDocument leaves them.
	finishRow := len(p)
	b(0x48, 0xda, 0x5a) // PHA; PHX; PHY
	b(0xa5, 0x0a)       // LDA $0a (the row's drawn width)
	b(0xd0, 0x03)       // BNE over the trampoline: an empty row is a long branch away
	finishRowEmptyJump := len(p)
	b(0x4c, 0, 0) // JMP finishRowEmpty (no row was consumed)
	// Keep the row's geometry where the 16-bit arithmetic below can read it as a
	// word. A direct 16-bit read of $0a would take the caret's low byte at $0b
	// with it, which is the exact shape of three earlier defects in this routine's
	// neighbourhood.
	b(0x8d, 0x18, 0x0f, 0x9c, 0x19, 0x0f)       // STA $0f18; STZ $0f19 (width)
	b(0x1a, 0x8d, 0x1c, 0x0f, 0x9c, 0x1d, 0x0f) // INC A; STA $0f1c; STZ $0f1d (width+1)
	b(0xc2, 0x20)                               // REP #$20 (16-bit A)
	b(0x98, 0x38, 0xed, 0x18, 0x0f)             // TYA; SEC; SBC $0f18 (rowStart = Y - width)
	b(0x8d, 0x1a, 0x0f)                         // STA $0f1a
	b(0x98, 0x38, 0xe9, 0x01, 0x00)             // TYA; SEC; SBC #1 (its last drawn cell)
	b(0xaa)                                     // TAX
	b(0xe2, 0x20)                               // SEP #$20 (8-bit A)
	// A wrapped line ends on the space it broke at, and that space is not part of
	// the visible line: counting it would leave right-aligned text a cell short of
	// the margin and put every wrapped centred line half a cell off.
	finishRowTrim := len(p)
	b(0xad, 0x18, 0x0f) // LDA $0f18 (cells still counted)
	beqFinishRowTrimmed := len(p)
	b(0xf0, 0)
	b(0xbd, 0x00, 0x10, 0xc9, ' ') // LDA $1000,X; CMP #' '
	bneFinishRowTrimmed := len(p)
	b(0xd0, 0)
	b(0xca, 0xce, 0x18, 0x0f) // DEX; DEC $0f18
	braFinishRowTrim := len(p)
	b(0x80, 0)
	finishRowTrimmed := len(p)
	branch(beqFinishRowTrimmed, finishRowTrimmed)
	branch(bneFinishRowTrimmed, finishRowTrimmed)
	branch(braFinishRowTrim, finishRowTrim)
	// X is now the row's last visible cell and $0f18 its visible width.
	// shift = alignment 1 -> (30 - width)/2, 2 -> 30 - width, otherwise 0. A row
	// of nothing but spaces has no visible width and is never moved.
	b(0xad, 0x18, 0x0f)
	beqFinishRowNoShift := len(p)
	b(0xf0, 0)
	b(0xad, 0x14, 0x0f, 0xc9, 0x01) // LDA $0f14; CMP #1
	beqFinishRowCentre := len(p)
	b(0xf0, 0)
	b(0xc9, 0x02) // CMP #2
	beqFinishRowRight := len(p)
	b(0xf0, 0)
	finishRowNoShift := len(p)
	branch(beqFinishRowNoShift, finishRowNoShift)
	b(0xa9, 0x00) // left, or justify rendered as left
	braFinishRowHaveShift := len(p)
	b(0x80, 0)
	finishRowCentre := len(p)
	branch(beqFinishRowCentre, finishRowCentre)
	b(0xa9, 0x1e, 0x38, 0xed, 0x18, 0x0f, 0x4a) // LDA #30; SEC; SBC $0f18; LSR A
	braFinishRowHaveShift2 := len(p)
	b(0x80, 0)
	finishRowRight := len(p)
	branch(beqFinishRowRight, finishRowRight)
	b(0xa9, 0x1e, 0x38, 0xed, 0x18, 0x0f) // LDA #30; SEC; SBC $0f18
	finishRowHaveShift := len(p)
	branch(braFinishRowHaveShift, finishRowHaveShift)
	branch(braFinishRowHaveShift2, finishRowHaveShift)
	// Record the shift for this row, then advance the row counter. The pointer and
	// wrap-aware Up/Down read this table to bring a visible cell back into layout
	// space, so every row the loop completes has to appear in it -- zero shifts
	// included.
	b(0x8d, 0x16, 0x0f, 0x9c, 0x17, 0x0f) // STA $0f16; STZ $0f17 (shift)
	// X still points at the row's last visible cell and the move loop below needs
	// it, but indexing the row table takes X away.
	b(0xc2, 0x20, 0x8a, 0x8d, 0x20, 0x0f, 0xe2, 0x20) // TXA; STA $0f20 (16-bit)
	b(0xae, 0x12, 0x0f)                               // LDX $0f12 (row index; its high byte stays 0)
	b(0xad, 0x16, 0x0f)                               // LDA $0f16
	b(0x9d, 0x00, 0x0f)                               // STA $0f00,X
	b(0xe8, 0x8e, 0x12, 0x0f)                         // INX; STX $0f12
	b(0xae, 0x20, 0x0f)                               // LDX $0f20 (the last visible cell again)
	b(0xad, 0x16, 0x0f)                               // LDA $0f16
	b(0xd0, 0x03)                         // BNE over the trampoline
	finishRowDoneJump := len(p)
	b(0x4c, 0, 0) // JMP finishRowDone (already flush left)
	// Move the row's visible cells right, last cell first so the copy can never
	// overwrite a cell it has not read yet. X is still that last cell.
	b(0xc2, 0x20)                   // REP #$20 (16-bit A)
	b(0x8a, 0x18, 0x6d, 0x16, 0x0f) // TXA; CLC; ADC $0f16 (+ shift)
	b(0xa8)                         // TAY (destination)
	b(0xe2, 0x20)                   // SEP #$20 (8-bit A)
	finishRowMove := len(p)
	b(0xbd, 0x00, 0x10, 0x99, 0x00, 0x10) // LDA $1000,X; STA $1000,Y
	b(0xbd, 0x00, 0x12, 0x99, 0x00, 0x12) // LDA $1200,X; STA $1200,Y
	b(0xca, 0x88, 0xce, 0x18, 0x0f)       // DEX; DEY; DEC $0f18
	bneFinishRowMove := len(p)
	b(0xd0, 0)
	branch(bneFinishRowMove, finishRowMove)
	// Blank the cells the row vacated, so the text it used to hold there cannot
	// stay behind the moved line.
	b(0xae, 0x1a, 0x0f)                         // LDX $0f1a (rowStart)
	b(0xad, 0x16, 0x0f, 0x8d, 0x18, 0x0f)       // LDA $0f16; STA $0f18 (count = shift)
	finishRowBlank := len(p)
	b(0xa9, ' ', 0x9d, 0x00, 0x10)              // LDA #' '; STA $1000,X
	b(0xa9, documentBaseAttr, 0x9d, 0x00, 0x12) // LDA #base; STA $1200,X
	b(0xe8, 0xce, 0x18, 0x0f)                   // INX; DEC $0f18
	bneFinishRowBlank := len(p)
	b(0xd0, 0)
	branch(bneFinishRowBlank, finishRowBlank)
	// The caret cell belongs to this row like any other cell, including the cell
	// one past its last character, where the caret sits at a line end. Leaving it
	// behind would draw the block cursor where the text used to be.
	b(0xc2, 0x20)       // REP #$20 (16-bit A)
	b(0xa5, 0x0b)       // LDA $0b (caret cell)
	b(0xcd, 0x1a, 0x0f) // CMP $0f1a (rowStart)
	bccFinishRowCaretDone := len(p)
	b(0x90, 0)                      // BCC: the caret is in an earlier row
	b(0x38, 0xed, 0x1a, 0x0f)       // SEC; SBC $0f1a (its column in this row)
	b(0xcd, 0x1c, 0x0f)             // CMP $0f1c (width + 1)
	bcsFinishRowCaretDone := len(p)
	b(0xb0, 0)                                        // BCS: the caret is in a later row
	b(0xa5, 0x0b, 0x18, 0x6d, 0x16, 0x0f, 0x85, 0x0b) // LDA $0b; CLC; ADC $0f16; STA $0b
	finishRowCaretDone := len(p)
	branch(bccFinishRowCaretDone, finishRowCaretDone)
	branch(bcsFinishRowCaretDone, finishRowCaretDone)
	b(0xe2, 0x20) // SEP #$20 (8-bit A)
	finishRowDone := len(p)
	jump(finishRowDoneJump, finishRowDone)
	finishRowEmpty := len(p)
	jump(finishRowEmptyJump, finishRowEmpty)
	// The row that starts here inherits the alignment of its first character.
	b(0x7a, 0xfa)       // PLY; PLX (X is the next character to draw)
	b(0xbd, 0x00, 0x0d) // LDA $0d00,X
	b(0x8d, 0x14, 0x0f) // STA $0f14
	b(0x68)             // PLA (the caller's accumulator)
	b(0x60)             // RTS

	renderDocument := len(p)
	jump(beqRenderDocument+2, renderDocument)
	b(0xc2, 0x10)
	// $56 is a "true caret cell already found" flag for this render pass (see
	// `positionCursor`'s per-character CPX $00 match below). Both places that
	// terminate the loop -- reaching the natural end of decoded text here,
	// and the screenFull overflow path further down -- used to unconditionally
	// overwrite $0b with the current/sentinel screen position, clobbering a
	// correct earlier capture whenever the cursor sits before the very last
	// rendered character. Both are now guarded by this flag.
	// $0b/$0c is the 16-bit caret cell (index registers are 16-bit here, so the
	// captures below use STY/LDY across both bytes); $5a is the word-length
	// counter, deliberately kept off $0c so measuring a word after the caret is
	// captured cannot corrupt the caret's high byte.
	b(0xa2, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x64, 0x0a, 0x64, 0x0b, 0x64, 0x0c, 0x64, 0x56, 0x64, 0x5a)
	// Alignment state for this pass: an all-zero shift table (rows the loop never
	// reaches must still read as unshifted for the pointer), row counter 0, and
	// the first row's alignment taken from the first character.
	b(0xa2, 0x11, 0x00) // LDX #17
	clearRowShifts := len(p)
	b(0xca, 0x9e, 0x00, 0x0f) // DEX; STZ $0f00,X
	bneClearRowShifts := len(p)
	b(0xd0, 0)
	branch(bneClearRowShifts, clearRowShifts)
	b(0x9c, 0x12, 0x0f, 0x9c, 0x13, 0x0f) // STZ $0f12; STZ $0f13 (row 0)
	b(0xad, 0x00, 0x0d, 0x8d, 0x14, 0x0f) // LDA $0d00; STA $0f14
	b(0xa2, 0x00, 0x00)                   // LDX #0 (the clear loop above used X)
	drawLoop := len(p)
	b(0xc0, 0xfe, 0x01)
	b(0x90, 0x03) // BCC visible; otherwise jump beyond the long layout loop
	screenFullJump := len(p)
	b(0x4c, 0, 0)
	b(0xe4, 0x08)
	bneTextRemains := len(p)
	b(0xd0, 0)    // BNE textRemains; end-of-document records/draws cursor
	b(0xa5, 0x56) // LDA $56 (true caret already found?)
	bneSkipNaturalEndCapture := len(p)
	b(0xd0, 0)
	b(0x84, 0x0b) // STY $0b -- only when the true caret was never found this pass
	skipNaturalEndCapture := len(p)
	branch(bneSkipNaturalEndCapture, skipNaturalEndCapture)
	// The document's last line is a finished row too, and it is the one most
	// likely to be short -- so it is the one alignment moves furthest.
	jsrTo(finishRow)
	drawCursorJump := len(p)
	b(0x4c, 0, 0)
	textRemains := len(p)
	branch(bneTextRemains, textRemains)
	b(0xbd, 0x00, 0x05, 0xc9, 0x0d, 0xd0, 0x03)
	newlineJump := len(p)
	b(0x4c, 0, 0)
	b(0xc9, 0x20)
	beqPositionCursor := len(p)
	b(0xf0, 0)
	// A word is measured only at its first character. If it cannot fit on the
	// current line, move the entire word to the next line before recording the
	// cursor position or emitting tiles. Words longer than 30 cells still split
	// deterministically because no legal unbroken placement exists.
	b(0x8a)
	beqMeasureWord := len(p)
	b(0xf0, 0)
	b(0xca, 0xbd, 0x00, 0x05, 0xe8, 0xc9, 0x20)
	beqMeasureWord2 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x0d)
	bnePositionCursor := len(p)
	b(0xd0, 0)
	measureWord := len(p)
	branch(beqMeasureWord, measureWord)
	branch(beqMeasureWord2, measureWord)
	// $58 (word-start X, saved/restored across this per-word measurement) --
	// not $09: $09 doubles as the high byte of the 16-bit decoded document
	// length ($08/$09, persisted by the viewport decode routine), which a
	// word-start save here would silently truncate to its low byte on every
	// redraw pass once a document exceeds 255 characters. Verified via a
	// standalone harness sweeping document lengths 10..300: $09 was
	// unconditionally zeroed by this STX whenever the first measured word
	// starts at column 0, exactly matching the observed length%256 pattern.
	// The word-length counter lives at $5a, not $0c. renderDocument runs with
	// 16-bit index registers, so the caret capture's `STY $0b` writes a full
	// 16-bit cell across $0b AND $0c -- and `drawCursor` reads it back the same
	// way with `LDY $0b`. Counting word length in $0c therefore overwrote the
	// caret cell's high byte on every word measured after the caret was
	// captured, sending the caret glyph to $1000 + (wordLen<<8) + column. That
	// is outside the document plane entirely, so a cursor anywhere except the
	// natural end of text drew no visible caret at all (confirmed: cursor 8 in a
	// 61-character document put the glyph at $1608 instead of $1008, with
	// $0b=$08 correct and $0c=$06 left over from measuring "juliet"). This is
	// the third zero-page collision of exactly this shape in this file, after
	// $19/$1b and $09.
	b(0x86, 0x58, 0x64, 0x5a)
	measureLoop := len(p)
	b(0xe4, 0x08)
	beqMeasureDone := len(p)
	b(0xf0, 0)
	b(0xbd, 0x00, 0x05, 0xc9, 0x20)
	beqMeasureDone2 := len(p)
	b(0xf0, 0)
	b(0xc9, 0x0d)
	beqMeasureDone3 := len(p)
	b(0xf0, 0)
	b(0xe8, 0xe6, 0x5a) // INX ; INC $5a
	// $5a is a single (8-bit) zero-page byte, incremented once per character in
	// this per-character render-time loop -- the hottest path in the whole
	// draw routine, run in full on every frame while any text exists, so any
	// extra per-iteration cost here is expensive (confirmed empirically: it
	// measurably slows convergence of in-flight typing in the xband
	// end-to-end test). A word of 256+ non-space/non-CR characters would
	// otherwise silently wrap $5a back toward 0, corrupting the later "does
	// this word fit" comparison against the 30-column width. Catch the wrap
	// cheaply, after the fact, only on the rare frame it actually happens:
	// INC sets the zero flag when it wraps 255->0, so the common (no-wrap)
	// case costs only one extra BNE beyond the original INX/INC.
	bneNoWrap := len(p)
	b(0xd0, 0)
	b(0xa9, 31, 0x85, 0x5a) // LDA #31; STA $5a -- any count >=31 already means "doesn't fit"
	noWrap := len(p)
	branch(bneNoWrap, noWrap)
	braMeasureLoop := len(p)
	b(0x80, 0)
	branch(braMeasureLoop, measureLoop)
	measureDone := len(p)
	branch(beqMeasureDone, measureDone)
	branch(beqMeasureDone2, measureDone)
	branch(beqMeasureDone3, measureDone)
	b(0xa6, 0x58, 0xa5, 0x0a)
	beqPositionCursor2 := len(p)
	b(0xf0, 0)
	// The fit test below sums the column and the word length in an 8-bit
	// accumulator. $5a legally reaches 255 -- it saturates to 31 only on the
	// frame its INC actually wraps 255->0 -- so column + $5a can exceed 255 and
	// wrap the accumulator itself, making an unbroken word of roughly 227-255
	// characters falsely report "fits" at a high column. Reject any word of 31+
	// cells up front: it cannot fit at any nonzero column no matter where the
	// column sits, so the addition is never reached with an operand large enough
	// to overflow. This runs once per word, not once per character, so it stays
	// out of the timing-sensitive measure loop above.
	b(0xa5, 0x5a, 0xc9, 0x1f) // LDA $5a ; CMP #31
	bcsAdvanceToLine := len(p)
	b(0xb0, 0)                      // BCS advanceToLine
	b(0x18, 0x65, 0x0a, 0xc9, 0x1f) // CLC ; ADC $0a ; CMP #31 (A holds $5a; the sum is commutative)
	bccPositionCursor := len(p)
	b(0x90, 0)
	advanceToLine := len(p)
	branch(bcsAdvanceToLine, advanceToLine)
	// A word that does not fit ends the visual line, and X is already back at
	// that word's first character, which is the next row's first character too.
	// This runs once per line: the padding loop below re-enters at padLine, not
	// here, because its own back edge would otherwise finish the same row once
	// per padded cell -- each call translating it further right.
	jsrTo(finishRow)
	padLine := len(p)
	// This padding loop's screen-full guard was dead code. `REP #$10` at the top
	// of renderDocument makes the index registers 16-bit, so `CPY #imm` is a
	// three-byte instruction here -- but the bound was emitted as the two-byte
	// `c0 f0`, which swallowed the following `BCC` opcode and decoded as
	// `CPY #$90F0`. The intended `BCC screenNotFull / JMP screenFull` pair then
	// disassembled as two garbage instructions (`ORA $4c,S` / `AND [$9f],Y`)
	// whose only effect was to clobber A, which the very next `LDA $0a`
	// reloaded -- so the loop still terminated on column 30 and the defect stayed
	// invisible. With no live bound, padding could advance Y up to 29 cells past
	// the 510-cell plane and the following draw then wrote a glyph out of bounds
	// (confirmed: a wrap landing at Y=509 wrote 'w' to $11FE, one cell past the
	// plane; a wrap from a lower column reaches $1216, inside the attribute
	// plane). The bound is now a correct three-byte 16-bit immediate and matches
	// the 510-cell limit drawLoop itself uses, rather than the stale 240 (8-row)
	// value from before the plane grew to 30x17.
	b(0xc8, 0xe6, 0x0a, 0xc0, 0xfe, 0x01) // INY ; INC $0a ; CPY #$01FE
	bccScreenNotFull := len(p)
	b(0x90, 0)
	screenFull2Jump := len(p)
	b(0x4c, 0, 0)
	screenNotFull := len(p)
	branch(bccScreenNotFull, screenNotFull)
	b(0xa5, 0x0a, 0xc9, 0x1e)
	bnePadLine := len(p)
	b(0xd0, 0)
	branch(bnePadLine, padLine)
	b(0x64, 0x0a)
	positionCursor := len(p)
	branch(beqPositionCursor, positionCursor)
	branch(bnePositionCursor, positionCursor)
	branch(beqPositionCursor2, positionCursor)
	branch(bccPositionCursor, positionCursor)
	b(0xe4, 0x00)
	bneNotCursor := len(p)
	b(0xd0, 0)
	b(0x84, 0x0b) // STY $0b -- the one true caret screen-position capture
	// Capture the caret's column alongside its cell. Wrap-aware Up/Down needs
	// the caret's (row, column), and knowing the column turns that into pure
	// addition: rowStart = caretCell - caretColumn, with no divide-by-30 loop in
	// a path that already re-renders. This runs at most once per render pass
	// (only at the cursor's own character), so the PHA/PLA around it is cheap --
	// and necessary, because A holds the in-flight glyph here.
	b(0x48)             // PHA
	b(0xa5, 0x0a)       // LDA $0a (column)
	b(0x8d, 0x5c, 0x03) // STA $035c (caret column, low)
	b(0x9c, 0x5d, 0x03) // STZ $035d (high -- kept 0 so 16-bit reads are safe)
	b(0x68)             // PLA
	// INC (not LDA #1/STA) so A -- which holds the in-flight glyph byte here,
	// consumed by the attribute/style logic further down -- is never clobbered.
	b(0xe6, 0x56) // INC $56 (mark found, so the unguarded natural-end/screenFull
	// termination captures further down don't clobber this with a stale position)
	notCursor := len(p)
	branch(bneNotCursor, notCursor)
	b(0xe4, 0x08)
	bneNotDrawCursor := len(p)
	b(0xd0, 0)
	drawCursor2Jump := len(p)
	b(0x4c, 0, 0)
	notDrawCursor := len(p)
	branch(bneNotDrawCursor, notDrawCursor)
	b(0xbd, 0x00, 0x05, 0xc9, 0x0d)
	// The styled draw path below is long enough that a plain BEQ can't reach
	// `newline` in one 8-bit relative branch, so this inverts the test (BNE)
	// and skips over a JMP trampoline instead.
	bneNotNewline2 := len(p)
	b(0xd0, 0)
	jmpNewlineAt := len(p)
	b(0x4c, 0, 0) // JMP newline (patched once the label is known)
	notNewline2 := len(p)
	branch(bneNotNewline2, notNewline2)
	b(0xc9, 0x20)
	bneDrawCharacter := len(p)
	b(0xd0, 0)
	b(0xa5, 0x0a)
	beqDiscardLeadingSpace := len(p)
	b(0xf0, 0)
	b(0xa9, 0x20)
	braDrawCharacter := len(p)
	b(0x80, 0)
	discardLeadingSpace := len(p)
	branch(beqDiscardLeadingSpace, discardLeadingSpace)
	b(0xe8) // discard spaces at the beginning of a visual line
	jmpTo(drawLoop)
	drawCharacter := len(p)
	branch(bneDrawCharacter, drawCharacter)
	branch(braDrawCharacter, drawCharacter)
	// Split path: keep baseline document rendering isolated from proof-map
	// styling so local editing follows the original plain draw path. The
	// styled path below is long enough that a plain BPL can't reach
	// drawCharacterPlain in one 8-bit relative branch, so this inverts the
	// test (BMI) and skips over a JMP trampoline instead.
	b(0x24, 0x1f) // BIT $1f (proofing-active flag in bit 7)
	bmiDrawCharacterStyled := len(p)
	b(0x30, 0)
	jmpDrawCharacterPlainAt := len(p)
	b(0x4c, 0, 0) // JMP drawCharacterPlain (patched once the label is known)
	drawCharacterStyled := len(p)
	branch(bmiDrawCharacterStyled, drawCharacterStyled)
	b(0x48) // PHA (preserve glyph on proofing/style path)
	// Selection highlighting takes priority over proofing and rich style: a
	// selected cell always renders with the spelling-issue visual style
	// (solid fill) and no shape override, regardless of its own proofing/
	// style state -- matching how conventional editors let selection
	// visually override underlying spell/grammar/style indicators. Selection
	// bounds live in $50 (start, inclusive) / $52 (end, exclusive), not the
	// draw loop's own $19 measurement scratch or title-render's $1b
	// byte-count scratch -- both of those are reused within the same render
	// pass and cannot carry selection state here.
	b(0xe4, 0x50) // CPX $50
	bccNotSelected := len(p)
	b(0x90, 0)
	b(0xe4, 0x52) // CPX $52
	bcsNotSelected := len(p)
	b(0xb0, 0)
	b(0xa9, proofingSpellingAttr)
	b(0x99, 0x00, 0x12) // STA $1200,Y
	b(0x64, 0x54)       // STZ $54 (no tile-shape override while selected)
	braStyleApplied := len(p)
	b(0x80, 0)
	notSelected := len(p)
	branch(bccNotSelected, notSelected)
	branch(bcsNotSelected, notSelected)
	// Proofing/style map is staged as one byte per source cell in $0b00,X:
	// bit 0 bold, bit 1 italic, bit 2 underline, bit 3 spelling issue,
	// bit 4 grammar issue. Proofing selects the cell's color palette
	// (grammar wins when both proofing bits are present); style selects an
	// alternate glyph shape via a tile-id page offset (underline wins over
	// bold, bold wins over italic, when more than one style bit is present).
	// The two axes are independent bit fields in the SNES tilemap attribute
	// byte (palette = bits 4-2, tile-id bits 9-8 = bits 1-0) and were
	// verified independent by direct emulator inspection before this code
	// was written.
	b(0xbd, 0x00, 0x0b, 0x29, 0x18) // LDA $0b00,X; AND #(spell|grammar)
	beqPaletteBase := len(p)
	b(0xf0, 0)
	b(0x29, 0x10) // grammar bit?
	beqPaletteSpelling := len(p)
	b(0xf0, 0)
	b(0xa9, proofingGrammarAttr)
	braPaletteReady := len(p)
	b(0x80, 0)
	paletteSpelling := len(p)
	branch(beqPaletteSpelling, paletteSpelling)
	b(0xa9, proofingSpellingAttr)
	braPaletteReady2 := len(p)
	b(0x80, 0)
	paletteBase := len(p)
	branch(beqPaletteBase, paletteBase)
	b(0xa9, documentBaseAttr)
	paletteReady := len(p)
	branch(braPaletteReady, paletteReady)
	branch(braPaletteReady2, paletteReady)
	b(0x48)                         // PHA (stash palette attribute)
	// The three style bits are the glyph page index, so there is no priority to
	// resolve and nothing to look up: bold, italic and underline each own one
	// bit, and every combination is its own page (see glyphShapePixels). LSR
	// splits that page index the way the tilemap splits a tile id: bit 0 becomes
	// the character byte's +128 OR-mask staged in $54, and the remaining two
	// bits are the attribute byte's tile-id bits 8-9, ORed into the palette
	// attribute already stashed on the stack.
	b(0xbd, 0x00, 0x0b, 0x29, 0x07) // LDA $0b00,X; AND #(bold|italic|underline)
	b(0x4a)                         // LSR A: C = bold, A = page >> 1 (0-3)
	b(0x48)                         // PHA (stash the two high page bits)
	b(0xa9, 0x00)
	bccShapeLowZero := len(p)
	b(0x90, 0)
	b(0xa9, 0x80)
	shapeLowZero := len(p)
	branch(bccShapeLowZero, shapeLowZero)
	b(0x85, 0x54)       // STA $54
	b(0x68)             // PLA (page >> 1)
	b(0x03, 0x01)       // ORA $01,S (the palette attribute, left in place)
	b(0x99, 0x00, 0x12) // STA $1200,Y
	b(0x68)             // PLA (discard the palette attribute)
	styleApplied := len(p)
	branch(braStyleApplied, styleApplied)
	b(0x68) // PLA (restore original glyph)
	braDrawCharacterAttrDone := len(p)
	b(0x80, 0)
	drawCharacterPlain := len(p)
	jump(jmpDrawCharacterPlainAt, drawCharacterPlain)
	b(0x64, 0x54) // STZ $54 (no format runs at all this render: no shape override)
	drawCharacterAttrDone := len(p)
	branch(braDrawCharacterAttrDone, drawCharacterAttrDone)
	// Cell hit-test: while a resolution is active ($0340), remember the document
	// character index (X) drawn at the greatest output position not past the
	// target ($0339/$033a). Output position advances monotonically, so the final
	// match is the character at or just before the target -- which is what makes
	// a target past the end of a short line clamp onto that line. A is the glyph
	// being drawn, so it is preserved across this block.
	//
	// The comparison is `CPY`, not the previous `TYA; CMP $0339`. Index registers
	// are 16-bit here, so TYA moved only Y's low byte and the test wrapped every
	// 256 cells; the pointer path only escaped that because it rejects rows >= 8.
	// Wrap-aware Up/Down has to resolve the full 510-cell plane, so the target is
	// now a 16-bit value and the compare is a real 16-bit compare.
	//
	// The found flag also moved from $0342 to $033b: `STX $0341` is a 16-bit
	// store that already owns $0341 AND $0342, so the old flag was overwriting
	// the character index's high byte on every hit.
	b(0x48)             // PHA (preserve glyph)
	b(0xad, 0x40, 0x03) // LDA $0340 (hit-test active?)
	beqHitDone := len(p)
	b(0xf0, 0)          // BEQ hitDone
	b(0xcc, 0x39, 0x03) // CPY $0339 (16-bit compare against the target cell)
	beqHitTake := len(p)
	b(0xf0, 0) // BEQ hitTake (==)
	bccHitTake := len(p)
	b(0x90, 0) // BCC hitTake (<)
	bcsHitDone := len(p)
	b(0xb0, 0) // BCS hitDone (> target)
	hitTake := len(p)
	branch(beqHitTake, hitTake)
	branch(bccHitTake, hitTake)
	b(0x8e, 0x41, 0x03)             // STX $0341 (character index, 16-bit -> $0341/$0342)
	b(0xa9, 0x01, 0x8d, 0x3b, 0x03) // LDA #1; STA $033b (found)
	hitDone := len(p)
	branch(beqHitDone, hitDone)
	branch(bcsHitDone, hitDone)
	b(0x68)       // PLA (restore glyph)
	b(0x05, 0x54) // ORA $54 (rich-style tile-id page bit, 0 when no shape override applies)
	b(0x99, 0x00, 0x10, 0xe8, 0xc8, 0xe6, 0x0a, 0xa5, 0x0a, 0xc9, 0x1e)
	b(0xf0, 0x03)
	jmpTo(drawLoop)
	// A line filled to the last column ends without any padding. Its width is
	// the full plane, so no alignment can move it, but the row still has to be
	// recorded and the next row's alignment picked up.
	jsrTo(finishRow)
	b(0x64, 0x0a)
	jmpTo(drawLoop)
	newline := len(p)
	jump(newlineJump, newline)
	jump(jmpNewlineAt, newline)
	b(0xe8) // INX past the newline, so X is the next row's first character
	// The paragraph ends here, so this is where its width is still known: the
	// padding below raises the column to 30 on its way to the next row.
	jsrTo(finishRow)
	newlineAdvance := len(p)
	b(0xa5, 0x0a)
	beqNewlineDone := len(p)
	b(0xf0, 0)
	b(0xc8, 0xe6, 0x0a, 0xa5, 0x0a, 0xc9, 0x1e)
	bneNewlineAdvance := len(p)
	b(0xd0, 0)
	branch(bneNewlineAdvance, newlineAdvance)
	b(0x64, 0x0a)
	newlineDone := len(p)
	branch(beqNewlineDone, newlineDone)
	jmpTo(drawLoop)
	screenFull := len(p)
	jump(screenFull2Jump, screenFull)
	patch16(screenFullJump, screenFull)
	// Finish the row the plane ran out on before Y stops being a cell index: the
	// caret sentinel below overwrites it.
	jsrTo(finishRow)
	b(0xa5, 0x56) // LDA $56 (true caret already found?)
	bneScreenFullAlreadyFound := len(p)
	b(0xd0, 0)
	b(0xa0, 0xfe, 0x01, 0x84, 0x0b) // LDY #$01FE; STY $0b -- sentinel, only if not already found
	drawCursor := len(p)
	branch(bneScreenFullAlreadyFound, drawCursor)
	jump(drawCursor2Jump, drawCursor)
	patch16(drawCursorJump, drawCursor)
	// Only swap the glyph tile at the caret cell; leave $1200,Y (attribute)
	// untouched. This used to unconditionally stamp documentBaseAttr here,
	// which was harmless only because the caret bug above always misplaced
	// $0b past the end of real text (an already-plain cell); now that $0b
	// correctly lands on the true caret cell, clobbering the attribute here
	// would discard that cell's real proofing/style color whenever the
	// cursor sits on a flagged/styled character. A cell the normal draw pass
	// never reached (cursor past the last character) still reads
	// documentBaseAttr from clearStage's initial fill, so this is safe both
	// ways.
	b(0xad, 0x3e, 0x03) // LDA $033e (caret hidden because the view is scrolled off it?)
	bneCaretHidden := len(p)
	b(0xd0, 0)
	b(0xa4, 0x0b, 0xa9, 0x7f, 0x99, 0x00, 0x10)
	caretHidden := len(p)
	branch(bneCaretHidden, caretHidden)
	b(0xe2, 0x30)
	// Wait for VBlank, then upload tile low bytes and zero attributes.
	// IMPORTANT: render runs with DB=$7e so stage copies can use direct absolute
	// WRAM addressing. Hardware registers like $2115/$43xx/$420b are in bank $00.
	// If DB is left at $7e, absolute STA/LDA in the upload block will silently
	// hit WRAM mirrors instead of PPU/DMA registers, leaving stage updates
	// visible in WRAM but never committed to VRAM/framebuffer.
	vblank := len(p)
	jump(menuUploadJump, vblank)
	b(0xaf, 0x12, 0x42, 0x00) // LDA.l $004212
	bplVblank := len(p)
	b(0x10, 0)
	branch(bplVblank, vblank)
	// Enter DB=$00 for hardware register programming, then restore DB=$7e after
	// upload so subsequent render flow keeps its WRAM addressing contract.
	b(0x8b, 0xa9, 0x00, 0x48, 0xab) // PHB; LDA #$00; PHA; PLB

	// Upload the 18x4 document-name card.
	ldaSta(0x00, 0x2115)
	ldaSta(0x00, 0x4300)
	ldaSta(0x18, 0x4301)
	ldaSta(0x00, 0x4302)
	ldaSta(0x14, 0x4303)
	ldaSta(0x7e, 0x4304)
	b(0xa9, 0x21, 0x85, 0x0d, 0x64, 0x0e, 0xa2, 0x04)
	titleLowRows := len(p)
	b(0xa5, 0x0d)
	sta(0x2116)
	b(0xa5, 0x0e)
	sta(0x2117)
	ldaSta(0x12, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	b(0x18, 0xa5, 0x0d, 0x69, 0x20, 0x85, 0x0d, 0xa5, 0x0e, 0x69, 0x00, 0x85, 0x0e, 0xca)
	bneTitleLowRows := len(p)
	b(0xd0, 0)
	branch(bneTitleLowRows, titleLowRows)
	ldaSta(0x80, 0x2115)
	ldaSta(0x19, 0x4301)
	ldaSta(0x00, 0x4302)
	ldaSta(0x15, 0x4303)
	b(0xa9, 0x21, 0x85, 0x0d, 0x64, 0x0e, 0xa2, 0x04)
	titleHighRows := len(p)
	b(0xa5, 0x0d)
	sta(0x2116)
	b(0xa5, 0x0e)
	sta(0x2117)
	ldaSta(0x12, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	b(0x18, 0xa5, 0x0d, 0x69, 0x20, 0x85, 0x0d, 0xa5, 0x0e, 0x69, 0x00, 0x85, 0x0e, 0xca)
	bneTitleHighRows := len(p)
	b(0xd0, 0)
	branch(bneTitleHighRows, titleHighRows)

	// Upload the 2x11 toolbar plane (bold/italic/underline + alignment
	// buttons), replacing the old static "FAIRYWRITER / SNES ONLINE" card
	// text. Staged at $1c00 (tile-low) / $1c20 (attribute) -- $1600/$1700/
	// $1800/$1900/$1a00 are all already used by browser path staging,
	// word-wrap measurement tables, and command payload/listing buffers
	// (confirmed by an actual regression when this was first tried at
	// $1600/$1620; verify any future WRAM pick against the full ctest
	// suite, not just static reads). VRAM tile row/col address is
	// row*32+col (0-based, verified against the title card's own 0x0021 =
	// row1*32+col1); this plane starts at row 1, col 20 -- the same
	// interior columns the old static text occupied.
	ldaSta(0x00, 0x2115)
	ldaSta(0x00, 0x4300)
	ldaSta(0x18, 0x4301)
	ldaSta(0x00, 0x4302)
	ldaSta(0x1c, 0x4303)
	ldaSta(0x7e, 0x4304)
	b(0xa9, 0x34, 0x85, 0x0d, 0x64, 0x0e, 0xa2, 0x02)
	toolbarLowRows := len(p)
	b(0xa5, 0x0d)
	sta(0x2116)
	b(0xa5, 0x0e)
	sta(0x2117)
	ldaSta(0x0b, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	b(0x18, 0xa5, 0x0d, 0x69, 0x20, 0x85, 0x0d, 0xa5, 0x0e, 0x69, 0x00, 0x85, 0x0e, 0xca)
	bneToolbarLowRows := len(p)
	b(0xd0, 0)
	branch(bneToolbarLowRows, toolbarLowRows)
	ldaSta(0x80, 0x2115)
	ldaSta(0x19, 0x4301)
	ldaSta(0x20, 0x4302)
	ldaSta(0x1c, 0x4303)
	b(0xa9, 0x34, 0x85, 0x0d, 0x64, 0x0e, 0xa2, 0x02)
	toolbarHighRows := len(p)
	b(0xa5, 0x0d)
	sta(0x2116)
	b(0xa5, 0x0e)
	sta(0x2117)
	ldaSta(0x0b, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	b(0x18, 0xa5, 0x0d, 0x69, 0x20, 0x85, 0x0d, 0xa5, 0x0e, 0x69, 0x00, 0x85, 0x0e, 0xca)
	bneToolbarHighRows := len(p)
	b(0xd0, 0)
	branch(bneToolbarHighRows, toolbarHighRows)

	// Upload the 30-cell chapter/word status row.
	ldaSta(0x00, 0x2115)
	ldaSta(0x18, 0x4301)
	ldaSta(0x80, 0x4302)
	ldaSta(0x14, 0x4303)
	ldaSta(0xe1, 0x2116)
	ldaSta(0x00, 0x2117)
	ldaSta(0x1e, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	ldaSta(0x80, 0x2115)
	ldaSta(0x19, 0x4301)
	ldaSta(0x80, 0x4302)
	ldaSta(0x15, 0x4303)
	ldaSta(0xe1, 0x2116)
	ldaSta(0x00, 0x2117)
	ldaSta(0x1e, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)

	// Upload the 30x8 document plane.
	ldaSta(0x00, 0x2115)
	ldaSta(0x00, 0x4300)
	ldaSta(0x18, 0x4301)
	ldaSta(0x00, 0x4302)
	ldaSta(0x10, 0x4303)
	ldaSta(0x7e, 0x4304)
	b(0xa9, 0x41, 0x85, 0x0d, 0xa9, 0x01, 0x85, 0x0e, 0xa2, 0x11)
	lowRows := len(p)
	b(0xa5, 0x0d)
	sta(0x2116)
	b(0xa5, 0x0e)
	sta(0x2117)
	ldaSta(0x1e, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	b(0x18, 0xa5, 0x0d, 0x69, 0x20, 0x85, 0x0d, 0xa5, 0x0e, 0x69, 0x00, 0x85, 0x0e, 0xca)
	bneLowRows := len(p)
	b(0xd0, 0)
	branch(bneLowRows, lowRows)
	ldaSta(0x80, 0x2115)
	ldaSta(0x19, 0x4301)
	ldaSta(0x00, 0x4302)
	ldaSta(0x12, 0x4303)
	b(0xa9, 0x41, 0x85, 0x0d, 0xa9, 0x01, 0x85, 0x0e, 0xa2, 0x11)
	highRows := len(p)
	b(0xa5, 0x0d)
	sta(0x2116)
	b(0xa5, 0x0e)
	sta(0x2117)
	ldaSta(0x1e, 0x4305)
	ldaSta(0x00, 0x4306)
	ldaSta(0x01, 0x420b)
	b(0x18, 0xa5, 0x0d, 0x69, 0x20, 0x85, 0x0d, 0xa5, 0x0e, 0x69, 0x00, 0x85, 0x0e, 0xca)
	bneHighRows := len(p)
	b(0xd0, 0)
	branch(bneHighRows, highRows)
	b(0xab)       // PLB (restore DB=$7e for the render routine epilogue)
	b(0xab, 0x60) // PLB; RTS

	// getbits8: four reads return two inverted, LSB-first bits each.
	get8 := len(p)
	for _, at := range []int{get8Call1, get8Call2} {
		patch16(at, get8)
	}
	b(0x64, 0x07, 0xa2, 0x04)
	bits8Loop := len(p)
	b(0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea)
	b(0xa5, 0x07, 0x4a, 0x4a, 0x85, 0x07, 0xad, 0x17, 0x40, 0x29, 0x03)
	b(0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x05, 0x07, 0x85, 0x07, 0xca)
	bneBits8 := len(p)
	b(0xd0, 0)
	branch(bneBits8, bits8Loop)
	b(0xa5, 0x07, 0x49, 0xff, 0x60)

	// getbits4: identical electrical reads, shifted down to a nibble.
	get4 := len(p)
	patch16(get4Call, get4)
	b(0x64, 0x07, 0xa2, 0x02)
	bits4Loop := len(p)
	b(0xea, 0xea, 0xea, 0xea, 0xea, 0xea, 0xea)
	b(0xa5, 0x07, 0x4a, 0x4a, 0x85, 0x07, 0xad, 0x17, 0x40, 0x29, 0x03)
	b(0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x05, 0x07, 0x85, 0x07, 0xca)
	bneBits4 := len(p)
	b(0xd0, 0)
	branch(bneBits4, bits4Loop)
	b(0xa5, 0x07, 0x49, 0xff, 0x4a, 0x4a, 0x4a, 0x4a, 0x60)

	// Interrupt landing pad. Every vector except RESET points here. It used to
	// point at $8000 -- the reset entry -- so a stray BRK, or NMI the first time
	// anyone sets $4200, would re-run init from scratch on top of a live
	// document rather than returning to what it interrupted.
	irqStub := len(p)
	b(0x40) // RTI
	return p, irqStub
}

func build() ([]byte, int) {
	tilemap, tiles, uploads := encode(scene())
	program, irqStub := emitProgram(len(tiles), uploads)
	menu := mainMenuPlane()
	transition := transitionPlane()
	browserReady := browserReadyPlane()
	help := helpPlane()
	settings := settingsPlane()
	saveFormat := saveFormatPlane()
	filename := filenamePlane()
	saveRootTitle := []byte(" CHOOSE A FOLDER FOR NEW FILE ")
	saveFolderTitle := []byte("  PRESS N: NEW FILE IN FOLDER ")
	// Region layout, checked per bank. Each entry must fit before the next one
	// starts; the final bound in each bank is the reserved window that closes it.
	for _, region := range []struct {
		name  string
		start int
		size  int
		limit int
	}{
		// Bank 0: program, then the scan map, then the v3 extended header.
		{"program", 0, len(program), scanMapOffset},
		{"scan map", scanMapOffset, 256, 0x7fb0},
		// Bank 1: fixed pages, then the variable tile blob.
		{"palette", paletteOffset, 128, tilemapOffset},
		{"tilemap", tilemapOffset, 2048, menuOffset},
		{"menu plane", menuOffset, len(menu), browserOffset},
		{"browser plane", browserOffset, len(transition), browserReadyOffset},
		{"browser-ready plane", browserReadyOffset, len(browserReady), helpOffset},
		{"help plane", helpOffset, len(help), settingsOffset},
		{"settings plane", settingsOffset, len(settings), saveFormatOffset},
		{"save-format plane", saveFormatOffset, len(saveFormat), saveRootTitleOffset},
		{"save root title", saveRootTitleOffset, len(saveRootTitle), saveFolderTitleOffset},
		{"save folder title", saveFolderTitleOffset, len(saveFolderTitle), filenameOffset},
		{"filename plane", filenameOffset, len(filename), tilesOffset},
		{"PPU tiles", tilesOffset, len(tiles), bank1Limit},
	} {
		if region.start+region.size > region.limit {
			panic(fmt.Sprintf("cartridge layout overflow: %s at %#x is %d bytes, %d past its %#x limit",
				region.name, region.start, region.size,
				region.start+region.size-region.limit, region.limit))
		}
		if region.start/0x8000 != (region.start+region.size-1)/0x8000 {
			panic(fmt.Sprintf("%s at %#x spans a bank boundary; DMA and absolute reads cannot follow it",
				region.name, region.start))
		}
	}
	rom := make([]byte, romSize)
	copy(rom, program)
	copy(rom[menuOffset:], menu)
	copy(rom[browserOffset:], transition)
	copy(rom[browserReadyOffset:], browserReady)
	copy(rom[helpOffset:], help)
	copy(rom[settingsOffset:], settings)
	copy(rom[saveFormatOffset:], saveFormat)
	copy(rom[saveRootTitleOffset:], saveRootTitle)
	copy(rom[saveFolderTitleOffset:], saveFolderTitle)
	copy(rom[filenameOffset:], filename)
	scanMap := xbandScanMap()
	copy(rom[scanMapOffset:], scanMap[:])
	copy(rom[tilemapOffset:], tilemap)
	copy(rom[tilesOffset:], tiles)
	pal := []uint16{color(0, 0, 0), color(2, 2, 5), color(8, 9, 14), color(3, 4, 14), color(5, 7, 23), color(9, 12, 28), color(27, 29, 28), color(12, 2, 2), color(19, 5, 3), color(25, 11, 4), color(30, 20, 8), color(31, 27, 17), color(3, 5, 7), color(6, 8, 10), color(31, 25, 2), color(31, 31, 29)}
	selected := append([]uint16(nil), pal...)
	for _, index := range []int{2, 3, 4, 5} {
		selected[index] = pal[15]
	}
	for _, index := range []int{6, 14, 15} {
		selected[index] = pal[4]
	}
	// Sub-palette 2, selected by `documentBaseAttr`, is what every document,
	// title and toolbar cell renders through. It used to remap index 4 (the blue
	// the static panels are already painted in) to index 7, the maroon -- so the
	// cell planes painted maroon over their own blue panels. Leaving index 4
	// alone lets the document surface match the panel it sits in.
	title := append([]uint16(nil), pal...)
	title[15] = pal[15]
	chapter := append([]uint16(nil), pal...)
	chapter[4] = pal[4]
	chapter[15] = pal[14]
	pal = append(pal, selected...)
	pal = append(pal, title...)
	pal = append(pal, chapter...)
	for i, v := range pal {
		binary.LittleEndian.PutUint16(rom[paletteOffset+i*2:], v)
	}
	// v3 extended header at $ffb0. Setting the developer ID below to $33 is
	// precisely what tells a loader these sixteen bytes are real, and they were
	// never written -- readers got whatever the scan-map tail happened to leave
	// behind. Order is maker code, game code, six reserved bytes, expansion
	// flash size, expansion RAM size, special version, chipset subtype.
	x := 0x7fb0
	copy(rom[x:x+2], []byte("FW"))
	copy(rom[x+2:x+6], []byte("FWTR"))

	h := 0x7fc0
	copy(rom[h:h+21], []byte("FAIRYWRITER SNES     "))
	rom[h+0x15] = 0x20 // 001smmmm: s=0 SlowROM, mmmm=0 LoROM
	rom[h+0x16] = 0x02 // ROM + RAM + battery
	rom[h+0x17] = 6    // 1<<6 KiB = 64 KiB ROM
	rom[h+0x18] = 5    // 32 KiB SRAM hosts backend state and the bring-up fallback
	rom[h+0x19] = 1    // NTSC / North America
	rom[h+0x1a] = 0x33 // developer ID $33 => the v3 extended header above is present
	rom[h+0x1b] = 0    // ROM version, written explicitly rather than left implicit

	// Vectors. Everything except RESET lands on the RTI stub; RESET enters the
	// init path at the start of the program. Native vectors live at $ffe0-$ffef
	// and emulation vectors at $fff0-$ffff; the unused slots in both tables stay
	// pointed at the stub so a spurious entry returns instead of running data.
	stub := loromAddr(irqStub)
	for off := 0x7fe0; off < 0x8000; off += 2 {
		binary.LittleEndian.PutUint16(rom[off:], stub)
	}
	binary.LittleEndian.PutUint16(rom[0x7ffc:], loromAddr(0)) // RESET

	// Reserved header-scoring windows. snes_other.c does not read the map-mode
	// byte to pick a mapper: it scores every candidate header location the image
	// is large enough to contain and derives the mapping from where the winner
	// sits (`location < 0x9000 ? LoROM : HiROM`). At 64 KiB that means $81c0 and
	// $ffc0 are now probed, and either one outscoring the real header at $7fc0
	// would silently load this cartridge as HiROM. Zeros score about -16 against
	// the real header's 47. The windows start 0x10 early because readHeader
	// reaches back that far for the v3 extended fields.
	for _, window := range [][2]int{{0x81b0, 0x8200}, {bank1Limit, romSize}} {
		for i := window[0]; i < window[1]; i++ {
			if rom[i] != 0 {
				panic(fmt.Sprintf("reserved header-scoring window %#x-%#x is not zero at %#x; "+
					"a rival header there can make the loader map this cartridge as HiROM",
					window[0], window[1], i))
			}
		}
	}

	// Canonical checksum: the sum is taken with the checksum field reading
	// $0000 and its complement reading $ffff, so the pair cancels itself out.
	// Zeroing both -- as this did -- lands exactly 0x1fe low, which this
	// emulator accepts (it only checks the pair sums to $ffff) and a real
	// verifier rejects.
	binary.LittleEndian.PutUint16(rom[0x7fdc:], 0xffff)
	binary.LittleEndian.PutUint16(rom[0x7fde:], 0x0000)
	var sum uint32
	for _, v := range rom {
		sum += uint32(v)
	}
	check := uint16(sum)
	binary.LittleEndian.PutUint16(rom[0x7fde:], check)
	binary.LittleEndian.PutUint16(rom[0x7fdc:], check^0xffff)
	return rom, len(tiles) / 32
}

func main() {
	if len(os.Args) < 2 || len(os.Args) > 3 {
		fmt.Fprintln(os.Stderr, "usage: fairywriter-rom OUTPUT.sfc [MAILBOX_FIXTURE.srm]")
		os.Exit(2)
	}
	rom, tiles := build()
	if err := os.WriteFile(os.Args[1], rom, 0644); err != nil {
		panic(err)
	}
	if len(os.Args) == 3 {
		sram := make([]byte, 32*1024)
		sram[0], sram[1], sram[2] = 'A', 1, 0
		if err := os.WriteFile(os.Args[2], sram, 0644); err != nil {
			panic(err)
		}
	}
	keys := make([]byte, 0, len(glyphs))
	for k := range glyphs {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })
	fmt.Printf("wrote %s: %d bytes, %d unique PPU tiles, %d resident glyphs\n", os.Args[1], len(rom), tiles, len(keys))
}
