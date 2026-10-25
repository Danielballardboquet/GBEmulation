/**
 * @file Opcode.h
 * @brief LR35902 instruction set: register file, flags, decoding tables and
 *        the execution of every opcode (including the CB prefixed group).
 */

#ifndef GB_OPCODE_H
#define GB_OPCODE_H

#include "emulation/Types.h"

#include <array>
#include <cstddef>
#include <string>

namespace gb {

class Memory;
class InterruptController;

/**
 * @brief The eight 8-bit registers of the CPU.
 *
 * The numeric values are used as indexes by the instruction decoder, so they
 * must stay in the order B, C, D, E, H, L, (HL), A.
 */
enum class Register8 : u8 {
    B = 0, /**< B register. */
    C = 1, /**< C register. */
    D = 2, /**< D register. */
    E = 3, /**< E register. */
    H = 4, /**< H register. */
    L = 5, /**< L register. */
    HL = 6, /**< Memory pointed by HL (not a real register). */
    A = 7  /**< Accumulator. */
};

/**
 * @brief The four 16-bit register pairs addressable by the instruction set.
 */
enum class Register16 : u8 {
    BC = 0, /**< B:C pair. */
    DE = 1, /**< D:E pair. */
    HL = 2, /**< H:L pair. */
    SP = 3  /**< Stack pointer. */
};

/**
 * @brief Condition codes used by the conditional jumps, calls and returns.
 */
enum class Condition : u8 {
    NotZero = 0, /**< Z == 0. */
    Zero = 1,    /**< Z == 1. */
    NotCarry = 2, /**< C == 0. */
    Carry = 3    /**< C == 1. */
};

/**
 * @brief Arithmetic and logic operations of the ALU opcode group.
 */
enum class AluOperation : u8 {
    Add = 0,    /**< ADD A, value. */
    Adc = 1,    /**< ADC A, value. */
    Sub = 2,    /**< SUB value. */
    Sbc = 3,    /**< SBC A, value. */
    And = 4,    /**< AND value. */
    Xor = 5,    /**< XOR value. */
    Or = 6,     /**< OR value. */
    Compare = 7 /**< CP value. */
};

/**
 * @brief The five flags of the CPU stored in the F register.
 */
struct Flags {
    bool z = false; /**< Zero flag. */
    bool n = false; /**< Subtract flag. */
    bool h = false; /**< Half carry flag. */
    bool c = false; /**< Carry flag. */

    /**
     * @brief Packs the flags into the upper nibble of a byte.
     * @return The F register value.
     */
    u8 toByte() const;

    /**
     * @brief Unpacks the flags from the upper nibble of a byte.
     * @param value The F register value.
     */
    void fromByte(u8 value);

    /**
     * @brief Clears every flag.
     */
    void clear();
};

/**
 * @brief The complete register file of the LR35902 CPU.
 */
struct Registers {
    u8 a = 0x01; /**< Accumulator. */
    u8 f = 0xB0; /**< Flags register. */
    u8 b = 0x00; /**< B register. */
    u8 c = 0x13; /**< C register. */
    u8 d = 0x00; /**< D register. */
    u8 e = 0xD8; /**< E register. */
    u8 h = 0x01; /**< H register. */
    u8 l = 0x4D; /**< L register. */
    u16 sp = 0xFFFE; /**< Stack pointer. */
    u16 pc = 0x0100; /**< Program counter. */

    /**
     * @brief Returns the 16-bit value of a register pair.
     * @param pair The register pair to read.
     * @return The combined value.
     */
    u16 get16(Register16 pair) const;

    /**
     * @brief Assigns a 16-bit value to a register pair.
     * @param pair The register pair to write.
     * @param value The value to store.
     */
    void set16(Register16 pair, u16 value);

    /**
     * @brief Returns the 8-bit value of a register.
     * @param reg The register to read (Register8::HL is not allowed).
     * @return The register value.
     */
    u8 get8(Register8 reg) const;

    /**
     * @brief Assigns an 8-bit value to a register.
     * @param reg The register to write (Register8::HL is not allowed).
     * @param value The value to store.
     */
    void set8(Register8 reg, u8 value);
};

/**
 * @brief Executes the LR35902 instruction set on top of the memory bus.
 *
 * The class owns the register file and the interrupt master enable logic. It
 * exposes a single step() entry point that fetches, decodes and executes one
 * instruction and reports how many cycles it consumed.
 */
class Opcode {
public:
    /**
     * @brief Constructs a CPU with the register values of a DMG after boot.
     */
    Opcode();

