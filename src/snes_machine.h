#ifndef FAIRYWRITER_SNES_MACHINE_H
#define FAIRYWRITER_SNES_MACHINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FairySnesMachine FairySnesMachine;

FairySnesMachine *fairy_snes_create(const uint8_t *rom, size_t size);
void fairy_snes_destroy(FairySnesMachine *machine);
bool fairy_snes_run_frame(FairySnesMachine *machine);
const uint32_t *fairy_snes_framebuffer(const FairySnesMachine *machine);
bool fairy_snes_key_event(FairySnesMachine *machine, uint8_t scancode,
                          bool pressed, bool extended);
void fairy_snes_mouse_event(FairySnesMachine *machine, int8_t dx, int8_t dy,
                            bool left, bool right);
uint8_t fairy_snes_debug_wram(const FairySnesMachine *machine, uint32_t address);
uint8_t fairy_snes_debug_bus_read(const FairySnesMachine *machine, uint32_t address);
void fairy_snes_debug_bus_write(FairySnesMachine *machine, uint32_t address, uint8_t value);
uint16_t fairy_snes_debug_vram_word(const FairySnesMachine *machine, uint16_t address);
uint8_t fairy_snes_debug_aram(const FairySnesMachine *machine, uint16_t address);
uint8_t fairy_snes_debug_dsp_reg(const FairySnesMachine *machine, uint8_t reg);

// S-DSP output. One "block" is the 534 native 32040 Hz stereo samples the DSP
// produces per emulated NTSC frame; fairy_snes_audio_read consumes exactly one
// block and resamples it to `frames` stereo frames for the host's output rate.
// It returns false without consuming anything when a full block is not yet
// buffered, so the caller emits silence rather than stalling the guest.
// All three are callable only from the thread that drives fairy_snes_run_frame.
int fairy_snes_audio_blocks(const FairySnesMachine *machine);
bool fairy_snes_audio_read(FairySnesMachine *machine, int16_t *out, int frames);
void fairy_snes_audio_discard(FairySnesMachine *machine);

#ifdef __cplusplus
}
#endif

#endif
