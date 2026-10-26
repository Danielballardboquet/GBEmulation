/**
 * @file memory.cpp
 * @brief Implementation of the memory bus and the cartridge mappers.
 *
 * The bus owns every addressable byte of the Game Boy and forwards the writes
 * to the component that owns the addressed byte. The cartridge controllers
 * (MBC1, MBC2, MBC3 and MBC5) are implemented in the private ROM and RAM
 * helpers, together with the timer, the serial port and the joypad.
 */

#include "emulation/Memory.h"
#include "emulation/GPU.h"
#include "emulation/Interrupt.h"
#include "emulation/Types.h"
#include "emulation/Utilities.h"

#include <cstring>
#include <ctime>

namespace gb {

namespace {

/** @brief Minimum size of a valid ROM image (header ends at 0x014F). */
constexpr std::size_t kMinRomSize = 0x150;

/**
 * @brief Returns the number of 16 KiB ROM banks coded by the header byte.
 * @param code The ROM size byte at 0x0148.
 * @return The number of banks.
 */
int romBanksFromCode(u8 code)
{
    switch (code) {
    case 0x00: return 2;
    case 0x01: return 4;
    case 0x02: return 8;
    case 0x03: return 16;
    case 0x04: return 32;
    case 0x05: return 64;
    case 0x06: return 128;
    case 0x07: return 256;
    case 0x08: return 512;
    case 0x52: return 72;
    case 0x53: return 80;
    case 0x54: return 96;
    default:   return 2;
    }
}

/**
 * @brief Returns the number of 8 KiB RAM banks coded by the header byte.
 * @param code The RAM size byte at 0x0149.
 * @return The number of banks (0 when no RAM is present).
 */
int ramBanksFromCode(u8 code)
{
    switch (code) {
    case 0x00: return 0;
    case 0x01: return 1;  /**< 2 KiB effectively, treated as one bank. */
    case 0x02: return 1;
    case 0x03: return 4;
    case 0x04: return 16;
    case 0x05: return 8;  /**< MBC2: 512 nibbles. */
    default:   return 0;
    }
}

} // namespace

Memory::Memory()
{
    m_bootRom = util::bootRom();
}

Memory::~Memory() = default;

void Memory::setGPU(GPU *gpu)
{
    m_gpu = gpu;
}

void Memory::setInterrupts(InterruptController *interrupts)
{
    m_interrupts = interrupts;
}

bool Memory::loadCartridge(const std::vector<u8> &data, std::string &error)
{
    unloadCartridge();
    if (data.size() < kMinRomSize) {
        error = "ROM image is too small to contain a cartridge header";
        return false;
    }
    m_cart.rom = data;
    if (!decodeHeader(error)) {
        m_cart.rom.clear();
        return false;
    }
    m_cart.ram.assign(static_cast<std::size_t>(m_cart.ramBanks) * 0x2000, 0);
    return true;
}

void Memory::unloadCartridge()
{
    m_cart = Cartridge{};
    m_romBank = 1;
    m_ramBank = 0;
    m_ramEnabled = false;
    m_bankingMode = false;
}

bool Memory::hasCartridge() const
{
    return !m_cart.rom.empty();
}

const Cartridge &Memory::cartridge() const
{
    return m_cart;
}

const std::vector<u8> &Memory::saveData() const
{
    return m_cart.ram;
}

void Memory::loadSaveData(const std::vector<u8> &data)
{
    const std::size_t expected = static_cast<std::size_t>(m_cart.ramBanks) * 0x2000;
    m_cart.ram.assign(expected, 0);
    if (!data.empty()) {
        const std::size_t count = data.size() < expected ? data.size() : expected;
        std::memcpy(m_cart.ram.data(), data.data(), count);
    }
}

void Memory::setBootRomEnabled(bool enabled)
{
    m_bootRomEnabled = enabled;
}

bool Memory::isBootRomEnabled() const
{
    return m_bootRomEnabled;
}

void Memory::reset()
{
    m_vram.fill(0);
    m_wram.fill(0);
    m_oam.fill(0);
    m_io.fill(0);
    m_hram.fill(0);
    m_timer = TimerRegisters{};
    m_serial = SerialRegisters{};
    m_serialOutput.clear();
    m_joypadSelect = 0x30;
    m_joypadButtons = 0xFF;
    m_romBank = 1;
    m_ramBank = 0;
    m_ramEnabled = false;
    m_bankingMode = false;
    if (m_interrupts != nullptr) {
        m_interrupts->reset();
    }
}

u8 Memory::read(u16 address)
{
    if (address < 0x8000) {
        return readRom(address);
    }
    if (address < 0xA000) {
        return m_vram[address - 0x8000];
    }
    if (address < 0xC000) {
        return readCartRam(address);
    }
    if (address < 0xE000) {
        return m_wram[address - 0xC000];
    }
    if (address < 0xFE00) {
        return m_wram[address - 0xE000];
    }
    if (address < 0xFEA0) {
        return m_oam[address - 0xFE00];
    }
    if (address < 0xFF00) {
        return 0xFF;
    }
    if (address < 0xFF80) {
        return readIo(address);
    }
    if (address < 0xFFFF) {
        return m_hram[address - 0xFF80];
    }
    if (m_interrupts != nullptr) {
        return m_interrupts->enableRegister();
    }
    return m_ie;
}

void Memory::write(u16 address, u8 value)
{
    if (address < 0x8000) {
        writeRomControl(address, value);
        return;
    }
    if (address < 0xA000) {
        m_vram[address - 0x8000] = value;
        return;
    }
    if (address < 0xC000) {
        writeCartRam(address, value);
        return;
    }
    if (address < 0xE000) {
        m_wram[address - 0xC000] = value;
        return;
    }
    if (address < 0xFE00) {
        m_wram[address - 0xE000] = value;
        return;
    }
    if (address < 0xFEA0) {
        m_oam[address - 0xFE00] = value;
        return;
    }
    if (address < 0xFF00) {
        return; // Unused area, writes are ignored.
    }
    if (address < 0xFF80) {
        writeIo(address, value);
        return;
    }
    if (address < 0xFFFF) {
        m_hram[address - 0xFF80] = value;
        return;
    }
    if (m_interrupts != nullptr) {
        m_interrupts->setEnableRegister(value);
    }
    m_ie = value;
}

u8 Memory::peek(u16 address) const
{
    if (address < 0x8000) {
        return readRom(address);
    }
    if (address < 0xA000) {
        return m_vram[address - 0x8000];
    }
    if (address < 0xC000) {
        return readCartRam(address);
    }
    if (address < 0xE000) {
        return m_wram[address - 0xC000];
    }
    if (address < 0xFE00) {
        return m_wram[address - 0xE000];
    }
    if (address < 0xFEA0) {
        return m_oam[address - 0xFE00];
    }
    if (address < 0xFF00) {
        return 0xFF;
    }
    if (address < 0xFF80) {
        return m_io[address - 0xFF00];
    }
    if (address < 0xFFFF) {
        return m_hram[address - 0xFF80];
    }
    if (m_interrupts != nullptr) {
        return m_interrupts->enableRegister();
    }
    return m_ie;
}

u16 Memory::readWord(u16 address)
{
    return makeWord(read(static_cast<u16>(address + 1)), read(address));
}

void Memory::writeWord(u16 address, u16 value)
{
    write(address, lowByte(value));
    write(static_cast<u16>(address + 1), highByte(value));
}

void Memory::tick(int cycles)
{
    if (cycles <= 0) {
        return;
    }
    tickDivider(cycles);
    tickSerial(cycles);
}

u8 Memory::joypadState() const
{
    u8 direction = 0x0F;
    u8 buttons = 0x0F;
    if ((m_joypadSelect & 0x20) == 0) {
        // P15 selected: return button bits (A, B, Select, Start)
        buttons = static_cast<u8>((m_joypadButtons >> 4) & 0x0F);
    }
    if ((m_joypadSelect & 0x10) == 0) {
        // P14 selected: return direction bits (Right, Left, Up, Down)
        direction = static_cast<u8>(m_joypadButtons & 0x0F);
    }
    return static_cast<u8>(m_joypadSelect | (direction & buttons));
}

void Memory::setButton(int index, bool pressed)
{
    if (index < 0 || index > 7) {
        return;
    }
    const u8 mask = static_cast<u8>(1u << index);
    m_joypadButtons = pressed ? static_cast<u8>(m_joypadButtons & ~mask)
                              : static_cast<u8>(m_joypadButtons | mask);
    if (pressed && m_interrupts != nullptr) {
        m_interrupts->request(InterruptType::Joypad);
    }
}

const Memory::TimerRegisters &Memory::timer() const
{
    return m_timer;
}

const Memory::SerialRegisters &Memory::serial() const
{
    return m_serial;
}

const std::string &Memory::serialOutput() const
{
    return m_serialOutput;
}

void Memory::clearSerialOutput()
{
    m_serialOutput.clear();
}

const u8 *Memory::videoRam() const
{
    return m_vram.data();
}

u8 *Memory::videoRamBuffer()
{
    return m_vram.data();
}

const u8 *Memory::highRam() const
{
    return m_hram.data();
}

const u8 *Memory::workRam() const
{
    return m_wram.data();
}

Memory::MemoryState Memory::memoryState() const
{
    MemoryState state;
    state.vram = m_vram;
    state.wram = m_wram;
    state.oam = m_oam;
    state.io = m_io;
    state.hram = m_hram;
    state.timer = m_timer;
    state.serial = m_serial;
    state.cartRam = m_cart.ram;
    state.joypadSelect = m_joypadSelect;
    state.joypadButtons = m_joypadButtons;
    state.romBank = m_romBank;
    state.ramBank = m_ramBank;
    state.ramEnabled = m_ramEnabled;
    state.bankingMode = m_bankingMode;
    state.bootRomEnabled = m_bootRomEnabled;
    state.ie = m_ie;
    return state;
}

void Memory::restoreMemoryState(const Memory::MemoryState &state)
{
    m_vram = state.vram;
    m_wram = state.wram;
    m_oam = state.oam;
    m_io = state.io;
    m_hram = state.hram;
    m_timer = state.timer;
    m_serial = state.serial;
    m_cart.ram = state.cartRam;
    m_joypadSelect = state.joypadSelect;
    m_joypadButtons = state.joypadButtons;
    m_romBank = state.romBank;
    m_ramBank = state.ramBank;
    m_ramEnabled = state.ramEnabled;
    m_bankingMode = state.bankingMode;
    m_bootRomEnabled = state.bootRomEnabled;
    m_ie = state.ie;
}

bool Memory::decodeHeader(std::string &error)
{
    if (m_cart.rom.size() < kMinRomSize) {
        error = "ROM image is too small to contain a cartridge header";
        return false;
    }
    if (m_cart.rom[0x143] == 0xC0) {
        error = "Game Boy Color (CGB) cartridges are not supported";
        return false;
    }
    switch (m_cart.rom[0x147]) {
    case 0x00:
        m_cart.mapper = MapperType::None;
        break;
    case 0x01:
    case 0x02:
    case 0x03:
        m_cart.mapper = MapperType::MBC1;
        m_cart.hasBattery = (m_cart.rom[0x147] == 0x03);
        break;
    case 0x05:
    case 0x06:
        m_cart.mapper = MapperType::MBC2;
        m_cart.hasBattery = (m_cart.rom[0x147] == 0x06);
        break;
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        m_cart.mapper = MapperType::MBC3;
        m_cart.hasRtc = (m_cart.rom[0x147] == 0x0F || m_cart.rom[0x147] == 0x10);
        m_cart.hasBattery = (m_cart.rom[0x147] == 0x0F || m_cart.rom[0x147] == 0x10
                             || m_cart.rom[0x147] == 0x13);
        break;
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
        m_cart.mapper = MapperType::MBC5;
        m_cart.hasBattery = (m_cart.rom[0x147] == 0x1A || m_cart.rom[0x147] == 0x1B
                             || m_cart.rom[0x147] == 0x1D || m_cart.rom[0x147] == 0x1E);
        break;
    default:
        error = "Unsupported cartridge type (0x" + util::toHex(m_cart.rom[0x147]) + ")";
        return false;
    }
    m_cart.title.clear();
    for (std::size_t i = 0x134; i <= 0x143; ++i) {
        const char ch = static_cast<char>(m_cart.rom[i]);
        if (ch == '\0') {
            break;
        }
        m_cart.title.push_back(ch);
    }
    m_cart.romBanks = romBanksFromCode(m_cart.rom[0x148]);
    m_cart.ramBanks = ramBanksFromCode(m_cart.rom[0x149]);
    if (m_cart.mapper == MapperType::MBC2) {
        m_cart.ramBanks = 1;
    }
    return true;
}

u8 Memory::readRom(u16 address) const
{
    // While the boot ROM is mapped it replaces the first 256 bytes of the
    // cartridge address space; the cartridge header is still visible above it.
    if (m_bootRomEnabled && address < kBootRomSize) {
        return m_bootRom[address];
    }
    int bank = 0;
    if (address >= 0x4000) {
        switch (m_cart.mapper) {
        case MapperType::MBC1:
            bank = m_bankingMode ? ((m_ramBank & 0x03) << 5) | (m_romBank & 0x1F)
                                 : (m_romBank & 0x1F);
            if (bank == 0) {
                bank = 1;
            }
            break;
        case MapperType::MBC2:
            bank = m_romBank & 0x0F;
            if (bank == 0) {
                bank = 1;
            }
            break;
        case MapperType::MBC3:
            bank = m_romBank & 0x7F;
            if (bank == 0) {
                bank = 1;
            }
            break;
        case MapperType::MBC5:
            bank = (m_bankingMode ? 0x100 : 0) | m_romBank;
            if (bank == 0) {
                bank = 1;
            }
            break;
        case MapperType::None:
        case MapperType::Unknown:
        default:
            break;
        }
    } else if (m_cart.mapper == MapperType::MBC1 && m_bankingMode && m_cart.romBanks > 32) {
        bank = (m_ramBank & 0x03) << 5;
    }
    if (bank != 0 && m_cart.romBanks > 1) {
        bank &= m_cart.romBanks - 1;
    }
    const std::size_t offset = static_cast<std::size_t>(bank * 0x4000) + (address & 0x3FFF);
    if (offset >= m_cart.rom.size()) {
        return 0xFF;
    }
    return m_cart.rom[offset];
}

u8 Memory::readCartRam(u16 address) const
{
    if (!m_ramEnabled) {
        return 0xFF;
    }
    if (m_cart.mapper == MapperType::MBC3 && m_ramBank >= 0x08 && m_ramBank <= 0x0C) {
        return m_io[0x58 + (m_ramBank - 0x08)];
    }
    if (m_cart.mapper == MapperType::MBC2) {
        const std::size_t index = (address - 0xA000) >> 1;
        if (index >= m_cart.ram.size()) {
            return 0xFF;
        }
        const u8 nibble = static_cast<u8>(m_cart.ram[index] & 0x0F);
        return static_cast<u8>(nibble | 0xF0);
    }
    const std::size_t offset = ramBankOffset() + (address - 0xA000);
    if (offset >= m_cart.ram.size()) {
        return 0xFF;
    }
    return m_cart.ram[offset];
}

void Memory::writeRomControl(u16 address, u8 value)
{
    if (m_cart.rom.empty()) {
        return;
    }
    switch (m_cart.mapper) {
    case MapperType::None:
        break;
    case MapperType::MBC1:
        if (address < 0x2000) {
            m_ramEnabled = ((value & 0x0F) == 0x0A);
        } else if (address < 0x4000) {
            m_romBank = static_cast<u8>((m_romBank & 0xE0) | (value & 0x1F));
        } else if (address < 0x6000) {
            m_ramBank = value & 0x03;
        } else if (address < 0x8000) {
            m_bankingMode = (value & 0x01) != 0;
        }
        break;
    case MapperType::MBC2:
        if (address < 0x2000) {
            if ((address & 0x0100) == 0) {
                m_ramEnabled = ((value & 0x0F) == 0x0A);
            }
        } else if (address < 0x4000) {
            if ((address & 0x0100) != 0) {
                m_romBank = static_cast<u8>(value & 0x0F);
            }
        }
        break;
    case MapperType::MBC3:
        if (address < 0x2000) {
            m_ramEnabled = ((value & 0x0F) == 0x0A);
        } else if (address < 0x4000) {
            m_romBank = value;
        } else if (address < 0x6000) {
            m_ramBank = value & 0x0F;
        } else if (address < 0x8000) {
            // Real time clock latch: a write of 0x01 after 0x00 snapshots the
            // current time into the RTC scratch registers held in the unused
            // I/O slots 0xFF58-0xFF5C (0x5D remembers the last latch write).
            if (m_cart.hasRtc) {
                if (value == 0x01 && m_io[0x5D] != 0x01) {
                    const std::time_t now = std::time(nullptr);
                    std::tm local{};
#if defined(_WIN32)
                    localtime_s(&local, &now);
#else
                    localtime_r(&now, &local);
#endif
                    const u32 days = static_cast<u32>(now / 86400);
                    m_io[0x58] = static_cast<u8>(local.tm_sec);
                    m_io[0x59] = static_cast<u8>(local.tm_min);
                    m_io[0x5A] = static_cast<u8>(local.tm_hour);
                    m_io[0x5B] = static_cast<u8>(days & 0xFF);
                    const u8 keep = static_cast<u8>(m_io[0x5C] & 0xC0);
                    m_io[0x5C] = static_cast<u8>(keep | ((days >> 8) & 0x01)
                                                 | (days > 511 ? 0x80 : 0x00));
                }
                m_io[0x5D] = value;
            }
        }
        break;
    case MapperType::MBC5:
        if (address < 0x2000) {
            m_ramEnabled = ((value & 0x0F) == 0x0A);
        } else if (address < 0x3000) {
            m_romBank = value;
        } else if (address < 0x4000) {
            m_bankingMode = (value & 0x01) != 0;
        } else if (address < 0x6000) {
            m_ramBank = value & 0x0F;
        }
        break;
    case MapperType::Unknown:
    default:
        break;
    }
}

void Memory::writeCartRam(u16 address, u8 value)
{
    if (!m_ramEnabled) {
        return;
    }
    if (m_cart.mapper == MapperType::MBC3 && m_ramBank >= 0x08 && m_ramBank <= 0x0C) {
        m_io[0x58 + (m_ramBank - 0x08)] = value;
        return;
    }
    if (m_cart.mapper == MapperType::MBC2) {
        const std::size_t index = (address - 0xA000) >> 1;
        if (index < m_cart.ram.size()) {
            m_cart.ram[index] = value & 0x0F;
        }
        return;
    }
    const std::size_t offset = ramBankOffset() + (address - 0xA000);
    if (offset < m_cart.ram.size()) {
        m_cart.ram[offset] = value;
    }
}

u8 Memory::readIo(u16 address)
{
    switch (address) {
    case 0xFF00:
        return joypadState();
    case 0xFF01:
        return m_serial.sb;
    case 0xFF02:
        return static_cast<u8>(m_serial.sc | 0x7E);
    case 0xFF04:
        return highByte(m_timer.divider);
    case 0xFF05:
        return m_timer.tima;
    case 0xFF06:
        return m_timer.tma;
    case 0xFF07:
        return static_cast<u8>(m_timer.tac | 0xF8);
    case 0xFF0F:
        return m_interrupts != nullptr ? static_cast<u8>(m_interrupts->flags() | 0xE0) : 0xFF;
    default:
        if (m_gpu != nullptr
            && ((address >= 0xFF40 && address <= 0xFF4B)
                || (address >= 0xFF68 && address <= 0xFF6B))) {
            return m_gpu->readRegister(address);
        }
        return m_io[address - 0xFF00];
    }
}

void Memory::writeIo(u16 address, u8 value)
{
    switch (address) {
    case 0xFF00:
        m_joypadSelect = value & 0x30;
        break;
    case 0xFF01:
        m_serial.sb = value;
        break;
    case 0xFF02:
        m_serial.sc = static_cast<u8>(value & 0x81);
        if ((value & 0x81) == 0x81) {
            // Internal clock transfer of eight bits at 8192 Hz.
            m_serial.bitTimer = 8 * (kCyclesPerSecond / 8192);
            m_serial.outgoing = m_serial.sb;
            m_serial.incoming = 0xFF;
        }
        break;
    case 0xFF04:
        m_timer.divider = 0;
        break;
    case 0xFF05:
        m_timer.tima = value;
        m_timer.overflow = false;
        break;
    case 0xFF06:
        m_timer.tma = value;
        break;
    case 0xFF07:
        m_timer.tac = value & 0x07;
        break;
    case 0xFF0F:
        if (m_interrupts != nullptr) {
            m_interrupts->setFlags(static_cast<u8>(value | 0xE0));
        }
        break;
    case 0xFF50:
        // Writing a non-zero value unmaps the boot ROM for good; the real
        // hardware ignores the value written otherwise.
        if (value != 0) {
            m_bootRomEnabled = false;
        }
        break;
    default:
        if (m_gpu != nullptr
            && ((address >= 0xFF40 && address <= 0xFF4B)
                || (address >= 0xFF68 && address <= 0xFF6B))) {
            m_gpu->writeRegister(address, value);
        } else {
            m_io[address - 0xFF00] = value;
        }
        break;
    }
}

void Memory::tickDivider(int cycles)
{
    const u16 oldDivider = m_timer.divider;
    m_timer.divider = static_cast<u16>(m_timer.divider + static_cast<u16>(cycles));
    if (timerEnabled()) {
        const int bit = timerBit();
        const bool oldState = ((oldDivider >> bit) & 0x01u) != 0;
        const bool newState = ((m_timer.divider >> bit) & 0x01u) != 0;
        if (oldState && !newState) {
            tickTimer();
        }
    }
    applyTimerOverflow();
}

void Memory::tickTimer()
{
    m_timer.tima = static_cast<u8>(m_timer.tima + 1);
    if (m_timer.tima == 0) {
        m_timer.overflow = true;
    }
}

void Memory::tickSerial(int cycles)
{
    if ((m_serial.sc & 0x80) == 0 || m_serial.bitTimer <= 0) {
        return;
    }
    m_serial.bitTimer -= cycles;
    if (m_serial.bitTimer > 0) {
        return;
    }
    m_serial.sc = static_cast<u8>(m_serial.sc & ~0x80);
    m_serial.bitTimer = 0;
    m_serial.sb = m_serial.incoming;
    if (m_serial.incoming >= 0x20 && m_serial.incoming < 0x7F) {
        m_serialOutput.push_back(static_cast<char>(m_serial.incoming));
    }
    if (m_interrupts != nullptr) {
        m_interrupts->request(InterruptType::Serial);
    }
}

void Memory::applyTimerOverflow()
{
    if (!m_timer.overflow) {
        return;
    }
    m_timer.overflow = false;
    m_timer.tima = m_timer.tma;
    if (m_interrupts != nullptr) {
        m_interrupts->request(InterruptType::Timer);
    }
}

int Memory::timerBit() const
{
    switch (m_timer.tac & 0x03) {
    case 0: return 9;  /**< 4096 Hz. */
    case 1: return 3;  /**< 262144 Hz. */
    case 2: return 5;  /**< 65536 Hz. */
    default: return 7; /**< 16384 Hz. */
    }
}

bool Memory::timerEnabled() const
{
    return (m_timer.tac & 0x04) != 0;
}

std::size_t Memory::ramBankOffset() const
{
    switch (m_cart.mapper) {
    case MapperType::MBC1:
        return static_cast<std::size_t>(m_bankingMode ? (m_ramBank & 0x03) : 0) * 0x2000;
    case MapperType::MBC3:
        return static_cast<std::size_t>(m_ramBank <= 3 ? m_ramBank : 0) * 0x2000;
    case MapperType::MBC5:
        return static_cast<std::size_t>(m_ramBank & 0x0F) * 0x2000;
    case MapperType::None:
    case MapperType::MBC2:
    case MapperType::Unknown:
    default:
        return 0;
    }
}

} // namespace gb
