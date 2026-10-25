/**
 * @file GBScreenWidget.h
 * @brief Qt widget that displays the Game Boy LCD and translates keyboard
 *        input into joypad presses.
 */

#ifndef GB_UI_GBSCREENWIDGET_H
#define GB_UI_GBSCREENWIDGET_H

#include "ui/KeyBindings.h"

#include <QImage>
#include <QWidget>

#include <array>

class QKeyEvent;
class QPaintEvent;
class QResizeEvent;

namespace gb {
class Emulation;
}

namespace ui {

/**
 * @brief Paints the 160x144 framebuffer of the emulator with nearest neighbour
 *        scaling and forwards keyboard events to the joypad.
 *
 * The widget keeps the aspect ratio of the LCD and letterboxes the image when
 * the window is resized. When no ROM is loaded it draws a placeholder message.
 * The keyboard layout is fully configurable through setKeyBindings().
 */
class GBScreenWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the screen widget.
     * @param parent Parent widget.
     */
    explicit GBScreenWidget(QWidget *parent = nullptr);

    /**
     * @brief Destroys the screen widget.
     */
    ~GBScreenWidget() override;

    /**
     * @brief Attaches the emulator whose framebuffer is displayed.
     * @param emulator Pointer to the emulator (may be null).
     */
    void setEmulator(gb::Emulation *emulator);

    /**
     * @brief Copies the current framebuffer of the emulator into the widget.
     */
    void refreshFrame();

    /**
     * @brief Sets the message drawn when no frame is available.
     * @param message The text to display.
     */
    void setPlaceholderText(const QString &message);

    /**
     * @brief Replaces the keyboard layout.
     * @param bindings One list of Qt key codes per joypad button.
     */
    void setKeyBindings(const std::array<QVector<int>, kJoypadButtonCount> &bindings);

    /**
     * @brief Replaces the global action bindings (pause, fullscreen, exit).
     * @param bindings One list of Qt key codes per action.
     */
    void setActionBindings(const std::array<QVector<int>, kUiActionCount> &bindings);

    /**
     * @brief Returns the widget size that gives the best integer scaling.
     * @param availableSize Size available in the parent layout.
     * @return The preferred size, keeping a 10:9 aspect ratio.
     */
    static QSize preferredSize(const QSize &availableSize);

signals:
    /**
     * @brief Emitted when a joypad button is pressed or released.
     * @param index Button index (0 = right ... 7 = start).
     * @param pressed True when the button is held down.
     */
    void buttonChanged(int index, bool pressed);

    /**
     * @brief Emitted when a global action key is pressed.
     * @param action The triggered action.
     */
    void uiActionTriggered(UiAction action);

protected:
    /**
     * @brief Paints the framebuffer or the placeholder text.
     * @param event The paint event.
     */
    void paintEvent(QPaintEvent *event) override;

    /**
     * @brief Recomputes the letterbox rectangle when the widget is resized.
     * @param event The resize event.
     */
    void resizeEvent(QResizeEvent *event) override;

    /**
     * @brief Translates a key press into a joypad press.
     * @param event The key event.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief Translates a key release into a joypad release.
     * @param event The key event.
     */
    void keyReleaseEvent(QKeyEvent *event) override;

    /**
     * @brief Releases every held button when the widget loses focus.
     * @param event The focus event.
     */
    void focusOutEvent(QFocusEvent *event) override;

private:
    /**
     * @brief Maps a Qt key code to a joypad button index.
     * @param key The Qt key code.
     * @return The button index (0..7) or -1 when the key is not mapped.
     */
    int buttonForKey(int key) const;

    /**
     * @brief Maps a Qt key code to a global action.
     * @param key The Qt key code.
     * @return The triggered action or UiActionCount when not mapped.
     */
    UiAction actionForKey(int key) const;

    /**
     * @brief Recomputes the destination rectangle of the LCD image.
     */
    void updateTargetRect();

    gb::Emulation *m_emulator = nullptr; /**< Emulator being displayed. */
    QImage m_image;                      /**< Framebuffer converted to QImage. */
    QRect m_targetRect;                  /**< Destination rectangle of the LCD. */
    QString m_placeholder;               /**< Text shown when no frame exists. */
    bool m_held[8] = {false, false, false, false, false, false, false, false}; /**< Held buttons. */
    std::array<QVector<int>, kJoypadButtonCount> m_keyBindings; /**< Keyboard layout. */
    std::array<QVector<int>, kUiActionCount> m_actionBindings;  /**< Global action keys. */
};

} // namespace ui

#endif // GB_UI_GBSCREENWIDGET_H
