/**
 * @file opcode.cpp
 * @brief Implementation of the full LR35902 instruction set.
 *
 * The decoder uses a 256 entry table that maps every opcode to a micro
 * operation, an instruction length and a base cycle count. The register to
 * register loads (0x40-0x7F), the ALU group (0x80-0xBF) and the entire CB
 * prefixed group are decoded from their bit fields instead, which keeps the
 * table small and the timing tables accurate.
 */

#include "emulation/Opcode.h"
#include "emulation/Interrupt.h"
#include "emulation/Memory.h"
#include "emulation/Types.h"
#include "emulation/Utilities.h"

#include <cstdio>

namespace gb {

namespace {

/**
 * @brief Micro operations addressed by the main decode table.
 */
enum class OpMode : int {
    None,        /**< NOP or illegal opcode. */
    LdBC_d16,    /**< LD BC,d16 */
    LdBCm_A,     /**< LD (BC),A */
    IncBC,       /**< INC BC */
    IncB,        /**< INC B */
    DecB,        /**< DEC B */
    LdB_d8,      /**< LD B,d8 */
    Rlca,        /**< RLCA */
    Lda16m_SP,   /**< LD (a16),SP */
    AddHL_BC,    /**< ADD HL,BC */
    LdA_BCm,     /**< LD A,(BC) */
    DecBC,       /**< DEC BC */
    IncC,        /**< INC C */
    DecC,        /**< DEC C */
    LdC_d8,      /**< LD C,d8 */
    Rrca,        /**< RRCA */
    Stop,        /**< STOP */
    LdDE_d16,    /**< LD DE,d16 */
    LdDEm_A,     /**< LD (DE),A */
    IncDE,       /**< INC DE */
    IncD,        /**< INC D */
    DecD,        /**< DEC D */
    LdD_d8,      /**< LD D,d8 */
    Rla,         /**< RLA */
    Jr,          /**< JR r8 */
    AddHL_DE,    /**< ADD HL,DE */
    LdA_DEm,     /**< LD A,(DE) */
    DecDE,       /**< DEC DE */
    IncE,        /**< INC E */
    DecE,        /**< DEC E */
    LdE_d8,      /**< LD E,d8 */
    Rra,         /**< RRA */
    Jr_NZ,       /**< JR NZ,r8 */
    LdHL_d16,    /**< LD HL,d16 */
    LdHLinc_A,   /**< LD (HL+),A */
    IncHL,       /**< INC HL */
    IncH,        /**< INC H */
    DecH,        /**< DEC H */
    LdH_d8,      /**< LD H,d8 */
    Daa,         /**< DAA */
    Jr_Z,        /**< JR Z,r8 */
    AddHL_HL,    /**< ADD HL,HL */
    LdA_HLinc,   /**< LD A,(HL+) */
    DecHL,       /**< DEC HL */
    IncL,        /**< INC L */
    DecL,        /**< DEC L */
    LdL_d8,      /**< LD L,d8 */
    Cpl,         /**< CPL */
    Jr_NC,       /**< JR NC,r8 */
    LdSP_d16,    /**< LD SP,d16 */
    LdHLdec_A,   /**< LD (HL-),A */
    IncSP,       /**< INC SP */
    IncHLm,      /**< INC (HL) */
    DecHLm,      /**< DEC (HL) */
    LdHLm_d8,    /**< LD (HL),d8 */
    Scf,         /**< SCF */
    Jr_C,        /**< JR C,r8 */
    AddHL_SP,    /**< ADD HL,SP */
    LdA_HLdec,   /**< LD A,(HL-) */
    DecSP,       /**< DEC SP */
    IncA,        /**< INC A */
    DecA,        /**< DEC A */
    LdA_d8,      /**< LD A,d8 */
    Ccf,         /**< CCF */
    Halt,        /**< HALT */
    Ret_NZ,      /**< RET NZ */
    Pop_BC,      /**< POP BC */
    Jp_NZ,       /**< JP NZ,a16 */
    Jp,          /**< JP a16 */
    Call_NZ,     /**< CALL NZ,a16 */
    Push_BC,     /**< PUSH BC */
    Add_d8,      /**< ADD A,d8 */
    Rst_00,      /**< RST 00H */
    Ret_Z,       /**< RET Z */
    Ret,         /**< RET */
    Jp_Z,        /**< JP Z,a16 */
    Prefix,      /**< CB prefix */
    Call_Z,      /**< CALL Z,a16 */
    Call,        /**< CALL a16 */
    Adc_d8,      /**< ADC A,d8 */
    Rst_08,      /**< RST 08H */
    Ret_NC,      /**< RET NC */
    Pop_DE,      /**< POP DE */
    Jp_NC,       /**< JP NC,a16 */
    Call_NC,     /**< CALL NC,a16 */
    Push_DE,     /**< PUSH DE */
    Sub_d8,      /**< SUB d8 */
    Rst_10,      /**< RST 10H */
    Ret_C,       /**< RET C */
    Reti,        /**< RETI */
    Jp_C,        /**< JP C,a16 */
    Call_C,      /**< CALL C,a16 */
    Sbc_d8,      /**< SBC A,d8 */
    Rst_18,      /**< RST 18H */
    Ldh_a8,      /**< LDH (a8),A */
    Pop_HL,      /**< POP HL */
    LdCm_A,      /**< LD (C),A */
    Push_HL,     /**< PUSH HL */
    And_d8,      /**< AND d8 */
    Rst_20,      /**< RST 20H */
    AddSP_r8,    /**< ADD SP,r8 */
    Jp_HL,       /**< JP HL */
    Lda16m_A,    /**< LD (a16),A */
    Xor_d8,      /**< XOR d8 */
    Rst_28,      /**< RST 28H */
    Ldh_A_a8,    /**< LDH A,(a8) */
    Pop_AF,      /**< POP AF */
    LdA_Cm,      /**< LD A,(C) */
    Di,          /**< DI */
    Push_AF,     /**< PUSH AF */
    Or_d8,       /**< OR d8 */
    Rst_30,      /**< RST 30H */
    LdHL_SP_r8,  /**< LD HL,SP+r8 */
    LdSP_HL,     /**< LD SP,HL */
    LdA_a16m,    /**< LD A,(a16) */
    Ei,          /**< EI */
    Cp_d8,       /**< CP d8 */
    Rst_38       /**< RST 38H */
};

/**
 * @brief One entry of the main decode table.
 */
struct TableEntry {
    OpMode mode; /**< Micro operation to execute. */
    int length;  /**< Instruction size in bytes. */
    int cycles;  /**< Base cycles, taken branches not included. */
};

/** @brief Mnemonics of the fixed opcode regions (0x00-0x3F, 0xC0-0xFF). */
constexpr const char *kMnemonicLow[0x40] = {
    "NOP", "LD BC", "LD (BC),A", "INC BC", "INC B", "DEC B", "LD B", "RLCA",
    "LD (a16),SP", "ADD HL,BC", "LD A,(BC)", "DEC BC", "INC C", "DEC C", "LD C", "RRCA",
    "STOP", "LD DE", "LD (DE),A", "INC DE", "INC D", "DEC D", "LD D", "RLA",
    "JR", "ADD HL,DE", "LD A,(DE)", "DEC DE", "INC E", "DEC E", "LD E", "RRA",
    "JR NZ", "LD HL", "LD (HL+),A", "INC HL", "INC H", "DEC H", "LD H", "DAA",
    "JR Z", "ADD HL,HL", "LD A,(HL+)", "DEC HL", "INC L", "DEC L", "LD L", "CPL",
    "JR NC", "LD SP", "LD (HL-),A", "INC SP", "INC (HL)", "DEC (HL)", "LD (HL)", "SCF",
    "JR C", "ADD HL,SP", "LD A,(HL-)", "DEC SP", "INC A", "DEC A", "LD A", "CCF"
};

/** @brief Mnemonics of the fixed opcodes 0xC0-0xFF. */
constexpr const char *kMnemonicHigh[0x40] = {
    "RET NZ", "POP BC", "JP NZ", "JP", "CALL NZ", "PUSH BC", "ADD A", "RST 00H",
    "RET Z", "RET", "JP Z", "PREFIX", "CALL Z", "CALL", "ADC A", "RST 08H",
    "RET NC", "POP DE", "JP NC", "NOP", "CALL NC", "PUSH DE", "SUB", "RST 10H",
    "RET C", "RETI", "JP C", "NOP", "CALL C", "NOP", "SBC A", "RST 18H",
    "LDH (a8),A", "POP HL", "LD (C),A", "NOP", "NOP", "PUSH HL", "AND", "RST 20H",
    "ADD SP", "JP HL", "LD (a16),A", "NOP", "NOP", "NOP", "XOR", "RST 28H",
    "LDH A,(a8)", "POP AF", "LD A,(C)", "DI", "NOP", "PUSH AF", "OR", "RST 30H",
    "LD HL,SP", "LD SP,HL", "LD A,(a16)", "EI", "NOP", "NOP", "CP", "RST 38H"
};

/** @brief Names of the CB rotate/shift/swap operations indexed by (opcode >> 3). */
constexpr const char *kCbShiftNames[8] = {
    "RLC ", "RRC ", "RL ", "RR ", "SLA ", "SRA ", "SWAP ", "SRL "
};

/** @brief Names of the eight 8-bit registers in the B,C,D,E,H,L,(HL),A order. */
constexpr const char *kRegisterNames[8] = {
    "B", "C", "D", "E", "H", "L", "(HL)", "A"
};

/** @brief Names of the four register pairs. */
constexpr const char *kPairNames[4] = {
    "BC", "DE", "HL", "SP"
};

/** @brief The main opcode decode table, indexed by the opcode byte. */
constexpr TableEntry kDecodeTable[256] = {
    // 0x00-0x0F
    {OpMode::None, 1, 4},      {OpMode::LdBC_d16, 3, 12},  {OpMode::LdBCm_A, 1, 8},
    {OpMode::IncBC, 1, 8},     {OpMode::IncB, 1, 4},       {OpMode::DecB, 1, 4},
    {OpMode::LdB_d8, 2, 8},    {OpMode::Rlca, 1, 4},       {OpMode::Lda16m_SP, 3, 20},
    {OpMode::AddHL_BC, 1, 8},  {OpMode::LdA_BCm, 1, 8},    {OpMode::DecBC, 1, 8},
    {OpMode::IncC, 1, 4},      {OpMode::DecC, 1, 4},       {OpMode::LdC_d8, 2, 8},
    {OpMode::Rrca, 1, 4},
    // 0x10-0x1F
    {OpMode::Stop, 1, 4},      {OpMode::LdDE_d16, 3, 12},  {OpMode::LdDEm_A, 1, 8},
    {OpMode::IncDE, 1, 8},     {OpMode::IncD, 1, 4},       {OpMode::DecD, 1, 4},
    {OpMode::LdD_d8, 2, 8},    {OpMode::Rla, 1, 4},        {OpMode::Jr, 2, 12},
    {OpMode::AddHL_DE, 1, 8},  {OpMode::LdA_DEm, 1, 8},    {OpMode::DecDE, 1, 8},
    {OpMode::IncE, 1, 4},      {OpMode::DecE, 1, 4},       {OpMode::LdE_d8, 2, 8},
    {OpMode::Rra, 1, 4},
    // 0x20-0x2F
    {OpMode::Jr_NZ, 2, 8},     {OpMode::LdHL_d16, 3, 12},  {OpMode::LdHLinc_A, 1, 8},
    {OpMode::IncHL, 1, 8},     {OpMode::IncH, 1, 4},       {OpMode::DecH, 1, 4},
    {OpMode::LdH_d8, 2, 8},    {OpMode::Daa, 1, 4},        {OpMode::Jr_Z, 2, 8},
    {OpMode::AddHL_HL, 1, 8},  {OpMode::LdA_HLinc, 1, 8},  {OpMode::DecHL, 1, 8},
    {OpMode::IncL, 1, 4},      {OpMode::DecL, 1, 4},       {OpMode::LdL_d8, 2, 8},
    {OpMode::Cpl, 1, 4},
    // 0x30-0x3F
    {OpMode::Jr_NC, 2, 8},     {OpMode::LdSP_d16, 3, 12},  {OpMode::LdHLdec_A, 1, 8},
    {OpMode::IncSP, 1, 8},     {OpMode::IncHLm, 1, 12},    {OpMode::DecHLm, 1, 12},
    {OpMode::LdHLm_d8, 2, 12}, {OpMode::Scf, 1, 4},        {OpMode::Jr_C, 2, 8},
    {OpMode::AddHL_SP, 1, 8},  {OpMode::LdA_HLdec, 1, 8},  {OpMode::DecSP, 1, 8},
    {OpMode::IncA, 1, 4},      {OpMode::DecA, 1, 4},       {OpMode::LdA_d8, 2, 8},
    {OpMode::Ccf, 1, 4},
    // 0x40-0x7F: LD r,r' decoded from the bit fields.
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::Halt, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    // 0x80-0xBF: ALU operations decoded from the bit fields.
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},  {OpMode::None, 1, 4},
    // 0xC0-0xCF
    {OpMode::Ret_NZ, 1, 8},   {OpMode::Pop_BC, 1, 12},    {OpMode::Jp_NZ, 3, 12},
    {OpMode::Jp, 3, 16},      {OpMode::Call_NZ, 3, 12},   {OpMode::Push_BC, 1, 16},
    {OpMode::Add_d8, 2, 8},   {OpMode::Rst_00, 1, 16},    {OpMode::Ret_Z, 1, 8},
    {OpMode::Ret, 1, 16},     {OpMode::Jp_Z, 3, 12},      {OpMode::Prefix, 1, 4},
    {OpMode::Call_Z, 3, 12},  {OpMode::Call, 3, 24},      {OpMode::Adc_d8, 2, 8},
    {OpMode::Rst_08, 1, 16},
    // 0xD0-0xDF
    {OpMode::Ret_NC, 1, 8},   {OpMode::Pop_DE, 1, 12},    {OpMode::Jp_NC, 3, 12},
    {OpMode::None, 1, 4},     {OpMode::Call_NC, 3, 12},   {OpMode::Push_DE, 1, 16},
    {OpMode::Sub_d8, 2, 8},   {OpMode::Rst_10, 1, 16},    {OpMode::Ret_C, 1, 8},
    {OpMode::Reti, 1, 16},    {OpMode::Jp_C, 3, 12},      {OpMode::None, 1, 4},
    {OpMode::Call_C, 3, 12},  {OpMode::None, 1, 4},       {OpMode::Sbc_d8, 2, 8},
    {OpMode::Rst_18, 1, 16},
    // 0xE0-0xEF
    {OpMode::Ldh_a8, 2, 12},  {OpMode::Pop_HL, 1, 12},    {OpMode::LdCm_A, 1, 8},
    {OpMode::None, 1, 4},     {OpMode::None, 1, 4},       {OpMode::Push_HL, 1, 16},
    {OpMode::And_d8, 2, 8},   {OpMode::Rst_20, 1, 16},    {OpMode::AddSP_r8, 2, 16},
    {OpMode::Jp_HL, 1, 4},    {OpMode::Lda16m_A, 3, 16},  {OpMode::None, 1, 4},
    {OpMode::None, 1, 4},     {OpMode::None, 1, 4},       {OpMode::Xor_d8, 2, 8},
    {OpMode::Rst_28, 1, 16},
    // 0xF0-0xFF
    {OpMode::Ldh_A_a8, 2, 12}, {OpMode::Pop_AF, 1, 12},   {OpMode::LdA_Cm, 1, 8},
    {OpMode::Di, 1, 4},        {OpMode::None, 1, 4},      {OpMode::Push_AF, 1, 16},
    {OpMode::Or_d8, 2, 8},     {OpMode::Rst_30, 1, 16},   {OpMode::LdHL_SP_r8, 2, 12},
    {OpMode::LdSP_HL, 1, 8},   {OpMode::LdA_a16m, 3, 16}, {OpMode::Ei, 1, 4},
    {OpMode::None, 1, 4},      {OpMode::None, 1, 4},      {OpMode::Cp_d8, 2, 8},
    {OpMode::Rst_38, 1, 16}
};

/**
 * @brief Returns the ALU operation encoded in bits 3-5 of an opcode.
 * @param opcode The opcode byte of the ALU group.
 * @return The decoded operation.
 */
AluOperation aluFromOpcode(u8 opcode)
{
    return static_cast<AluOperation>((opcode >> 3) & 0x07);
}

/**
 * @brief Formats a mnemonic with a register operand into a static buffer.
 * @param name The instruction name such as "LD".
 * @param reg The register operand.
 * @return A pointer to the formatted string.
 */
const char *formatReg(const char *name, Register8 reg)
{
    static char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%s %s", name,
                  Opcode::registerName(reg));
    return buffer;
}

