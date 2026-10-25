/**
 * @file interrupt.cpp
 * @brief Implementation of the interrupt controller.
 */

#include "emulation/Interrupt.h"

namespace gb {

InterruptController::InterruptController()
    : m_if(0xE1), m_ie(0x00)
{
}

void InterruptController::reset()
{
    m_if = 0xE1;
    m_ie = 0x00;
}

void InterruptController::request(InterruptType type)
{
    const u8 bit = static_cast<u8>(type);
    m_if |= static_cast<u8>(1u << bit);
}

void InterruptController::clear(InterruptType type)
{
    const u8 bit = static_cast<u8>(type);
    m_if &= static_cast<u8>(~(1u << bit));
}

bool InterruptController::isRequested(InterruptType type) const
{
    const u8 bit = static_cast<u8>(type);
    return ((m_if >> bit) & 0x01u) != 0;
}

bool InterruptController::isEnabled(InterruptType type) const
{
    const u8 bit = static_cast<u8>(type);
    return ((m_ie >> bit) & 0x01u) != 0;
}

bool InterruptController::pending(InterruptType &type) const
{
    const u8 masked = m_if & m_ie & 0x1F;
    if (masked == 0) {
        return false;
    }
    for (int i = 0; i < 5; ++i) {
        if ((masked >> i) & 0x01u) {
            type = static_cast<InterruptType>(i);
            return true;
        }
    }
    return false;
}

void InterruptController::acknowledge(InterruptType type)
{
    clear(type);
}

u16 InterruptController::vectorAddress(InterruptType type)
{
    const u8 index = static_cast<u8>(type);
    return static_cast<u16>(0x0040 + index * 8);
}

const char *InterruptController::name(InterruptType type)
{
    switch (type) {
    case InterruptType::VBlank:
        return "VBlank";
    case InterruptType::LCDStat:
        return "LCD STAT";
    case InterruptType::Timer:
        return "Timer";
    case InterruptType::Serial:
        return "Serial";
    case InterruptType::Joypad:
        return "Joypad";
    default:
        return "Unknown";
    }
}

u8 InterruptController::flags() const
{
    return m_if;
}

void InterruptController::setFlags(u8 value)
{
    m_if = value | 0xE0;
}

u8 InterruptController::enableRegister() const
{
    return m_ie;
}

void InterruptController::setEnableRegister(u8 value)
{
    m_ie = value;
}

} // namespace gb