    /**
     * @brief Attaches the memory bus used for all operand fetches.
     * @param memory Pointer to the memory bus.
     */
    void setMemory(Memory *memory);

    /**
     * @brief Attaches the interrupt controller used by EI, DI, HALT and RETI.
     * @param interrupts Pointer to the interrupt controller.
     */
    void setInterrupts(InterruptController *interrupts);

    /**
     * @brief Resets the CPU to the state it has after the boot ROM.
     */
    void reset();

    /**
     * @brief Executes exactly one instruction (or services one interrupt).
     * @return Number of CPU cycles consumed.
     */
    int step();

    /**
     * @brief Returns the register file.
     * @return Constant reference to the registers.
     */
    const Registers &registers() const;

    /**
     * @brief Returns the flags decoded from the F register.
     * @return The current flags.
     */
    Flags flags() const;

    /**
     * @brief Tells whether interrupts are globally enabled (IME).
     * @return True when IME is set.
     */
    bool interruptsEnabled() const;

    /**
     * @brief Tells whether the CPU is halted waiting for an interrupt.
     * @return True while halted.
     */
    bool isHalted() const;

    /**
     * @brief Tells whether the CPU is stopped in low power mode.
     * @return True while stopped.
     */
    bool isStopped() const;

    /**
     * @brief Tells whether an EI instruction is about to enable interrupts.
     * @return True when IME will be set after the next instruction.
     */
    bool isEiPending() const;

    /**
     * @brief Returns the number of instructions executed so far.
     * @return The instruction counter.
     */
    std::uint64_t instructionCount() const;

    /**
     * @brief Returns the address of the last executed instruction.
     * @return The program counter before the last step().
     */
    u16 lastInstructionAddress() const;

    /**
     * @brief Returns the opcode of the last executed instruction.
     * @return The opcode byte (0xCB for prefixed instructions).
     */
    u8 lastOpcode() const;

    /**
     * @brief Snapshot of the register file and the control flags of the CPU.
     */
    struct CpuState {
        Registers regs;                      /**< Register file. */
        bool ime = false;                    /**< Interrupt master enable. */
        bool imeScheduled = false;           /**< EI executed, IME after next instruction. */
        bool halted = false;                 /**< HALT state. */
        bool stopped = false;                /**< STOP state. */
        std::uint64_t instructions = 0;      /**< Executed instruction counter. */
        u16 lastAddress = 0;                 /**< Address of the last executed instruction. */
        u8 lastOpcode = 0;                   /**< Opcode of the last executed instruction. */
    };

    /**
     * @brief Returns a copy of the complete CPU state.
     * @return The registers and the control flags.
     */
    CpuState cpuState() const;

    /**
     * @brief Restores the CPU from a previously captured state.
     * @param state The state returned by cpuState().
     */
    void restoreCpuState(const CpuState &state);

    /**
     * @brief Returns a disassembly of the instruction at an address.
     * @param address The address to decode.
     * @return A string such as "LD A,0x12".
     */
    std::string disassemble(u16 address) const;

    /**
     * @brief Returns the length in bytes of the instruction at an address.
     * @param address The address to inspect.
     * @return 1, 2 or 3.
     */
    int instructionLength(u16 address) const;

    /**
     * @brief Returns the mnemonic of an unprefixed opcode.
     * @param opcode The opcode byte.
     * @return A static string with the mnemonic.
     */
    static const char *mnemonic(u8 opcode);

    /**
     * @brief Returns the mnemonic of a CB prefixed opcode.
     * @param opcode The byte following the 0xCB prefix.
     * @return A static string with the mnemonic.
     */
    static const char *cbMnemonic(u8 opcode);

    /**
     * @brief Returns the number of cycles of an unprefixed opcode.
     * @param opcode The opcode byte.
     * @return The cycle count (taken branch not included).
     */
    static int cycleCount(u8 opcode);

    /**
     * @brief Returns the number of cycles of a CB prefixed opcode.
     * @param opcode The byte following the 0xCB prefix.
     * @return The cycle count.
     */
    static int cbCycleCount(u8 opcode);

