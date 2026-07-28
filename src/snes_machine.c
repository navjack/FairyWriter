#include "snes_machine.h"

#include "snes/apu.h"
#include "snes/dma.h"
#include "snes/dsp.h"
#include "snes/dsp_shadow.h"
#include "snes/interp816.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FairySnesMachine {
  Snes *snes;
  Interp816 *cpu;
  uint64_t completed_frames;
  uint32_t pixels[256 * 239];
};

uint8 g_ram[0x20000];
Snes *g_snes;
Ppu *g_ppu;
bool g_new_ppu = true;
bool g_fail = false;

void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void RtlApuWrite(uint16 adr, uint8 val) { g_snes->apu->inPorts[adr & 3] = val; }
void rtl_accumulate_apu_catchup(void) {}
void debug_on_wram_write_byte(uint32 a, uint8 o, uint8 n) { (void)a; (void)o; (void)n; }
void debug_on_wram_write_word(uint32 a, uint16 o, uint16 n) { (void)a; (void)o; (void)n; }
void ppudma_record_dma(int ch, int from_b, uint8 bank, uint16 adr,
                       uint8 badr, uint16 size) {
  (void)ch; (void)from_b; (void)bank; (void)adr; (void)badr; (void)size;
}
int interp816_opcode_hook(uint32_t addr) { (void)addr; return 0; }
DspShadow *dsp_shadow_create(void) { return NULL; }
void dsp_shadow_free(DspShadow *shadow) { (void)shadow; }
void dsp_shadow_process(DspShadow *shadow, Dsp *dsp, int left, int right,
                        int *out_left, int *out_right) {
  (void)shadow; (void)dsp; *out_left = left; *out_right = right;
}
void dsp_shadow_verify_brr(const uint8_t *aram, uint16_t start, int a, int b,
                           const int16_t *canon) {
  (void)aram; (void)start; (void)a; (void)b; (void)canon;
}
void dsp_shadow_verify_echo(const int16_t *left, const int16_t *right,
                            const int8_t *coeff, int index, int sum_left,
                            int sum_right) {
  (void)left; (void)right; (void)coeff; (void)index; (void)sum_left; (void)sum_right;
}
void Die(const char *error) {
  fprintf(stderr, "FairyWriter SNES fatal: %s\n", error ? error : "unknown");
  abort();
}

static uint8_t bus_read(void *opaque, uint32_t address) {
  FairySnesMachine *machine = (FairySnesMachine *)opaque;
  return snes_read(machine->snes, address);
}

static void bus_write(void *opaque, uint32_t address, uint8_t value) {
  FairySnesMachine *machine = (FairySnesMachine *)opaque;
  snes_write(machine->snes, address, value);
}

static void clock_position(FairySnesMachine *machine) {
  Snes *snes = machine->snes;
  Interp816 *cpu = machine->cpu;
  if (snes->vIrqEnabled && snes->hIrqEnabled) {
    if (snes->vPos == snes->vTimer + 1 && snes->hPos == 4 * snes->hTimer) {
      snes->inIrq = true; cpu->irqWanted = true;
    }
  } else if (snes->vIrqEnabled) {
    if (snes->vPos == snes->vTimer + 1 && snes->hPos == 1024) {
      snes->inIrq = true; cpu->irqWanted = true;
    }
  } else if (snes->hIrqEnabled && snes->hPos == 4 * snes->hTimer) {
    snes->inIrq = true; cpu->irqWanted = true;
  }

  if (snes->hPos == 0) {
    if (snes->vPos < 240)
      ppu_runLine(snes->ppu, snes->vPos);
    bool starting_vblank = false;
    if (snes->vPos == 0) {
      snes->inVblank = false; snes->inNmi = false;
      dma_startDma(snes->dma, 0, true);
    } else if (!snes->inVblank) {
      // Static-recomp $4212 polling can advance h/v counters in coarse jumps.
      // Enter vblank once we have crossed the mode-dependent threshold, rather
      // than requiring an exact one-line match that can be skipped.
      const bool overscan = ppu_checkOverscan(snes->ppu);
      const int vblank_start = overscan ? 240 : 225;
      starting_vblank = snes->vPos >= vblank_start;
    }
    if (starting_vblank) {
      ppu_handleVblank(snes->ppu);
      snes->inVblank = true; snes->inNmi = true;
      if (snes->nmiEnabled) cpu->nmiWanted = true;
      if (snes->autoJoyRead) snes->autoJoyTimer = 0;
    }
  } else if (snes->hPos == 1024 && !snes->inVblank) {
    dma_cycle(snes->dma);
  }

  snes->hPos += 2;
  if (snes->hPos == 1364) {
    snes->hPos = 0;
    if (++snes->vPos == 262) {
      snes->vPos = 0;
      machine->completed_frames++;
    }
  }
}

