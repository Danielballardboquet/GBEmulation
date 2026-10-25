/**
 * @file DebugPanel.h
 * @brief Dockable panel with the CPU registers, the disassembly around the
 *        program counter, the interrupt state and the cartridge information.
 */

#ifndef GB_UI_DEBUGPANEL_H
#define GB_UI_DEBUGPANEL_H

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QTableWidget;

namespace gb {
class Emulation;
}

namespace ui {

/**
 * @brief Read only debugger view of the emulator state.
 *
 * The panel is refreshed by MainWindow at a low rate (about ten times per
 * second) so it never slows down the emulation thread.
 */
class DebugPanel : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the debug panel.
     * @param parent Parent widget.
     */
    explicit DebugPanel(QWidget *parent = nullptr);

    /**
     * @brief Destroys the debug panel.
     */
    ~DebugPanel() override;

    /**
     * @brief Attaches the emulator that is inspected.
     * @param emulator Pointer to the emulator (may be null).
     */
    void setEmulator(gb::Emulation *emulator);

    /**
     * @brief Rebuilds every label, table and disassembly view.
     */
    void refresh();

private:
    /**
     * @brief Creates the register table with its captions.
     */
    void buildRegisterTable();

    /**
     * @brief Creates the interrupt table with its captions.
     */
    void buildInterruptTable();

    /**
     * @brief Updates the register table from the CPU state.
     */
    void refreshRegisters();

    /**
     * @brief Updates the disassembly view around the program counter.
     */
    void refreshDisassembly();

    /**
     * @brief Updates the interrupt table and the IME / HALT labels.
     */
    void refreshInterrupts();

    /**
     * @brief Updates the cartridge information labels.
     */
    void refreshCartridge();

    /**
     * @brief Returns a styled label for a section title.
     * @param text The title text.
     * @return The configured label.
     */
    QLabel *createSectionLabel(const QString &text);

    gb::Emulation *m_emulator = nullptr; /**< Emulator being inspected. */
    QTableWidget *m_registerTable = nullptr; /**< CPU register table. */
    QTableWidget *m_interruptTable = nullptr; /**< Interrupt table. */
    QLabel *m_cpuStateLabel = nullptr;  /**< IME / HALT / STOP state. */
    QLabel *m_cartridgeLabel = nullptr; /**< Cartridge metadata. */
    QLabel *m_timerLabel = nullptr;     /**< Timer and divider values. */
    QLabel *m_lcdLabel = nullptr;       /**< LCD state. */
    QLabel *m_serialLabel = nullptr;    /**< Serial port output. */
    QPlainTextEdit *m_disassembly = nullptr; /**< Disassembly view. */
};

} // namespace ui

#endif // GB_UI_DEBUGPANEL_H
