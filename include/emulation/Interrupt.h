/**
 * @file Interrupt.h
 * @brief Interrupt controller of the Game Boy (IE / IF registers, request,
 *        acknowledge and dispatch of the five interrupt sources).
 */

#ifndef GB_INTERRUPT_H
#define GB_INTERRUPT_H

#include "emulation/Types.h"

namespace gb {

/**
 * @brief Interrupt sources of the Game Boy, ordered by priority.
 *
 * The bit position of each enumerator is the bit used in the IF and IE
 * registers, so the lowest bit has the highest priority.
 */
enum class InterruptType : u8 {
    VBlank = 0,   /**< LCD entered vertical blank (0x40). */
    LCDStat = 1,  /**< LCD status change (0x48). */
    Timer = 2,    /**< Timer overflow (0x50). */
    Serial = 3,   /**< Serial transfer finished (0x58). */
    Joypad = 4    /**< Joypad button pressed (0x60). */
};

/**
 * @brief Owns the interrupt enable (IE) and interrupt flag (IF) registers.
 */
class InterruptController {
public:
    /**
     * @brief Constructs a controller with the post-boot register values.
     */
    InterruptController();

    /**
     * @brief Resets IE and IF to their post-boot values.
     */
    void reset();

    /**
     * @brief Raises an interrupt request by setting its bit in IF.
     * @param type The interrupt source to request.
     */
    void request(InterruptType type);

    /**
     * @brief Clears the request bit of an interrupt source.
     * @param type The interrupt source to clear.
     */
    void clear(InterruptType type);

    /**
     * @brief Tells whether a source has a pending request.
     * @param type The interrupt source to test.
     * @return True when the request bit is set.
     */
    bool isRequested(InterruptType type) const;

    /**
     * @brief Tells whether a source is enabled in IE.
     * @param type The interrupt source to test.
     * @return True when the enable bit is set.
     */
    bool isEnabled(InterruptType type) const;

    /**
     * @brief Returns the highest priority pending and enabled interrupt.
     * @param type Receives the selected interrupt source.
     * @return True when an interrupt is pending.
     */
    bool pending(InterruptType &type) const;

    /**
     * @brief Acknowledges an interrupt by clearing its IF bit.
     * @param type The interrupt source to acknowledge.
     */
    void acknowledge(InterruptType type);

    /**
     * @brief Returns the address of the handler of an interrupt source.
     * @param type The interrupt source.
     * @return The vector address (0x0040-0x0060).
     */
    static u16 vectorAddress(InterruptType type);

    /**
     * @brief Returns the human readable name of an interrupt source.
     * @param type The interrupt source.
     * @return A static string such as "VBlank".
     */
    static const char *name(InterruptType type);

    /**
     * @brief Reads the interrupt flag register.
     * @return The value of IF (only the lower five bits are meaningful).
     */
    u8 flags() const;

    /**
     * @brief Writes the interrupt flag register.
     * @param value The new value of IF.
     */
    void setFlags(u8 value);

    /**
     * @brief Reads the interrupt enable register.
     * @return The value of IE.
     */
    u8 enableRegister() const;

    /**
     * @brief Writes the interrupt enable register.
     * @param value The new value of IE.
     */
    void setEnableRegister(u8 value);

private:
    u8 m_if = 0xE1; /**< Interrupt flags (IF, 0xFF0F). */
    u8 m_ie = 0x00; /**< Interrupt enable (IE, 0xFFFF). */
};

} // namespace gb

#endif // GB_INTERRUPT_H