/**
 * @brief Formats a two operand LD mnemonic into a static buffer.
 * @param dst The destination register.
 * @param src The source register.
 * @return A pointer to the formatted string.
 */
const char *formatLd(Register8 dst, Register8 src)
{
    static char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "LD %s,%s",
                  Opcode::registerName(dst), Opcode::registerName(src));
    return buffer;
}

} // namespace

u8 Flags::toByte() const
{
    return static_cast<u8>((z ? 0x80 : 0x00) | (n ? 0x40 : 0x00)
                           | (h ? 0x20 : 0x00) | (c ? 0x10 : 0x00));
}

void Flags::fromByte(u8 value)
{
    z = (value & 0x80) != 0;
    n = (value & 0x40) != 0;
    h = (value & 0x20) != 0;
    c = (value & 0x10) != 0;
}

void Flags::clear()
{
    z = false;
    n = false;
    h = false;
    c = false;
}

u16 Registers::get16(Register16 pair) const
{
    switch (pair) {
    case Register16::BC:
        return makeWord(b, c);
    case Register16::DE:
        return makeWord(d, e);
    case Register16::HL:
        return makeWord(h, l);
    case Register16::SP:
    default:
        return sp;
    }
}

void Registers::set16(Register16 pair, u16 value)
{
    switch (pair) {
    case Register16::BC:
        b = highByte(value);
        c = lowByte(value);
        break;
    case Register16::DE:
        d = highByte(value);
        e = lowByte(value);
        break;
    case Register16::HL:
        h = highByte(value);
        l = lowByte(value);
        break;
    case Register16::SP:
    default:
        sp = value;
        break;
    }
}

