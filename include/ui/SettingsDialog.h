/**
 * @file SettingsDialog.h
 * @brief Dialog that edits the controller bindings, the global actions, the
 *        theme and the library folders used by the launcher.
 *
 * The Controls tab shows a stylised Game Boy diagram with the current
 * bindings drawn next to each control, the edit buttons for the eight joypad
 * buttons and the three global actions (pause, full screen, exit). The
 * Appearance tab selects the theme, the Library tab manages the folders that
 * are scanned for ROMs.
 */

#ifndef GB_UI_SETTINGSDIALOG_H
#define GB_UI_SETTINGSDIALOG_H

#include "ui/KeyBindings.h"

#include <QDialog>
#include <QPushButton>
#include <QWidget>

#include <array>

class QListWidget;
class QPaintEvent;

namespace ui {

/**
 * @brief Small button that captures the next key press when clicked.
 *
 * Used to assign keyboard keys to joypad buttons or global actions. Clicking
 * the button starts capturing; the next non-modifier key press replaces the
 * assigned keys. Up to two keys are kept.
 */
class KeyCaptureButton : public QPushButton {
    Q_OBJECT

public:
    /**
     * @brief Constructs the capture button.
     * @param description Display label of the binding (for tooltips).
     * @param keys The currently assigned keys.
     * @param parent Parent widget.
     */
    explicit KeyCaptureButton(const QString &description, const QVector<int> &keys,
                              QWidget *parent = nullptr);

    /**
     * @brief Returns the assigned keys.
     */
    const QVector<int> &keys() const { return m_keys; }

    /**
     * @brief Replaces the assigned keys and refreshes the label.
     * @param keys The new keys.
     */
    void setKeys(const QVector<int> &keys);

signals:
    /**
     * @brief Emitted when the assigned keys changed.
     */
    void keysChanged();

protected:
    /**
     * @brief Starts or stops capturing.
     * @param event The mouse press event.
     */
    void mousePressEvent(QMouseEvent *event) override;

    /**
     * @brief Captures the key or aborts the capturing.
     * @param event The key event.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief Stops capturing when the button loses focus.
     * @param event The focus event.
     */
    void focusOutEvent(QFocusEvent *event) override;

private:
    /**
     * @brief Updates the button text from the assigned keys.
     */
    void updateText();

    QString m_description;      /**< Display label of the binding. */
    QVector<int> m_keys;        /**< Assigned Qt key codes. */
    bool m_capturing = false;   /**< True while a key is being captured. */
};

/**
 * @brief Stylised diagram of the Game Boy showing the position of every
 *        control and the keys currently bound to it.
 *
 * The D-pad, the A/B buttons and Start/Select are painted with QPainter and
 * the keyboard binding of each control is drawn next to it in small text.
 */
class GameBoyDiagram : public QWidget {
public:
    /**
     * @brief Constructs the diagram.
     * @param bindings The bindings to annotate (copied).
     * @param parent Parent widget.
     */
    explicit GameBoyDiagram(const std::array<QVector<int>, kJoypadButtonCount> &bindings,
                            QWidget *parent = nullptr);

    /**
     * @brief Replaces the bindings shown on the diagram.
     * @param bindings The new bindings (copied).
     */
    void setBindings(const std::array<QVector<int>, kJoypadButtonCount> &bindings);

    /**
     * @brief Returns the preferred size of the diagram.
     * @return 320 x 240 pixels.
     */
    QSize sizeHint() const override;

protected:
    /**
     * @brief Paints the Game Boy and the binding labels.
     * @param event The paint event.
     */
    void paintEvent(QPaintEvent *event) override;

private:
    std::array<QVector<int>, kJoypadButtonCount> m_bindings; /**< Annotated bindings. */
};

/**
 * @brief Application settings dialog.
 *
 * The dialog edits the persisted settings (joypad bindings, global action
 * bindings, theme, library folders) when it is accepted with OK. Callers apply
 * the theme after the dialog closes.
 */
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the dialog.
     * @param parent Parent widget.
     */
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    /**
     * @brief Adds the folder chosen in a file dialog to the list.
     */
    void addLibraryFolder();

    /**
     * @brief Removes the selected folder from the list.
     */
    void removeLibraryFolder();

    std::array<QVector<int>, kJoypadButtonCount> m_editedBindings;   /**< Joypad bindings being edited. */
    std::array<QVector<int>, kUiActionCount> m_editedUiBindings;     /**< Action bindings being edited. */
    GameBoyDiagram *m_diagram = nullptr;  /**< Diagram annotated with the bindings. */
    QListWidget *m_folderList = nullptr;  /**< Library folders. */
    QPushButton *m_removeFolderButton = nullptr; /**< Remove folder button. */
};

} // namespace ui

#endif // GB_UI_SETTINGSDIALOG_H