FairySnesMachine *fairy_snes_create(const uint8_t *rom, size_t size) {
  if (!rom || size < 0x8000 || size > 0x7fffffff)
    return NULL;
  FairySnesMachine *machine = calloc(1, sizeof(*machine));
  if (!machine)
    return NULL;
  memset(g_ram, 0, sizeof(g_ram));
  machine->snes = snes_init(g_ram);
  if (!machine->snes || !snes_loadRom(machine->snes, rom, (int)size)) {
    fairy_snes_destroy(machine);
    return NULL;
  }
  g_snes = machine->snes;
  g_ppu = machine->snes->ppu;
  PpuBeginDrawing(g_ppu, (uint8_t *)machine->pixels, 256 * sizeof(uint32_t), 0);
  snes_reset(machine->snes, true);
  machine->cpu = interp816_init(machine, bus_read, bus_write);
  if (!machine->cpu) {
    fairy_snes_destroy(machine);
    return NULL;
  }
  interp816_reset(machine->cpu);
  return machine;
}

void fairy_snes_destroy(FairySnesMachine *machine) {
  if (!machine)
    return;
  if (machine->cpu)
    interp816_free(machine->cpu);
  if (machine->snes)
    snes_free(machine->snes);
  if (g_snes == machine->snes) { g_snes = NULL; g_ppu = NULL; }
  free(machine);
}

bool fairy_snes_run_frame(FairySnesMachine *machine) {
  if (!machine || !machine->cpu)
    return false;
  uint64_t target = machine->completed_frames + 1;
  unsigned guard = 20000000;
  while (machine->completed_frames < target && guard--) {
    int cycles = interp816_runOpcode(machine->cpu);
    if (cycles <= 0) cycles = 1;
    int master = cycles * 8;
    for (int i = 0; i < master; i += 2)
      clock_position(machine);
  }
  return machine->completed_frames == target;
}

const uint32_t *fairy_snes_framebuffer(const FairySnesMachine *machine) {
  return machine ? machine->pixels : NULL;
}

bool fairy_snes_key_event(FairySnesMachine *machine, uint8_t scancode,
                          bool pressed, bool extended) {
  return machine && snes_xband_key_event(machine->snes, scancode, pressed, extended);
}

void fairy_snes_mouse_event(FairySnesMachine *machine, int8_t dx, int8_t dy,
                            bool left, bool right) {
  if (machine) snes_mouse_event(machine->snes, dx, dy, left, right);
}

uint8_t fairy_snes_debug_wram(const FairySnesMachine *machine, uint32_t address) {
  if (!machine || address >= sizeof(g_ram))
    return 0;
  return g_ram[address];
}

uint8_t fairy_snes_debug_bus_read(const FairySnesMachine *machine, uint32_t address) {
  if (!machine || !machine->snes) return 0;
  return snes_read(machine->snes, address);
}

void fairy_snes_debug_bus_write(FairySnesMachine *machine, uint32_t address, uint8_t value) {
  if (machine) bus_write(machine, address, value);
}

uint16_t fairy_snes_debug_vram_word(const FairySnesMachine *machine, uint16_t address) {
  if (!machine || !machine->snes || !machine->snes->ppu) return 0;
  return machine->snes->ppu->vram[address & 0x7fff];
}
