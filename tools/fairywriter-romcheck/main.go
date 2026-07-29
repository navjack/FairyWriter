// fairywriter-romcheck validates the source-owned FairyWriter cartridge against
// the SNES conventions a real loader enforces, rather than against what the
// vendored emulator happens to tolerate.
//
// The distinction matters. snesrecomp accepts the image if the checksum pair
// merely sums to $ffff, and it never consults the map-mode byte at all -- it
// picks LoROM or HiROM purely from which candidate header location scores
// highest. Both behaviours hid real defects: a checksum 0x1fe off from the
// canonical value, and a cartridge whose mapping is decided by data it does not
// control. This tool checks the parts a lenient loader skips.
//
// Usage: fairywriter-romcheck CARTRIDGE.sfc
package main

import (
	"encoding/binary"
	"fmt"
	"os"
)

// candidateLocations mirrors snes_other.c's snes_loadRom: it probes up to four
// header positions, gated on image size, and takes the highest scoring one. The
// odd entries are the copier-headered (+512 byte) variants.
var candidateLocations = []struct {
	name   string
	offset int
	minLen int
}{
	{"LoROM $7fc0", 0x7fc0, 0x8000},
	{"LoROM headered $81c0", 0x81c0, 0x8200},
	{"HiROM $ffc0", 0xffc0, 0x10000},
	{"HiROM headered $101c0", 0x101c0, 0x10200},
}

// score replicates snes_other.c readHeader's scoring exactly. Any drift here is
// a bug in this tool, not a policy choice -- the point is to predict which
// header the loader will actually pick.
func score(data []byte, location int) int {
	at := func(i int) byte { return data[location+i] }
	speed := at(0x15) >> 4
	kind := at(0x15) & 0xf
	coprocessor := at(0x16) >> 4
	chips := at(0x16) & 0xf
	region := at(0x19)
	complement := uint16(at(0x1d))<<8 + uint16(at(0x1c))
	checksum := uint16(at(0x1f))<<8 + uint16(at(0x1e))

	s := 0
	s += pick(speed == 2 || speed == 3, 5, -4)
	s += pick(kind <= 3 || kind == 5, 5, -2)
	s += pick(coprocessor <= 5 || coprocessor >= 0xe, 5, -2)
	s += pick(chips <= 6 || chips == 9 || chips == 0xa, 5, -2)
	s += pick(region <= 0x14, 5, -2)
	s += pick(int(checksum)+int(complement) == 0xffff, 8, -6)

	reset := uint16(at(0x3c)) | uint16(at(0x3d))<<8
	s += pick(reset >= 0x8000, 8, -20)

	opcodeLoc := location + 0x40 - 0x8000 + int(reset&0x7fff)
	opcode := byte(0xff)
	if opcodeLoc < len(data) && opcodeLoc >= 0 {
		opcode = data[opcodeLoc]
	} else {
		s -= 14
	}
	if opcode == 0x78 || opcode == 0x18 { // sei, clc
		s += 6
	}
	if opcode == 0x4c || opcode == 0x5c || opcode == 0x9c { // jmp abs, jml abl, stz abs
		s += 3
	}
	if opcode == 0x00 || opcode == 0xff || opcode == 0xdb { // brk, sbc alx, stp
		s -= 6
	}
	return s
}

func pick(cond bool, yes, no int) int {
	if cond {
		return yes
	}
	return no
}

type report struct {
	failures []string
}

func (r *report) fail(format string, args ...interface{}) {
	r.failures = append(r.failures, fmt.Sprintf(format, args...))
}

