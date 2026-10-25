/**
 * @file DebugPanel.cpp
 * @brief Read only debugger view of the emulator state: CPU registers, the
 *        disassembly around the program counter, the interrupt state and the
 *        cartridge information.
 *
 * The panel is refreshed by MainWindow about ten times per second so it never
 * slows down the emulation thread.
 *
 * @see https://gbdev.io/pandocs/CPU_Registers_and_Flags.html  Register layout
 * @see https://gbdev.io/pandocs/Interrupts.html               IE / IF registers
 * @see https://gbdev.io/pandocs/The_Cartridge_Header.html     Cartridge info
 */

#include "ui/DebugPanel.h"

#include "emulation/Emulation.h"
#include "emulation/GPU.h"
#include "emulation/Interrupt.h"
#include "emulation/Memory.h"
#include "emulation/Opcode.h"
#include "emulation/Types.h"

#include <QFontDatabase>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace ui {

namespace {

/** @brief Number of rows in the CPU register table. */
constexpr int kRegisterRowCount = 14;
/** @brief Number of rows in the interrupt table (one per interrupt source). */
constexpr int kInterruptRowCount = 5;

/**
 * @brief Formats a byte as a two digit uppercase hex string (e.g. "3F").
 * @param value The byte to format.
 * @return The formatted string.
 */
QString hexByte(gb::u8 value)
{
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

/**
 * @brief Formats a word as a four digit uppercase hex string (e.g. "C000").
 * @param value The word to format.
 * @return The formatted string.
 */
QString hexWord(gb::u16 value)
{
    return QStringLiteral("%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
}

} // namespace

DebugPanel::DebugPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setSpacing(4);

    layout->addWidget(createSectionLabel(tr("Registers")));
    buildRegisterTable();
    layout->addWidget(m_registerTable);

    m_cpuStateLabel = new QLabel(tr("IME: -"), content);
    layout->addWidget(m_cpuStateLabel);

    layout->addWidget(createSectionLabel(tr("Disassembly")));
    m_disassembly = new QPlainTextEdit(content);
    m_disassembly->setReadOnly(true);
    m_disassembly->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_disassembly->setMaximumBlockCount(4096);
    layout->addWidget(m_disassembly, 1);

    layout->addWidget(createSectionLabel(tr("Interrupts")));
    buildInterruptTable();
    layout->addWidget(m_interruptTable);

    m_timerLabel = new QLabel(tr("TIMA: -  TMA: -  TAC: -  DIV: -"), content);
    layout->addWidget(m_timerLabel);

    m_lcdLabel = new QLabel(tr("LCD: -"), content);
    layout->addWidget(m_lcdLabel);

    m_serialLabel = new QLabel(tr("Serial: -"), content);
    layout->addWidget(m_serialLabel);

    layout->addWidget(createSectionLabel(tr("Cartridge")));
    m_cartridgeLabel = new QLabel(tr("No cartridge inserted."), content);
    m_cartridgeLabel->setWordWrap(true);
    layout->addWidget(m_cartridgeLabel);

    layout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

DebugPanel::~DebugPanel() = default;

void DebugPanel::setEmulator(gb::Emulation *emulator)
{
    m_emulator = emulator;
    refresh();
}

void DebugPanel::refresh()
{
    if (!m_emulator) {
        m_cpuStateLabel->setText(tr("No emulator attached."));
        m_timerLabel->setText(tr("TIMA: -  TMA: -  TAC: -  DIV: -"));
        m_lcdLabel->setText(tr("LCD: -"));
        m_serialLabel->setText(tr("Serial: -"));
        m_disassembly->clear();
        refreshCartridge();
        return;
    }

    refreshRegisters();
    refreshDisassembly();
    refreshInterrupts();
    refreshCartridge();

    const auto &timer = m_emulator->memory().timer();
    m_timerLabel->setText(tr("TIMA: %1  TMA: %2  TAC: %3  DIV: %4")
                              .arg(hexByte(timer.tima))
                              .arg(hexByte(timer.tma))
                              .arg(hexByte(timer.tac))
                              .arg(hexByte(static_cast<gb::u8>(timer.divider >> 8))));

    const gb::GPU &gpu = m_emulator->gpu();
    m_lcdLabel->setText(tr("LCD: %1  LY: %2  MODE: %3  STAT: %4  Sprites: %5")
                            .arg(gpu.isLcdEnabled() ? tr("ON") : tr("OFF"))
                            .arg(gpu.currentLine())
                            .arg(gpu.mode())
                            .arg(hexByte(gpu.statRegister()))
                            .arg(gpu.spritesOnLine()));

    const std::string &serial = m_emulator->serialOutput();
    m_serialLabel->setText(serial.empty() ? tr("Serial: (empty)")
                                          : tr("Serial: %1").arg(QString::fromStdString(serial)));
}

void DebugPanel::buildRegisterTable()
{
    m_registerTable = new QTableWidget(kRegisterRowCount, 2, this);
    QStringList captions;
    captions << tr("Register") << tr("Value");
    m_registerTable->setHorizontalHeaderLabels(captions);
    m_registerTable->verticalHeader()->setVisible(false);
    m_registerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_registerTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_registerTable->setFocusPolicy(Qt::NoFocus);
    m_registerTable->horizontalHeader()->setStretchLastSection(true);

    const char *names[kRegisterRowCount] = {
        "A", "F", "AF", "B", "C", "BC", "D", "E", "DE", "H", "L", "HL", "SP", "PC"};
    for (int row = 0; row < kRegisterRowCount; ++row) {
        m_registerTable->setItem(row, 0, new QTableWidgetItem(QString::fromLatin1(names[row])));
        m_registerTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("--")));
    }
    m_registerTable->setMaximumHeight((kRegisterRowCount + 1) * m_registerTable->verticalHeader()->defaultSectionSize());
}

