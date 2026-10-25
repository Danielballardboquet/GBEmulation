/**
 * @file Memory.h
 * @brief Memory bus, cartridge (MBC) and memory mapped I/O of the Game Boy.
 *
 * The Memory class owns every byte the CPU can address: the boot ROM, the
 * cartridge ROM/RAM (with mapper), the video RAM, the work RAM, the echo RAM,
 * the object attribute memory, the unused area, the I/O registers, the high RAM
 * and the interrupt enable register.
 */

#ifndef GB_MEMORY_H
#define GB_MEMORY_H

#include "emulation/Types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace gb {

class GPU;
class InterruptController;

/**
 * @brief Cartridge mapper types supported by the emulator.
 */
enum class MapperType : u8 {
    None = 0x00,   /**< No mapper: 32 KiB ROM, optional 8 KiB RAM. */
    MBC1 = 0x01,   /**< MBC1: up to 2 MiB ROM and 32 KiB RAM. */
    MBC2 = 0x02,   /**< MBC2: up to 256 KiB ROM and 512x4 bits of RAM. */
    MBC3 = 0x03,   /**< MBC3: up to 2 MiB ROM, 32 KiB RAM and real time clock. */
    MBC5 = 0x05,   /**< MBC5: up to 8 MiB ROM and 128 KiB RAM. */
    Unknown = 0xFF /**< Unsupported mapper, the cartridge is rejected. */
};

/**
 * @brief Parsed contents and metadata of a Game Boy cartridge image.
 */
struct Cartridge {
    std::vector<u8> rom;              /**< Raw ROM image. */
    std::vector<u8> ram;              /**< Battery backed RAM image (may be empty). */
    MapperType mapper = MapperType::None; /**< Mapper implemented by the cartridge. */
    std::string title;                /**< Title stored in the header (0x0134-0x0143). */
    bool hasBattery = false;          /**< True when the cartridge has battery backed RAM. */
    bool hasRtc = false;              /**< True when the cartridge has a real time clock. */
    int romBanks = 2;                 /**< Number of 16 KiB ROM banks. */
    int ramBanks = 0;                 /**< Number of 8 KiB RAM banks. */
};

/**
 * @brief The 16-bit memory bus of the Game Boy.
 *
 * Reads and writes are dispatched to the component that owns the addressed
 * byte. The bus also exposes the I/O registers used by the timer, the joypad
 * and the serial port, and forwards the LCD registers to the GPU.
 */
class Memory {
public:
    /**
     * @brief Registers of the timer (DIV and TIMA/TMA/TAC).
     */
    struct TimerRegisters {
        u16 divider = 0xABCC;  /**< Internal 16-bit divider (upper byte is DIV). */
        u8 tima = 0;           /**< Timer counter (0xFF05). */
        u8 tma = 0;            /**< Timer modulo (0xFF06). */
        u8 tac = 0;            /**< Timer control (0xFF07). */
        bool overflow = false; /**< Pending TIMA overflow, applied after one cycle. */
    };

    /**
     * @brief Registers of the serial port (link cable).
     */
    struct SerialRegisters {
        u8 sb = 0;            /**< Serial transfer data (0xFF01). */
        u8 sc = 0;            /**< Serial transfer control (0xFF02). */
        int bitTimer = 0;     /**< Bits left in the running transfer. */
        u8 outgoing = 0;      /**< Byte currently being shifted out. */
        u8 incoming = 0;      /**< Byte currently being shifted in. */
    };

    /**
     * @brief Constructs an empty bus with no cartridge inserted.
     */
    Memory();

    /**
     * @brief Destroys the bus and releases the cartridge.
     */
    ~Memory();

    Memory(const Memory &) = delete;
    Memory &operator=(const Memory &) = delete;

    /**
     * @brief Attaches the GPU so LCD registers can be forwarded to it.
     * @param gpu Pointer to the GPU owned by the emulator.
     */
    void setGPU(GPU *gpu);

    /**
     * @brief Attaches the interrupt controller used by the I/O registers.
     * @param interrupts Pointer to the interrupt controller.
     */
    void setInterrupts(InterruptController *interrupts);