u8 Registers::get8(Register8 reg) const
{
    switch (reg) {
    case Register8::B:
        return b;
    case Register8::C:
        return c;
    case Register8::D:
        return d;
    case Register8::E:
        return e;
    case Register8::H:
        return h;
    case Register8::L:
        return l;
    case Register8::A:
    default:
        return a;
    }
}

void Registers::set8(Register8 reg, u8 value)
{
    switch (reg) {
    case Register8::B:
        b = value;
        break;
    case Register8::C:
        c = value;
        break;
    case Register8::D:
        d = value;
        break;
    case Register8::E:
        e = value;
        break;
    case Register8::H:
        h = value;
        break;
    case Register8::L:
        l = value;
        break;
    case Register8::A:
    default:
        a = value;
        break;
    }
}

Opcode::Opcode()
{
    reset();
}

void Opcode::setMemory(Memory *memory)
{
    m_memory = memory;
}

void Opcode::setInterrupts(InterruptController *interrupts)
{
    m_interrupts = interrupts;
}

void Opcode::reset()
{
    m_regs.a = 0x01;
    m_regs.f = 0xB0;
    m_regs.b = 0x00;
    m_regs.c = 0x13;
    m_regs.d = 0x00;
    m_regs.e = 0xD8;
    m_regs.h = 0x01;
    m_regs.l = 0x4D;
    m_regs.sp = 0xFFFE;
    // When the boot ROM is mapped the CPU starts at 0x0000, otherwise the
    // program is entered at the post-boot entry point.
    m_regs.pc = (m_memory != nullptr && m_memory->isBootRomEnabled()) ? 0x0000 : 0x0100;
    m_ime = false;
    m_imeScheduled = false;
    m_halted = false;
    m_stopped = false;
    m_instructions = 0;
    m_lastAddress = 0;
    m_lastOpcode = 0;
}

