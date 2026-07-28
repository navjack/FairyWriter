package main

import (
	"fmt"
	"os"
)

func main() {
	if len(os.Args) != 3 { panic("usage: embed-rom INPUT OUTPUT") }
	data, err := os.ReadFile(os.Args[1]); if err != nil { panic(err) }
	out, err := os.Create(os.Args[2]); if err != nil { panic(err) }; defer out.Close()
	fmt.Fprintln(out, "// Generated from the source-owned FairyWriter cartridge. Do not edit.")
	fmt.Fprintln(out, "#include <cstddef>")
	fmt.Fprintln(out, "#include <cstdint>")
	fmt.Fprintln(out, "#include \"cartridge_image.h\"")
	fmt.Fprintln(out, "namespace FairyWriter {")
	fmt.Fprintf(out, "static const std::uint8_t kImage[] = {")
	for i, b := range data { if i%12 == 0 { fmt.Fprint(out, "\n ") }; fmt.Fprintf(out, "0x%02x, ", b) }
	fmt.Fprintln(out, "\n};")
	fmt.Fprintf(out, "const std::uint8_t* cartridgeImage() noexcept { return kImage; }\nstd::size_t cartridgeImageSize() noexcept { return sizeof(kImage); }\n}")
}