void DebugPanel::buildInterruptTable()
{
    m_interruptTable = new QTableWidget(kInterruptRowCount, 3, this);
    QStringList captions;
    captions << tr("Source") << tr("IF") << tr("IE");
    m_interruptTable->setHorizontalHeaderLabels(captions);
    m_interruptTable->verticalHeader()->setVisible(false);
    m_interruptTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_interruptTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_interruptTable->setFocusPolicy(Qt::NoFocus);
    m_interruptTable->horizontalHeader()->setStretchLastSection(true);

    for (int row = 0; row < kInterruptRowCount; ++row) {
        const auto type = static_cast<gb::InterruptType>(row);
        m_interruptTable->setItem(row, 0, new QTableWidgetItem(
            QString::fromLatin1(gb::InterruptController::name(type))));
        m_interruptTable->setItem(row, 1, new QTableWidgetItem(tr("--")));
        m_interruptTable->setItem(row, 2, new QTableWidgetItem(tr("--")));
    }
    m_interruptTable->setMaximumHeight((kInterruptRowCount + 1) * m_interruptTable->verticalHeader()->defaultSectionSize());
}

/**
 * @brief Updates the register table from the CPU state.
 * @see https://gbdev.io/pandocs/CPU_Registers_and_Flags.html Register layout
 */