int Opcode::step()
{
    // An EI schedules the enable for the start of the next instruction.
    if (m_imeScheduled) {
        m_ime = true;
        m_imeScheduled = false;
    }

    if (m_stopped) {
        InterruptType type;
        if (m_interrupts != nullptr && m_interrupts->pending(type)) {
            m_stopped = false;
        }
        return 4;
    }

    if (m_halted) {
        InterruptType type;
        if (m_interrupts == nullptr || !m_interrupts->pending(type)) {
            return 4;
        }
        m_halted = false;
        if (!m_ime) {
            // Halt bug: with IME cleared the CPU wakes up on any pending
            // interrupt, sets IME and resumes execution.
            m_ime = true;
            return 4;
        }
    }

    // Service the highest priority pending interrupt when IME is set.
    if (serviceInterrupt()) {
        return 20;
    }

    m_lastAddress = m_regs.pc;
    const u8 opcode = fetchByte();
    m_lastOpcode = opcode;
    ++m_instructions;

    if (opcode == 0xCB) {
        const u8 subOpcode = fetchByte();
        m_lastOpcode = subOpcode;
        return executeCb(subOpcode);
    }
    return execute(opcode);
}

const Registers &Opcode::registers() const
{
    return m_regs;
}

Flags Opcode::flags() const
{
    Flags value;
    value.fromByte(m_regs.f);
    return value;
}

bool Opcode::interruptsEnabled() const
{
    return m_ime;
}

bool Opcode::isHalted() const
{
    return m_halted;
}

bool Opcode::isStopped() const
{
    return m_stopped;
}

bool Opcode::isEiPending() const
{
    return m_imeScheduled;
}

std::uint64_t Opcode::instructionCount() const
{
    return m_instructions;
}

u16 Opcode::lastInstructionAddress() const
{
    return m_lastAddress;
}

u8 Opcode::lastOpcode() const
{
    return m_lastOpcode;
}

std::string Opcode::disassemble(u16 address) const
{
    if (m_memory == nullptr) {
        return "?";
    }
    const u8 opcode = m_memory->peek(address);
    if (opcode == 0xCB) {
        const u8 sub = m_memory->peek(static_cast<u16>(address + 1));
        return std::string(cbMnemonic(sub));
    }
    std::string text = mnemonic(opcode);
    const int length = instructionLength(address);
    if (length >= 2) {
        text += " 0x";
        text += util::toHex(m_memory->peek(static_cast<u16>(address + 1)));
    }
    if (length >= 3) {
        const u16 value = static_cast<u16>(
            m_memory->peek(static_cast<u16>(address + 1))
            | (static_cast<u16>(m_memory->peek(static_cast<u16>(address + 2))) << 8));
        text += " 0x";
        text += util::toHex16(value);
    }
    return text;
}

int Opcode::instructionLength(u16 address) const
{
    if (m_memory == nullptr) {
        return 1;
    }
    const u8 opcode = m_memory->peek(address);
    if (opcode == 0xCB) {
        return 2;
    }
    if (opcode >= 0x40 && opcode <= 0x7F) {
        return 1;
    }
    if (opcode >= 0x80 && opcode <= 0xBF) {
        return 1;
    }
    return kDecodeTable[opcode].length;
}

const char *Opcode::mnemonic(u8 opcode)
{
    if (opcode < 0x40) {
        return kMnemonicLow[opcode];
    }
    if (opcode < 0x80) {
        if (opcode == 0x76) {
            return "HALT";
        }
        return formatLd(static_cast<Register8>((opcode >> 3) & 0x07),
                        static_cast<Register8>(opcode & 0x07));
    }
    if (opcode < 0xC0) {
        const AluOperation operation = aluFromOpcode(opcode);
        const Register8 reg = static_cast<Register8>(opcode & 0x07);
        switch (operation) {
        case AluOperation::Add:
            return formatReg("ADD A", reg);
        case AluOperation::Adc:
            return formatReg("ADC A", reg);
        case AluOperation::Sub:
            return formatReg("SUB", reg);
        case AluOperation::Sbc:
            return formatReg("SBC A", reg);
        case AluOperation::And:
            return formatReg("AND", reg);
        case AluOperation::Xor:
            return formatReg("XOR", reg);
        case AluOperation::Or:
            return formatReg("OR", reg);
        case AluOperation::Compare:
        default:
            return formatReg("CP", reg);
        }
    }
    return kMnemonicHigh[opcode - 0xC0];
}