    /**
     * @brief Loads a cartridge image into the bus.
     * @param data Raw bytes of the ROM file.
     * @param error Receives a human readable error when the image is invalid.
     * @return True when the cartridge was accepted.
     */
    bool loadCartridge(const std::vector<u8> &data, std::string &error);

    /**
     * @brief Removes the inserted cartridge and resets the mapper state.
     */
    void unloadCartridge();

    /**
     * @brief Tells whether a cartridge is currently inserted.
     * @return True when a cartridge is loaded.
     */
    bool hasCartridge() const;

    /**
     * @brief Returns the metadata of the inserted cartridge.
     * @return Constant reference to the cartridge descriptor.
     */
    const Cartridge &cartridge() const;

    /**
     * @brief Returns the raw bytes of a save file (battery backed RAM).
     * @return The cartridge RAM contents.
     */
    const std::vector<u8> &saveData() const;

    /**
     * @brief Restores the battery backed RAM from a save file.
     * @param data Bytes previously returned by saveData().
     */
    void loadSaveData(const std::vector<u8> &data);

    /**
     * @brief Enables or disables the boot ROM mapping at 0x0000-0x00FF.
     * @param enabled True to map the boot ROM.
     */
    void setBootRomEnabled(bool enabled);

    /**
     * @brief Tells whether the boot ROM is currently mapped.
     * @return True when the boot ROM is mapped.
     */
    bool isBootRomEnabled() const;

    /**
     * @brief Resets the RAM contents and the I/O registers to post-boot values.
     */
    void reset();

    /**
     * @brief Reads a byte from the address bus.
     * @param address The 16-bit address to read.
     * @return The value stored at that address.
     */
    u8 read(u16 address);

    /**
     * @brief Writes a byte to the address bus.
     * @param address The 16-bit address to write.
     * @param value The value to store.
     */
    void write(u16 address, u8 value);

    /**
     * @brief Reads a byte without triggering any side effect.
     * @param address The address to peek.
     * @return The value stored at that address.
     */
    u8 peek(u16 address) const;

    /**
     * @brief Reads a little endian 16-bit value from the bus.
     * @param address Address of the low byte.
     * @return The 16-bit value.
     */
    u16 readWord(u16 address);

    /**
     * @brief Writes a little endian 16-bit value to the bus.
     * @param address Address of the low byte.
     * @param value The value to store.
     */
    void writeWord(u16 address, u16 value);

    /**
     * @brief Advances the timer, the divider and the serial port by a number of cycles.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tick(int cycles);

    /**
     * @brief Reads the current state of the joypad matrix.
     * @return The nibble with the pressed buttons (0 = pressed).
     */
    u8 joypadState() const;

    /**
     * @brief Presses or releases a button of the joypad.
     * @param index Button index (0 = right, 1 = left, 2 = up, 3 = down,
     *              4 = A, 5 = B, 6 = select, 7 = start).
     * @param pressed True when the button is held down.
     */
    void setButton(int index, bool pressed);

    /**
     * @brief Returns the timer registers for the debugger.
     * @return Constant reference to the timer registers.
     */
    const TimerRegisters &timer() const;

    /**
     * @brief Returns the serial registers for the debugger.
     * @return Constant reference to the serial registers.
     */
    const SerialRegisters &serial() const;

    /**
     * @brief Returns the bytes printed through the serial port (Blargg test output).
     * @return The text received on the link cable.
     */
    const std::string &serialOutput() const;

    /**
     * @brief Clears the captured serial output.
     */
    void clearSerialOutput();

    /**
     * @brief Returns a pointer to the video RAM.
     * @return Pointer to the 8 KiB of VRAM.
     */
    const u8 *videoRam() const;

    /**
     * @brief Returns a writable pointer to the video RAM.
     * @return Pointer to the 8 KiB VRAM array.
     */
    u8 *videoRamBuffer();

    /**
     * @brief Returns a pointer to the high RAM area.
     * @return Pointer to the 127 bytes of HRAM.
     */
    const u8 *highRam() const;