void DebugPanel::refreshRegisters()
{
    const gb::Registers &regs = m_emulator->cpu().registers();

    m_registerTable->item(0, 1)->setText(hexByte(regs.a));
    m_registerTable->item(1, 1)->setText(hexByte(regs.f));
    m_registerTable->item(2, 1)->setText(hexWord(gb::makeWord(regs.a, regs.f)));
    m_registerTable->item(3, 1)->setText(hexByte(regs.b));
    m_registerTable->item(4, 1)->setText(hexByte(regs.c));
    m_registerTable->item(5, 1)->setText(hexWord(gb::makeWord(regs.b, regs.c)));
    m_registerTable->item(6, 1)->setText(hexByte(regs.d));
    m_registerTable->item(7, 1)->setText(hexByte(regs.e));
    m_registerTable->item(8, 1)->setText(hexWord(gb::makeWord(regs.d, regs.e)));
    m_registerTable->item(9, 1)->setText(hexByte(regs.h));
    m_registerTable->item(10, 1)->setText(hexByte(regs.l));
    m_registerTable->item(11, 1)->setText(hexWord(gb::makeWord(regs.h, regs.l)));
    m_registerTable->item(12, 1)->setText(hexWord(regs.sp));
    m_registerTable->item(13, 1)->setText(hexWord(regs.pc));
}

/**
 * @brief Updates the disassembly view around the program counter.
 * @see https://gbdev.io/pandocs/CPU_Registers_and_Flags.html PC as anchor
 */
void DebugPanel::refreshDisassembly()
{
    const gb::Opcode &cpu = m_emulator->cpu();
    const gb::u16 pc = cpu.registers().pc;

    QString text;
    gb::u16 address = static_cast<gb::u16>(pc > 0x20 ? pc - 0x20 : 0x0000);
    const gb::u16 end = static_cast<gb::u16>(pc + 0x30);
    int guard = 0;
    while (address < end && guard < 128) {
        QString line = QStringLiteral("%1  %2")
                           .arg(address, 4, 16, QLatin1Char('0'))
                           .arg(QString::fromStdString(cpu.disassemble(address)));
        if (address == pc) {
            line.prepend(tr("-> "));
        }
        text += line + QLatin1Char('\n');
        address = static_cast<gb::u16>(address + cpu.instructionLength(address));
        ++guard;
    }
    m_disassembly->setPlainText(text);
}

void DebugPanel::refreshInterrupts()
{
    const gb::InterruptController &interrupts = m_emulator->interrupts();

    for (int row = 0; row < kInterruptRowCount; ++row) {
        const auto type = static_cast<gb::InterruptType>(row);
        m_interruptTable->item(row, 1)->setText(
            interrupts.isRequested(type) ? tr("Yes") : tr("No"));
        m_interruptTable->item(row, 2)->setText(
            interrupts.isEnabled(type) ? tr("Yes") : tr("No"));
    }

    const gb::Opcode &cpu = m_emulator->cpu();
    m_cpuStateLabel->setText(tr("IME: %1  HALT: %2  STOP: %3  EI pending: %4")
                                 .arg(cpu.interruptsEnabled() ? tr("on") : tr("off"))
                                 .arg(cpu.isHalted() ? tr("yes") : tr("no"))
                                 .arg(cpu.isStopped() ? tr("yes") : tr("no"))
                                 .arg(cpu.isEiPending() ? tr("yes") : tr("no")));
}

void DebugPanel::refreshCartridge()
{
    if (!m_emulator->isRomLoaded()) {
        m_cartridgeLabel->setText(tr("No cartridge inserted."));
        return;
    }

    const gb::Cartridge &cart = m_emulator->cartridge();
    const QString title = QString::fromStdString(cart.title).trimmed();
    const QString mapper = QString::fromUtf8(gb::Emulation::mapperName(cart.mapper));
    m_cartridgeLabel->setText(tr("Title: %1\nMapper: %2\nROM banks: %3  RAM banks: %4\nBattery: %5  RTC: %6")
                                  .arg(title.isEmpty() ? tr("(untitled)") : title)
                                  .arg(mapper)
                                  .arg(cart.romBanks)
                                  .arg(cart.ramBanks)
                                  .arg(cart.hasBattery ? tr("yes") : tr("no"))
                                  .arg(cart.hasRtc ? tr("yes") : tr("no")));
}

QLabel *DebugPanel::createSectionLabel(const QString &text)
{
    QLabel *label = new QLabel(text, this);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

} // namespace ui