const char *Opcode::cbMnemonic(u8 opcode)
{
    static char buffer[32];
    const int operation = (opcode >> 3) & 0x1F;
    const Register8 reg = static_cast<Register8>(opcode & 0x07);
    if (operation < 8) {
        std::snprintf(buffer, sizeof(buffer), "%s%s",
                      kCbShiftNames[operation], registerName(reg));
    } else if (operation < 16) {
        std::snprintf(buffer, sizeof(buffer), "BIT %d,%s",
                      operation - 8, registerName(reg));
    } else if (operation < 24) {
        std::snprintf(buffer, sizeof(buffer), "RES %d,%s",
                      operation - 16, registerName(reg));
    } else {
        std::snprintf(buffer, sizeof(buffer), "SET %d,%s",
                      operation - 24, registerName(reg));
    }
    return buffer;
}

int Opcode::cycleCount(u8 opcode)
{
    if (opcode >= 0x40 && opcode <= 0x7F) {
        const int src = opcode & 0x07;
        const int dst = (opcode >> 3) & 0x07;
        return (src == 6 || dst == 6) ? 8 : 4;
    }
    if (opcode >= 0x80 && opcode <= 0xBF) {
        return (opcode & 0x07) == 6 ? 8 : 4;
    }
    return kDecodeTable[opcode].cycles;
}

int Opcode::cbCycleCount(u8 opcode)
{
    const int reg = opcode & 0x07;
    const int operation = (opcode >> 3) & 0x1F;
    if (operation >= 8 && operation < 16) {
        // BIT n,r
        return reg == 6 ? 12 : 8;
    }
    return reg == 6 ? 16 : 8;
}

const char *Opcode::registerName(Register8 reg)
{
    return kRegisterNames[static_cast<int>(reg)];
}

const char *Opcode::registerPairName(Register16 pair)
{
    return kPairNames[static_cast<int>(pair)];
}

u8 Opcode::fetchByte()
{
    if (m_memory == nullptr) {
        return 0;
    }
    const u8 value = m_memory->read(m_regs.pc);
    m_regs.pc = static_cast<u16>(m_regs.pc + 1);
    return value;
}

u16 Opcode::fetchWord()
{
    const u8 low = fetchByte();
    const u8 high = fetchByte();
    return makeWord(high, low);
}

void Opcode::pushWord(u16 value)
{
    if (m_memory == nullptr) {
        return;
    }
    m_regs.sp = static_cast<u16>(m_regs.sp - 1);
    m_memory->write(m_regs.sp, highByte(value));
    m_regs.sp = static_cast<u16>(m_regs.sp - 1);
    m_memory->write(m_regs.sp, lowByte(value));
}

u16 Opcode::popWord()
{
    if (m_memory == nullptr) {
        return 0;
    }
    const u8 low = m_memory->read(m_regs.sp);
    m_regs.sp = static_cast<u16>(m_regs.sp + 1);
    const u8 high = m_memory->read(m_regs.sp);
    m_regs.sp = static_cast<u16>(m_regs.sp + 1);
    return makeWord(high, low);
}

u8 Opcode::readRegister(Register8 reg)
{
    if (reg == Register8::HL) {
        return m_memory != nullptr ? m_memory->read(m_regs.get16(Register16::HL)) : 0;
    }
    return m_regs.get8(reg);
}

void Opcode::writeRegister(Register8 reg, u8 value)
{
    if (reg == Register8::HL) {
        if (m_memory != nullptr) {
            m_memory->write(m_regs.get16(Register16::HL), value);
        }
        return;
    }
    m_regs.set8(reg, value);
}

Register16 Opcode::pairFromOpcode(u8 opcode)
{
    return static_cast<Register16>((opcode >> 4) & 0x03);
}

Register8 Opcode::registerFromOpcode(u8 opcode)
{
    return static_cast<Register8>(opcode & 0x07);
}

void Opcode::alu(AluOperation operation, u8 value)
{
    const u8 a = m_regs.a;
    Flags f;
    bool carry = false;
    bool halfCarry = false;
    switch (operation) {
    case AluOperation::Add:
        m_regs.a = util::addBytes(a, value, false, carry, halfCarry);
        f.z = m_regs.a == 0;
        f.n = false;
        f.h = halfCarry;
        f.c = carry;
        break;
    case AluOperation::Adc:
        f = flags();
        m_regs.a = util::addBytes(a, value, f.c, carry, halfCarry);
        f.z = m_regs.a == 0;
        f.n = false;
        f.h = halfCarry;
        f.c = carry;
        break;
    case AluOperation::Sub:
        m_regs.a = util::subBytes(a, value, false, carry, halfCarry);
        f.z = m_regs.a == 0;
        f.n = true;
        f.h = halfCarry;
        f.c = carry;
        break;
    case AluOperation::Sbc:
        f = flags();
        m_regs.a = util::subBytes(a, value, f.c, carry, halfCarry);
        f.z = m_regs.a == 0;
        f.n = true;
        f.h = halfCarry;
        f.c = carry;
        break;
    case AluOperation::And:
        m_regs.a = static_cast<u8>(a & value);
        f.z = m_regs.a == 0;
        f.n = false;
        f.h = true;
        f.c = false;
        break;
    case AluOperation::Xor:
        m_regs.a = static_cast<u8>(a ^ value);
        f.z = m_regs.a == 0;
        f.clear();
        f.z = m_regs.a == 0;
        break;
    case AluOperation::Or:
        m_regs.a = static_cast<u8>(a | value);
        f.z = m_regs.a == 0;
        f.clear();
        f.z = m_regs.a == 0;
        break;
    case AluOperation::Compare:
    default:
        f = flags();
        (void)util::subBytes(a, value, false, carry, halfCarry);
        f.z = a == value;
        f.n = true;
        f.h = halfCarry;
        f.c = carry;
        break;
    }
    m_regs.f = f.toByte();
}

