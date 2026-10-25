/**
 * @file Utilities.h
 * @brief Small helper routines shared by the emulation core: bit manipulation,
 *        binary coded decimal arithmetic, cartridge header checksums and the
 *        internal DMG boot ROM.
 */

#ifndef GB_UTILITIES_H
#define GB_UTILITIES_H

#include "emulation/Types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace gb {
namespace util {

/**
 * @brief Rotates a byte left through the carry flag.
 * @param value The byte to rotate.
 * @param carry The incoming carry flag.
 * @param carryOut Receives the bit rotated out of the byte.
 * @return The rotated byte.
 */
u8 rotateLeft(u8 value, bool carry, bool &carryOut);

/**
 * @brief Rotates a byte right through the carry flag.
 * @param value The byte to rotate.
 * @param carry The incoming carry flag.
 * @param carryOut Receives the bit rotated out of the byte.
 * @return The rotated byte.
 */
u8 rotateRight(u8 value, bool carry, bool &carryOut);

/**
 * @brief Adds two bytes and the carry flag.
 * @param a First operand.
 * @param b Second operand.
 * @param carry Incoming carry flag.
 * @param carryOut Receives the resulting carry flag.
 * @param halfCarryOut Receives the resulting half carry flag.
 * @return The 8-bit result.
 */
u8 addBytes(u8 a, u8 b, bool carry, bool &carryOut, bool &halfCarryOut);

/**
 * @brief Subtracts a byte and the carry flag from another byte.
 * @param a Minuend.
 * @param b Subtrahend.
 * @param carry Incoming carry flag (borrow).
 * @param carryOut Receives the resulting borrow flag.
 * @param halfCarryOut Receives the resulting half borrow flag.
 * @return The 8-bit result.
 */
u8 subBytes(u8 a, u8 b, bool carry, bool &carryOut, bool &halfCarryOut);

/**
 * @brief Adjusts a binary addition result to binary coded decimal.
 * @param a The accumulator after the addition.
 * @param b The operand that was added.
 * @param carry Incoming carry flag.
 * @param subtract True when the operation was a subtraction.
 * @param carryOut Receives the adjusted carry flag.
 * @param halfCarryOut Receives the adjusted half carry flag.
 * @return The adjusted accumulator.
 */
u8 adjustBcd(u8 a, u8 b, bool carry, bool subtract, bool &carryOut, bool &halfCarryOut);

/**
 * @brief Computes the Nintendo logo checksum of a cartridge header.
 * @param rom The ROM image.
 * @return True when the logo at 0x0104-0x0133 is valid.
 */
bool checkLogoChecksum(const std::vector<u8> &rom);

/**
 * @brief Computes the global header checksum (0x014E-0x014F).
 * @param rom The ROM image.
 * @return True when the header checksum is valid.
 */
bool checkHeaderChecksum(const std::vector<u8> &rom);

/**
 * @brief Returns the internal DMG boot ROM (256 bytes).
 * @return Constant reference to the boot ROM image.
 */
const std::array<u8, kBootRomSize> &bootRom();

/**
 * @brief Returns the Nintendo logo drawn by the boot ROM.
 * @return Constant reference to the 48 bytes of the logo.
 */
const std::array<u8, 48> &nintendoLogo();

/**
 * @brief Formats a byte as a two digit upper case hexadecimal string.
 * @param value The byte to format.
 * @return The formatted string, for example "0A".
 */
std::string toHex(u8 value);

/**
 * @brief Formats a 16-bit value as a four digit upper case hexadecimal string.
 * @param value The value to format.
 * @return The formatted string, for example "FF40".
 */
std::string toHex16(u16 value);

/**
 * @brief Decodes a binary coded decimal register pair into a decimal number.
 * @param high High byte of the register pair.
 * @param low Low byte of the register pair.
 * @return The decoded value (0 when the digits are not valid BCD).
 */
int decodeBcd(u8 high, u8 low);

} // namespace util
} // namespace gb

#endif // GB_UTILITIES_H