    /**
     * @brief Returns a pointer to the work RAM.
     * @return Pointer to the 8 KiB of WRAM.
     */
    const u8 *workRam() const;

private:
    /**
     * @brief Decodes the cartridge header and selects the mapper.
     * @param error Receives an error message when decoding fails.
     * @return True when the header is valid.
     */
    bool decodeHeader(std::string &error);

    /**
     * @brief Reads from the cartridge ROM area (0x0000-0x7FFF).
     * @param address The address to read.
     * @return The ROM byte.
     */
    u8 readRom(u16 address) const;

    /**
     * @brief Reads from the cartridge RAM area (0xA000-0xBFFF).
     * @param address The address to read.
     * @return The RAM byte, 0xFF when RAM is disabled.
     */
    u8 readCartRam(u16 address) const;

    /**
     * @brief Writes to the cartridge control area (0x0000-0x7FFF).
     * @param address The address written.
     * @param value The value written.
     */
    void writeRomControl(u16 address, u8 value);

    /**
     * @brief Writes to the cartridge RAM area (0xA000-0xBFFF).
     * @param address The address written.
     * @param value The value written.
     */
    void writeCartRam(u16 address, u8 value);

    /**
     * @brief Reads one of the I/O registers.
     * @param address The address inside 0xFF00-0xFF7F.
     * @return The register value.
     */
    u8 readIo(u16 address);

    /**
     * @brief Writes one of the I/O registers.
     * @param address The address inside 0xFF00-0xFF7F.
     * @param value The value written.
     */
    void writeIo(u16 address, u8 value);

    /**
     * @brief Advances the internal divider register.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tickDivider(int cycles);

    /**
     * @brief Increments TIMA when the selected divider bit falls.
     */
    void tickTimer();

    /**
     * @brief Advances the serial transfer state machine.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tickSerial(int cycles);

    /**
     * @brief Applies a pending TIMA overflow.
     */
    void applyTimerOverflow();

    /**
     * @brief Returns the bit of the divider that clocks TIMA.
     * @return The bit index selected by TAC.
     */
    int timerBit() const;

    /**
     * @brief Tells whether the timer is enabled.
     * @return True when bit 2 of TAC is set.
     */
    bool timerEnabled() const;

    /**
     * @brief Returns the address of the current RAM bank.
     * @return Byte offset inside the cartridge RAM.
     */
    std::size_t ramBankOffset() const;

    std::array<u8, kBootRomSize> m_bootRom;       /**< Internal DMG boot ROM. */
    bool m_bootRomEnabled = true;                 /**< Boot ROM mapping flag. */
    Cartridge m_cart;                             /**< Inserted cartridge. */
    std::array<u8, kVideoRamSize> m_vram;         /**< Video RAM. */
    std::array<u8, kWorkRamSize> m_wram;          /**< Work RAM. */
    std::array<u8, 0xA0> m_oam;                   /**< Object attribute memory. */
    std::array<u8, 0x80> m_io;                    /**< I/O registers 0xFF00-0xFF7F. */
    std::array<u8, kHighRamSize> m_hram;          /**< High RAM 0xFF80-0xFFFE. */
    u8 m_ie = 0;                                  /**< Interrupt enable register. */
    TimerRegisters m_timer;                       /**< Timer state. */
    SerialRegisters m_serial;                     /**< Serial port state. */
    std::string m_serialOutput;                   /**< Captured serial output. */
    u8 m_joypadSelect = 0x30;                     /**< Joypad selection bits. */
    u8 m_joypadButtons = 0xFF;                    /**< Pressed buttons (0 = pressed). */
    u8 m_romBank = 1;                             /**< Currently selected ROM bank. */
    u8 m_ramBank = 0;                             /**< Currently selected RAM bank. */
    bool m_ramEnabled = false;                    /**< Cartridge RAM enable flag. */
    bool m_bankingMode = false;                   /**< MBC1 advanced banking mode. */
    GPU *m_gpu = nullptr;                         /**< Attached GPU. */
    InterruptController *m_interrupts = nullptr;  /**< Attached interrupt controller. */
};

} // namespace gb

#endif // GB_MEMORY_H