void Opcode::increment(Register8 reg)
{
    const u8 oldValue = readRegister(reg);
    const u8 result = static_cast<u8>(oldValue + 1);
    Flags f = flags();
    f.z = result == 0;
    f.n = false;
    f.h = (oldValue & 0x0F) + 1 > 0x0F;
    writeRegister(reg, result);
    m_regs.f = f.toByte();
}

void Opcode::decrement(Register8 reg)
{
    const u8 oldValue = readRegister(reg);
    const u8 result = static_cast<u8>(oldValue - 1);
    Flags f = flags();
    f.z = result == 0;
    f.n = true;
    f.h = (oldValue & 0x0F) == 0;
    writeRegister(reg, result);
    m_regs.f = f.toByte();
}

bool Opcode::relativeJump(s8 displacement)
{
    m_regs.pc = static_cast<u16>(m_regs.pc + displacement);
    return true;
}

bool Opcode::checkCondition(Condition condition)
{
    const Flags f = flags();
    switch (condition) {
    case Condition::NotZero:
        return !f.z;
    case Condition::Zero:
        return f.z;
    case Condition::NotCarry:
        return !f.c;
    case Condition::Carry:
    default:
        return f.c;
    }
}

void Opcode::enableInterrupts()
{
    m_imeScheduled = true;
}

void Opcode::disableInterrupts()
{
    m_ime = false;
    m_imeScheduled = false;
}

bool Opcode::serviceInterrupt()
{
    if (m_interrupts == nullptr || !m_ime) {
        return false;
    }
    InterruptType type;
    if (!m_interrupts->pending(type)) {
        return false;
    }
    m_interrupts->acknowledge(type);
    m_ime = false;
    pushWord(m_regs.pc);
    m_regs.pc = InterruptController::vectorAddress(type);
    ++m_instructions;
    return true;
}

u8 Opcode::peekRegister(Register8 reg) const
{
    if (reg == Register8::HL) {
        return m_memory != nullptr ? m_memory->peek(m_regs.get16(Register16::HL)) : 0;
    }
    return m_regs.get8(reg);
}

