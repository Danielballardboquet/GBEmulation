/**
 * @file Types.h
 * @brief Fundamental integer types, constants and helper macros shared by every
 *        component of the Game Boy emulation core.
 *
 * The emulation core is deliberately free of any Qt or platform dependency so it
 * can be unit-tested, embedded or driven by any front-end.
 */

#ifndef GB_TYPES_H
#define GB_TYPES_H

#include <cstddef>
#include <cstdint>

namespace gb {

/** @brief Unsigned 8-bit integer used for registers, bytes and flags. */
using u8 = std::uint8_t;
/** @brief Unsigned 16-bit integer used for register pairs and addresses. */
using u16 = std::uint16_t;
/** @brief Unsigned 32-bit integer used for counters and timers. */
using u32 = std::uint32_t;

/**
 * @brief Colour source used to render the four LCD shades.
 *
 * DMG games are drawn in monochrome; the choice of the four colours is purely
 * cosmetic. The emulator maps the two LCD shade bits onto one of these sets.
 */
enum class DisplayPalette : u8 {
    Dmg,       /**< Original green LCD palette of the 1989 Game Boy. */
    Gbc,       /**< Default palette of the Game Boy Color. */
    Custom     /**< Four colours chosen by the user. */
};
/** @brief Signed 8-bit integer used for relative jumps. */
using s8 = std::int8_t;
/** @brief Signed 16-bit integer used for relative jumps. */
using s16 = std::int16_t;

/** @brief Number of bytes in the 16-bit address space of the Game Boy. */
constexpr std::size_t kAddressSpaceSize = 0x10000;
/** @brief Width of the LCD in pixels. */
constexpr int kScreenWidth = 160;
/** @brief Height of the LCD in pixels. */
constexpr int kScreenHeight = 144;
/** @brief Number of pixels of a single frame. */
constexpr std::size_t kScreenPixels = static_cast<std::size_t>(kScreenWidth * kScreenHeight);
/** @brief Number of bytes of a single frame in RGBA8888 format. */
constexpr std::size_t kFrameBufferBytes = kScreenPixels * 4;
/** @brief Size of one tile in bytes (8x8 pixels, 2 bits per pixel). */
constexpr std::size_t kTileBytes = 16;
/** @brief Number of tiles in a tile map / tile data area. */
constexpr std::size_t kTileCount = 384;
/** @brief Size of one tile map in bytes. */
constexpr std::size_t kTileMapBytes = 32 * 32;
/** @brief Number of sprites the PPU can handle per line. */
constexpr int kMaxSpritesPerLine = 10;
/** @brief Number of entries in the sprite attribute table. */
constexpr int kSpriteCount = 40;
/** @brief Number of entries in the background palette. */
constexpr int kBackgroundPaletteSize = 32;
/** @brief Number of entries in the sprite palette. */
constexpr int kSpritePaletteSize = 32;
/** @brief Size of the wave pattern RAM in bytes. */
constexpr std::size_t kWaveRamSize = 16;
/** @brief Size of the high RAM area (0xFF80-0xFFFE). */
constexpr std::size_t kHighRamSize = 0x7F;
/** @brief Size of the work RAM in bytes (8 KiB). */
constexpr std::size_t kWorkRamSize = 0x2000;
/** @brief Size of the video RAM in bytes (8 KiB). */
constexpr std::size_t kVideoRamSize = 0x2000;
/** @brief Size of the cartridge RAM in bytes (32 KiB). */
constexpr std::size_t kCartRamSize = 0x8000;
/** @brief Size of the boot ROM of a DMG. */
constexpr std::size_t kBootRomSize = 0x100;
/** @brief Number of dots (clock ticks) in one scanline. */
constexpr int kDotsPerLine = 456;
/** @brief Number of scanlines of a frame including the vertical blank period. */
constexpr int kLinesPerFrame = 154;
/** @brief Number of CPU cycles (T-states) of a frame at 59.7 Hz. */
constexpr int kCyclesPerFrame = kDotsPerLine * kLinesPerFrame;
/** @brief Number of CPU cycles executed per second (4.194304 MHz). */
constexpr int kCyclesPerSecond = 4194304;
/** @brief Number of frames rendered per second. */
constexpr double kFramesPerSecond = 59.7275;
/** @brief Number of samples per second produced by the audio unit. */
constexpr int kSampleRate = 48000;

/**
 * @brief Combines two bytes into a 16-bit value.
 * @param high The most significant byte.
 * @param low The least significant byte.
 * @return The combined 16-bit value.
 */
constexpr u16 makeWord(u8 high, u8 low) noexcept
{
    return static_cast<u16>((static_cast<u16>(high) << 8) | low);
}

/**
 * @brief Extracts the most significant byte of a 16-bit value.
 * @param value The value to split.
 * @return The high byte.
 */
constexpr u8 highByte(u16 value) noexcept
{
    return static_cast<u8>(value >> 8);
}

/**
 * @brief Extracts the least significant byte of a 16-bit value.
 * @param value The value to split.
 * @return The low byte.
 */
constexpr u8 lowByte(u16 value) noexcept
{
    return static_cast<u8>(value & 0xFF);
}

/**
 * @brief Tests whether a bit is set inside a value.
 * @param value The value to test.
 * @param bit Index of the bit (0..7).
 * @return True when the bit is set.
 */
constexpr bool isBitSet(u8 value, int bit) noexcept
{
    return ((value >> bit) & 0x01u) != 0;
}

/**
 * @brief Sets or clears a single bit of a value.
 * @param value The value to modify.
 * @param bit Index of the bit (0..7).
 * @param set True to set the bit, false to clear it.
 * @return The modified value.
 */
constexpr u8 setBit(u8 value, int bit, bool set = true) noexcept
{
    const u8 mask = static_cast<u8>(1u << bit);
    return set ? static_cast<u8>(value | mask) : static_cast<u8>(value & ~mask);
}

} // namespace gb

#endif // GB_TYPES_H