    /**
     * @brief Returns the name of a register for disassembly.
     * @param reg The register to name.
     * @return A static string such as "A" or "(HL)".
     */
    static const char *registerName(Register8 reg);

    /**
     * @brief Returns the name of a register pair for disassembly.
     * @param pair The pair to name.
     * @return A static string such as "BC" or "SP".
     */
    static const char *registerPairName(Register16 pair);

private:
    /**
     * @brief Fetches the next byte and advances the program counter.
     * @return The fetched byte.
     */
    u8 fetchByte();

    /**
     * @brief Fetches the next little endian word and advances the program counter.
     * @return The fetched word.
     */
    u16 fetchWord();

    /**
     * @brief Pushes a 16-bit value on the stack.
     * @param value The value to push.
     */
    void pushWord(u16 value);

    /**
     * @brief Pops a 16-bit value from the stack.
     * @return The popped value.
     */
    u16 popWord();

    /**
     * @brief Reads a register, dereferencing (HL) when needed.
     * @param reg The register to read.
     * @return The register or memory value.
     */
    u8 readRegister(Register8 reg);

    /**
     * @brief Writes a register, dereferencing (HL) when needed.
     * @param reg The register to write.
     * @param value The value to store.
     */
    void writeRegister(Register8 reg, u8 value);

    /**
     * @brief Returns the register pair encoded in bits 4-5 of an opcode.
     * @param opcode The opcode byte.
     * @return The decoded register pair.
     */
    static Register16 pairFromOpcode(u8 opcode);

    /**
     * @brief Returns the register encoded in bits 0-2 of an opcode.
     * @param opcode The opcode byte.
     * @return The decoded register.
     */
    static Register8 registerFromOpcode(u8 opcode);

    /**
     * @brief Executes an unprefixed opcode.
     * @param opcode The opcode byte.
     * @return Number of cycles consumed.
     */
    int execute(u8 opcode);

    /**
     * @brief Executes a CB prefixed opcode.
     * @param opcode The byte following the 0xCB prefix.
     * @return Number of cycles consumed.
     */
    int executeCb(u8 opcode);

    /**
     * @brief Executes the ALU operation selected by bits 3-5 of an opcode.
     * @param operation The operation to perform.
     * @param value The operand.
     */
    void alu(AluOperation operation, u8 value);

    /**
     * @brief Increments a register or (HL) and updates the flags.
     * @param reg The register to increment.
     */
    void increment(Register8 reg);

    /**
     * @brief Decrements a register or (HL) and updates the flags.
     * @param reg The register to decrement.
     */
    void decrement(Register8 reg);

    /**
     * @brief Adds a signed displacement to the program counter.
     * @param displacement The signed 8-bit displacement.
     * @return True when the branch was taken.
     */
    bool relativeJump(s8 displacement);

    /**
     * @brief Evaluates a condition code.
     * @param condition The condition to evaluate.
     * @return True when the condition holds.
     */
    bool checkCondition(Condition condition);

    /**
     * @brief Sets IME and schedules its activation after the next instruction.
     */
    void enableInterrupts();

    /**
     * @brief Clears IME immediately.
     */
    void disableInterrupts();

    /**
     * @brief Services the highest priority pending interrupt if allowed.
     * @return True when an interrupt was serviced.
     */
    bool serviceInterrupt();

    /**
     * @brief Returns the value of a register without side effects.
     * @param reg The register to read.
     * @return The register value, or the byte at HL for Register8::HL.
     */
    u8 peekRegister(Register8 reg) const;

    Registers m_regs;                      /**< Register file. */
    Memory *m_memory = nullptr;            /**< Attached memory bus. */
    InterruptController *m_interrupts = nullptr; /**< Attached interrupts. */
    bool m_ime = false;                    /**< Interrupt master enable. */
    bool m_imeScheduled = false;           /**< EI executed, IME after next instruction. */
    bool m_halted = false;                 /**< HALT state. */
    bool m_stopped = false;                /**< STOP state. */
    std::uint64_t m_instructions = 0;      /**< Executed instruction counter. */
    u16 m_lastAddress = 0;                 /**< Address of the last instruction. */
    u8 m_lastOpcode = 0;                   /**< Opcode of the last instruction. */
};

} // namespace gb

#endif // GB_OPCODE_H