int Opcode::execute(u8 opcode)
{
    // Register to register loads, 0x40-0x7F (0x76 is HALT).
    if (opcode >= 0x40 && opcode <= 0x7F) {
        if (opcode == 0x76) {
            m_halted = true;
            return 4;
        }
        const Register8 dst = static_cast<Register8>((opcode >> 3) & 0x07);
        const Register8 src = static_cast<Register8>(opcode & 0x07);
        writeRegister(dst, readRegister(src));
        return (dst == Register8::HL || src == Register8::HL) ? 8 : 4;
    }

    // ALU group, 0x80-0xBF.
    if (opcode >= 0x80 && opcode <= 0xBF) {
        const Register8 reg = static_cast<Register8>(opcode & 0x07);
        alu(aluFromOpcode(opcode), readRegister(reg));
        return reg == Register8::HL ? 8 : 4;
    }

    const TableEntry &entry = kDecodeTable[opcode];
    int cycles = entry.cycles;
    int extra = 0;

    switch (entry.mode) {
    case OpMode::None:
        break; // NOP or illegal opcode.
    case OpMode::LdBC_d16:
    case OpMode::LdDE_d16:
    case OpMode::LdHL_d16:
    case OpMode::LdSP_d16: {
        const Register16 pair = pairFromOpcode(opcode);
        m_regs.set16(pair, fetchWord());
        break;
    }
    case OpMode::LdBCm_A:
        if (m_memory != nullptr) {
            m_memory->write(m_regs.get16(Register16::BC), m_regs.a);
        }
        break;
    case OpMode::LdA_BCm:
        m_regs.a = m_memory != nullptr ? m_memory->read(m_regs.get16(Register16::BC)) : 0;
        break;
    case OpMode::LdDEm_A:
        if (m_memory != nullptr) {
            m_memory->write(m_regs.get16(Register16::DE), m_regs.a);
        }
        break;
    case OpMode::LdA_DEm:
        m_regs.a = m_memory != nullptr ? m_memory->read(m_regs.get16(Register16::DE)) : 0;
        break;
    case OpMode::IncBC:
    case OpMode::IncDE:
    case OpMode::IncHL:
    case OpMode::IncSP:
    case OpMode::DecBC:
    case OpMode::DecDE:
    case OpMode::DecHL:
    case OpMode::DecSP: {
        const Register16 pair = pairFromOpcode(opcode);
        const bool decrement = (opcode & 0x08) != 0;
        const u16 value = m_regs.get16(pair);
        m_regs.set16(pair, decrement ? static_cast<u16>(value - 1) : static_cast<u16>(value + 1));
        break;
    }
    case OpMode::IncB:
    case OpMode::IncC:
    case OpMode::IncD:
    case OpMode::IncE:
    case OpMode::IncH:
    case OpMode::IncL:
    case OpMode::IncA:
        increment(static_cast<Register8>((opcode >> 3) & 0x07));
        break;
    case OpMode::DecB:
    case OpMode::DecC:
    case OpMode::DecD:
    case OpMode::DecE:
    case OpMode::DecH:
    case OpMode::DecL:
    case OpMode::DecA:
        decrement(static_cast<Register8>((opcode >> 3) & 0x07));
        break;
    case OpMode::LdB_d8:
    case OpMode::LdC_d8:
    case OpMode::LdD_d8:
    case OpMode::LdE_d8:
    case OpMode::LdH_d8:
    case OpMode::LdL_d8:
    case OpMode::LdA_d8: {
        const Register8 reg = static_cast<Register8>((opcode >> 3) & 0x07);
        m_regs.set8(reg, fetchByte());
        break;
    }
    case OpMode::Rlca:
    case OpMode::Rla: {
        bool carry = false;
        m_regs.a = util::rotateLeft(m_regs.a, entry.mode == OpMode::Rla && flags().c, carry);
        Flags f = flags();
        f.z = false;
        f.n = false;
        f.h = false;
        f.c = carry;
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::Rrca:
    case OpMode::Rra: {
        bool carry = false;
        m_regs.a = util::rotateRight(m_regs.a, entry.mode == OpMode::Rra && flags().c, carry);
        Flags f = flags();
        f.z = false;
        f.n = false;
        f.h = false;
        f.c = carry;
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::Lda16m_SP:
        if (m_memory != nullptr) {
            m_memory->writeWord(fetchWord(), m_regs.sp);
        } else {
            (void)fetchWord();
        }
        break;
    case OpMode::AddHL_BC:
    case OpMode::AddHL_DE:
    case OpMode::AddHL_HL:
    case OpMode::AddHL_SP: {
        const Register16 pair = pairFromOpcode(opcode);
        const u16 hl = m_regs.get16(Register16::HL);
        const u16 value = m_regs.get16(pair);
        const u32 result = static_cast<u32>(hl) + static_cast<u32>(value);
        Flags f = flags();
        f.n = false;
        f.h = ((hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
        f.c = result > 0xFFFF;
        m_regs.set16(Register16::HL, static_cast<u16>(result & 0xFFFF));
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::Jr:
        (void)relativeJump(static_cast<s8>(fetchByte()));
        break;
    case OpMode::Jr_NZ:
    case OpMode::Jr_Z:
    case OpMode::Jr_NC:
    case OpMode::Jr_C: {
        const Condition condition = static_cast<Condition>(((opcode >> 3) & 0x01)
                                                           | (((opcode >> 4) & 0x01) << 1));
        const s8 displacement = static_cast<s8>(fetchByte());
        if (checkCondition(condition)) {
            (void)relativeJump(displacement);
            extra = 4;
        }
        break;
    }
    case OpMode::LdHLinc_A:
        if (m_memory != nullptr) {
            m_memory->write(m_regs.get16(Register16::HL), m_regs.a);
        }
        m_regs.set16(Register16::HL, static_cast<u16>(m_regs.get16(Register16::HL) + 1));
        break;
    case OpMode::LdA_HLinc:
        m_regs.a = m_memory != nullptr ? m_memory->read(m_regs.get16(Register16::HL)) : 0;
        m_regs.set16(Register16::HL, static_cast<u16>(m_regs.get16(Register16::HL) + 1));
        break;
    case OpMode::LdHLdec_A:
        if (m_memory != nullptr) {
            m_memory->write(m_regs.get16(Register16::HL), m_regs.a);
        }
        m_regs.set16(Register16::HL, static_cast<u16>(m_regs.get16(Register16::HL) - 1));
        break;
    case OpMode::LdA_HLdec:
        m_regs.a = m_memory != nullptr ? m_memory->read(m_regs.get16(Register16::HL)) : 0;
        m_regs.set16(Register16::HL, static_cast<u16>(m_regs.get16(Register16::HL) - 1));
        break;
    case OpMode::LdHLm_d8:
        if (m_memory != nullptr) {
            m_memory->write(m_regs.get16(Register16::HL), fetchByte());
        } else {
            (void)fetchByte();
        }
        break;
    case OpMode::IncHLm:
        increment(Register8::HL);
        break;
    case OpMode::DecHLm:
        decrement(Register8::HL);
        break;
    case OpMode::Daa: {
        u8 result = m_regs.a;
        Flags f = flags();
        if (!f.n) {
            if (f.c || result > 0x99) {
                result = static_cast<u8>(result + 0x60);
                f.c = true;
            }
            if (f.h || (result & 0x0F) > 0x09) {
                result = static_cast<u8>(result + 0x06);
            }
        } else {
            if (f.c) {
                result = static_cast<u8>(result - 0x60);
            }
            if (f.h) {
                result = static_cast<u8>(result - 0x06);
            }
        }
        f.z = result == 0;
        f.h = false;
        m_regs.a = result;
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::Cpl:
        m_regs.a = static_cast<u8>(~m_regs.a);
        {
            Flags f = flags();
            f.n = true;
            f.h = true;
            m_regs.f = f.toByte();
        }
        break;
    case OpMode::Scf: {
        Flags f = flags();
        f.n = false;
        f.h = false;
        f.c = true;
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::Ccf: {
        Flags f = flags();
        f.n = false;
        f.h = false;
        f.c = !f.c;
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::Halt:
        m_halted = true;
        break;
    case OpMode::Stop:
        m_stopped = true;
        break;
    case OpMode::Prefix:
        break; // Handled by step().
    case OpMode::Ret:
        m_regs.pc = popWord();
        break;
    case OpMode::Ret_NZ:
    case OpMode::Ret_Z:
    case OpMode::Ret_NC:
    case OpMode::Ret_C: {
        const Condition condition = static_cast<Condition>(((opcode >> 3) & 0x01)
                                                           | (((opcode >> 4) & 0x01) << 1));
        if (checkCondition(condition)) {
            m_regs.pc = popWord();
            extra = 12;
        }
        break;
    }
    case OpMode::Reti:
        m_regs.pc = popWord();
        m_ime = true;
        m_imeScheduled = false;
        break;
    case OpMode::Pop_AF: {
        const u16 raw = popWord();
        m_regs.a = highByte(raw);
        m_regs.f = static_cast<u8>(lowByte(raw) & 0xF0);
        break;
    }
    case OpMode::Pop_BC:
    case OpMode::Pop_DE:
    case OpMode::Pop_HL:
        m_regs.set16(pairFromOpcode(opcode), popWord());
        break;
    case OpMode::Jp:
        m_regs.pc = fetchWord();
        break;
    case OpMode::Jp_NZ:
    case OpMode::Jp_Z:
    case OpMode::Jp_NC:
    case OpMode::Jp_C: {
        const Condition condition = static_cast<Condition>(((opcode >> 3) & 0x01)
                                                           | (((opcode >> 4) & 0x01) << 1));
        const u16 target = fetchWord();
        if (checkCondition(condition)) {
            m_regs.pc = target;
            extra = 4;
        }
        break;
    }
    case OpMode::Jp_HL:
        m_regs.pc = m_regs.get16(Register16::HL);
        break;
    case OpMode::Call: {
        const u16 target = fetchWord();
        pushWord(m_regs.pc);
        m_regs.pc = target;
        break;
    }
    case OpMode::Call_NZ:
    case OpMode::Call_Z:
    case OpMode::Call_NC:
    case OpMode::Call_C: {
        const Condition condition = static_cast<Condition>(((opcode >> 3) & 0x01)
                                                           | (((opcode >> 4) & 0x01) << 1));
        const u16 target = fetchWord();
        if (checkCondition(condition)) {
            pushWord(m_regs.pc);
            m_regs.pc = target;
            extra = 12;
        }
        break;
    }
    case OpMode::Push_AF:
        pushWord(makeWord(m_regs.a, static_cast<u8>(m_regs.f & 0xF0)));
        break;
    case OpMode::Push_BC:
    case OpMode::Push_DE:
    case OpMode::Push_HL:
        pushWord(m_regs.get16(pairFromOpcode(opcode)));
        break;
    case OpMode::Add_d8:
        alu(AluOperation::Add, fetchByte());
        break;
    case OpMode::Adc_d8:
        alu(AluOperation::Adc, fetchByte());
        break;
    case OpMode::Sub_d8:
        alu(AluOperation::Sub, fetchByte());
        break;
    case OpMode::Sbc_d8:
        alu(AluOperation::Sbc, fetchByte());
        break;
    case OpMode::And_d8:
        alu(AluOperation::And, fetchByte());
        break;
    case OpMode::Xor_d8:
        alu(AluOperation::Xor, fetchByte());
        break;
    case OpMode::Or_d8:
        alu(AluOperation::Or, fetchByte());
        break;
    case OpMode::Cp_d8:
        alu(AluOperation::Compare, fetchByte());
        break;
    case OpMode::Rst_00:
    case OpMode::Rst_08:
    case OpMode::Rst_10:
    case OpMode::Rst_18:
    case OpMode::Rst_20:
    case OpMode::Rst_28:
    case OpMode::Rst_30:
    case OpMode::Rst_38:
        pushWord(m_regs.pc);
        m_regs.pc = static_cast<u16>(opcode & 0x38);
        break;
    case OpMode::Ldh_a8:
        if (m_memory != nullptr) {
            m_memory->write(static_cast<u16>(0xFF00 + fetchByte()), m_regs.a);
        } else {
            (void)fetchByte();
        }
        break;
    case OpMode::Ldh_A_a8:
        m_regs.a = m_memory != nullptr
                       ? m_memory->read(static_cast<u16>(0xFF00 + fetchByte()))
                       : 0;
        break;
    case OpMode::LdCm_A:
        if (m_memory != nullptr) {
            m_memory->write(static_cast<u16>(0xFF00 + m_regs.c), m_regs.a);
        }
        break;
    case OpMode::LdA_Cm:
        m_regs.a = m_memory != nullptr ? m_memory->read(static_cast<u16>(0xFF00 + m_regs.c)) : 0;
        break;
    case OpMode::Lda16m_A: {
        const u16 target = fetchWord();
        if (m_memory != nullptr) {
            m_memory->write(target, m_regs.a);
        }
        break;
    }
    case OpMode::LdA_a16m: {
        const u16 target = fetchWord();
        m_regs.a = m_memory != nullptr ? m_memory->read(target) : 0;
        break;
    }
    case OpMode::AddSP_r8: {
        const s8 displacement = static_cast<s8>(fetchByte());
        const u16 sp = m_regs.sp;
        const u32 result = static_cast<int>(sp) + static_cast<int>(displacement);
        Flags f;
        f.z = false;
        f.n = false;
        f.h = ((sp & 0x0F) + static_cast<int>(displacement & 0x0F)) > 0x0F;
        f.c = ((sp & 0xFF) + static_cast<int>(displacement & 0xFF)) > 0xFF;
        m_regs.sp = static_cast<u16>(result & 0xFFFF);
        m_regs.f = f.toByte();
        break;
    }
    case OpMode::LdHL_SP_r8: {
        const s8 displacement = static_cast<s8>(fetchByte());
        m_regs.set16(Register16::HL,
                     static_cast<u16>(m_regs.sp + displacement));
        break;
    }
    case OpMode::LdSP_HL:
        m_regs.sp = m_regs.get16(Register16::HL);
        break;
    case OpMode::Di:
        disableInterrupts();
        break;
    case OpMode::Ei:
        enableInterrupts();
        break;
    }
    return cycles + extra;
}

int Opcode::executeCb(u8 opcode)
{
    const Register8 reg = static_cast<Register8>(opcode & 0x07);
    const int operation = (opcode >> 3) & 0x1F;
    u8 value = readRegister(reg);
    Flags f = flags();
    bool carry = false;

    if (operation < 8) {
        switch (operation) {
        case 0: // RLC
            value = util::rotateLeft(value, false, carry);
            break;
        case 1: // RRC
            value = util::rotateRight(value, false, carry);
            break;
        case 2: // RL
            value = util::rotateLeft(value, f.c, carry);
            break;
        case 3: // RR
            value = util::rotateRight(value, f.c, carry);
            break;
        case 4: // SLA
            carry = isBitSet(value, 7);
            value = static_cast<u8>(value << 1);
            break;
        case 5: // SRA
            carry = isBitSet(value, 0);
            value = static_cast<u8>((value >> 1) | (value & 0x80));
            break;
        case 6: // SWAP
            value = static_cast<u8>((value << 4) | (value >> 4));
            break;
        default: // SRL
            carry = isBitSet(value, 0);
            value = static_cast<u8>(value >> 1);
            break;
        }
        f.z = value == 0;
        f.n = false;
        f.h = false;
        f.c = carry;
        writeRegister(reg, value);
    } else if (operation < 16) {
        // BIT n,r
        const int bit = operation - 8;
        f.z = !isBitSet(value, bit);
        f.n = false;
        f.h = true;
    } else if (operation < 24) {
        // RES n,r
        writeRegister(reg, setBit(value, operation - 16, false));
    } else {
        // SET n,r
        writeRegister(reg, setBit(value, operation - 24, true));
    }

    if (operation < 8 || (operation >= 8 && operation < 16)) {
        m_regs.f = f.toByte();
    }
    return cbCycleCount(opcode);
}

} // namespace gb