func (r *report) check(cond bool, format string, args ...interface{}) {
	if !cond {
		r.fail(format, args...)
	}
}

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: fairywriter-romcheck CARTRIDGE.sfc")
		os.Exit(2)
	}
	rom, err := os.ReadFile(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, "romcheck:", err)
		os.Exit(1)
	}
	r := &report{}

	// Size must be a power of two and at least one bank. A non-power-of-two
	// image is legal on hardware but makes the checksum rule conditional, and
	// snes_other.c would mirror-expand it behind our back.
	r.check(len(rom) >= 0x8000, "image is %d bytes, below the 32 KiB minimum", len(rom))
	r.check(len(rom)&(len(rom)-1) == 0, "image size %d is not a power of two", len(rom))
	if len(r.failures) > 0 {
		r.print()
		os.Exit(1)
	}

	const h = 0x7fc0

	// Map mode. The byte is 001smmmm: bit 4 is the speed and the low nibble is
	// the mapping. The header sits at $7fc0, which is the LoROM position, so the
	// low nibble must say LoROM -- a mismatch is how an image ends up declaring
	// one mapping and being loaded as another.
	mapMode := rom[h+0x15]
	r.check(mapMode&0xf0 == 0x20, "map mode %#02x is not of the form 001smmmm", mapMode)
	r.check(mapMode&0x0f == 0x0, "header lives at $7fc0 (LoROM) but map mode %#02x declares mapping %d",
		mapMode, mapMode&0x0f)

	// ROM size byte is 1<<N KiB and must cover the actual image.
	declared := 1024 << rom[h+0x17]
	r.check(declared == len(rom), "header declares %d bytes of ROM, image is %d", declared, len(rom))

	// Developer ID $33 is a promise that the v3 extended header at $ffb0 is
	// real. Making the promise without writing the bytes hands the loader
	// whatever happened to precede the header.
	if rom[h+0x1a] == 0x33 {
		ext := rom[0x7fb0:0x7fc0]
		printable := true
		for _, c := range ext[:6] {
			if c < 0x20 || c > 0x7e {
				printable = false
			}
		}
		r.check(printable, "developer ID $33 declares a v3 extended header, but $ffb0 is % x", ext)
	}

	// Canonical checksum: computed with the checksum reading $0000 and its
	// complement reading $ffff so the stored pair cancels itself out.
	stored := binary.LittleEndian.Uint16(rom[0x7fde:])
	storedComplement := binary.LittleEndian.Uint16(rom[0x7fdc:])
	canonical := append([]byte(nil), rom...)
	binary.LittleEndian.PutUint16(canonical[0x7fdc:], 0xffff)
	binary.LittleEndian.PutUint16(canonical[0x7fde:], 0x0000)
	var sum uint16
	for _, v := range canonical {
		sum += uint16(v)
	}
	r.check(sum == stored, "checksum is %#04x, canonical value is %#04x (off by %d)",
		stored, sum, int(sum)-int(stored))
	r.check(stored^0xffff == storedComplement, "checksum complement %#04x does not invert checksum %#04x",
		storedComplement, stored)

	// Vectors. RESET runs in emulation mode and must land on real code; the
	// others must at least resolve into the cartridge rather than into open bus.
	reset := binary.LittleEndian.Uint16(rom[0x7ffc:])
	r.check(reset >= 0x8000, "reset vector %#04x is below $8000", reset)
	if reset >= 0x8000 {
		op := rom[int(reset-0x8000)]
		r.check(op != 0x00 && op != 0xff && op != 0xdb,
			"reset vector %#04x lands on %#02x, which is not plausible entry code", reset, op)
	}
	for off := 0x7fe0; off < 0x8000; off += 2 {
		v := binary.LittleEndian.Uint16(rom[off:])
		r.check(v >= 0x8000, "vector at %#x is %#04x, which is below $8000", off, v)
	}

	// The decisive check. snes_other.c does not read the map-mode byte to choose
	// a mapper -- it scores every candidate header location the image is large
	// enough to contain and takes the winner, deriving the mapping from where
	// that header sits (`location < 0x9000 ? LoROM : HiROM`). So a cartridge can
	// be loaded as HiROM purely because some tile or plane data at $ffc0 happens
	// to score well. Reserving those windows is the fix; this is the check that
	// the reservation is working.
	want := score(rom, h)
	fmt.Printf("header candidates (image %d bytes):\n", len(rom))
	fmt.Printf("  %-24s score %3d   <- declared\n", candidateLocations[0].name, want)
	for _, c := range candidateLocations[1:] {
		if len(rom) < c.minLen {
			fmt.Printf("  %-24s not probed at this size\n", c.name)
			continue
		}
		got := score(rom, c.offset)
		mapping := "LoROM"
		if c.offset >= 0x9000 {
			mapping = "HiROM"
		}
		fmt.Printf("  %-24s score %3d   (would load as %s)\n", c.name, got, mapping)
		r.check(got < want, "candidate header at %#x scores %d against the real header's %d, "+
			"so the loader would map this cartridge as %s", c.offset, got, want, mapping)
	}

	r.print()
	if len(r.failures) > 0 {
		os.Exit(1)
	}
	fmt.Println("romcheck: OK")
}

func (r *report) print() {
	for _, f := range r.failures {
		fmt.Fprintln(os.Stderr, "romcheck: "+f)
	}
}
