#include "emulation/Emulation.h"
#include "emulation/GPU.h"
#include "emulation/Memory.h"

#include <cstdio>
#include <vector>

int main()
{
    gb::Emulation emu;
    emu.setBootRomEnabled(true);
    emu.reset();

    const gb::Memory &mem = emu.memory();

    std::printf("PC after reset: 0x%04X\n", emu.cpu().registers().pc);
    std::printf("boot rom mapped: %d\n", mem.isBootRomEnabled() ? 1 : 0);

    for (int frame = 0; frame < 120; ++frame) {
        emu.runFrame();
        if (frame % 20 == 0 || frame == 119) {
            std::size_t nonZero = 0;
            for (std::size_t i = 0; i < gb::kVideoRamSize; ++i) {
                if (mem.videoRam()[i] != 0) {
                    ++nonZero;
                }
            }
            std::printf("frame %3d  PC=0x%04X  LY=%3d  boot=%d  vram_nonzero=%zu  tile0=%02X %02X\n",
                        frame,
                        emu.cpu().registers().pc,
                        emu.gpu().currentLine(),
                        mem.isBootRomEnabled() ? 1 : 0,
                        nonZero,
                        mem.videoRam()[0],
                        mem.videoRam()[1]);
        }
    }
    return 0;
}
