#ifndef FAIRYWRITER_CARTRIDGE_IMAGE_H
#define FAIRYWRITER_CARTRIDGE_IMAGE_H

#include <cstddef>
#include <cstdint>

namespace FairyWriter {
const std::uint8_t* cartridgeImage() noexcept;
std::size_t cartridgeImageSize() noexcept;
}

#endif
