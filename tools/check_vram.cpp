#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>
#include "../include/emulation/Emulation.h"
#include "../include/emulation/Memory.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom_file>\n";
        return 1;
    }

    std::ifstream romFile(argv[1], std::ios::binary);
    if (!romFile) {
        std::cerr << "Failed to open ROM: " << argv[1] << "\n";
        return 1;
    }

    std::vector<uint8_t> romData((std::istreambuf_iterator<char>(romFile)),
                                  std::istreambuf_iterator<char>());
    romFile.close();

    gb::Emulation emu;
    std::string error;
    if (!emu.loadRom(romData, error)) {
        std::cerr << "Failed to load ROM: " << error << "\n";
        return 1;
    }
    
    emu.reset();
    
    std::cout << "ROM loaded and reset.\n";
    std::cout << "Boot ROM enabled: " << (emu.memory().isBootRomEnabled() ? "YES" : "NO") << "\n";
    std::cout << "First 16 bytes at 0x0000: ";
    for (int i = 0; i < 16; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)emu.memory().read(i) << " ";
    }
    std::cout << "\n\nRunning 500000 cycles...\n";
    
    for (int i = 0; i < 500000; ++i) {
        emu.step();
        if (i == 50000 || i == 100000 || i == 200000) {
            std::cout << "After " << std::dec << i << " cycles - Boot ROM: " 
                      << (emu.memory().isBootRomEnabled() ? "enabled" : "DISABLED") << "\n";
        }
    }
    
    std::cout << "After 500000 cycles - Boot ROM: " 
              << (emu.memory().isBootRomEnabled() ? "enabled" : "DISABLED") << "\n";

    std::cout << "\nVRAM sample (0x8000-0x801F):\n";
    bool allSame = true;
    uint8_t firstByte = emu.memory().read(0x8000);
    
    for (uint16_t addr = 0x8000; addr < 0x8020; ++addr) {
        uint8_t val = emu.memory().read(addr);
        if (val != firstByte) allSame = false;
        
        if ((addr & 0xF) == 0) {
            std::cout << "\n" << std::hex << std::setw(4) << std::setfill('0') << addr << ": ";
        }
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)val << " ";
    }
    
    std::cout << "\n\nVRAM sample (0x9000-0x901F):\n";
    for (uint16_t addr = 0x9000; addr < 0x9020; ++addr) {
        uint8_t val = emu.memory().read(addr);
        if (val != firstByte) allSame = false;
        
        if ((addr & 0xF) == 0) {
            std::cout << "\n" << std::hex << std::setw(4) << std::setfill('0') << addr << ": ";
        }
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)val << " ";
    }
    
    std::cout << "\n\n";
    
    if (allSame) {
        std::cout << "WARNING: All sampled VRAM bytes are 0x" 
                  << std::hex << std::setw(2) << std::setfill('0') << (int)firstByte 
                  << " - VRAM may not be initialized!\n";
        return 1;
    } else {
        std::cout << "SUCCESS: VRAM contains varying data\n";
        return 0;
    }
}
