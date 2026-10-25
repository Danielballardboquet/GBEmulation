/**
 * @file KeyBindings.h
 * @brief Storage and helpers for the joypad keyboard bindings.
 *
 * Bindings are persisted in QSettings under the "bindings" group, one
 * comma separated list of Qt key codes per joypad button. An empty entry
 * restores the default layout.
 */

#ifndef GB_UI_KEYBINDINGS_H
#define GB_UI_KEYBINDINGS_H

#include <QString>
#include <QVector>

#include <array>

class QSettings;

namespace ui {

/** @brief Number of joypad buttons. */
constexpr int kJoypadButtonCount = 8;

/**
 * @brief Returns the display name of a joypad button.
 * @param index Button index (0 = right ... 7 = start).
 * @return The display name, e.g. "Right" or "A".
 */
QString joypadButtonDisplayName(int index);

/**
 * @brief Returns the QSettings key of a joypad button.
 * @param index Button index (0 = right ... 7 = start).
 * @return The settings key, e.g. "bindings/right".
 */
QString joypadButtonSettingsKey(int index);

/**
 * @brief Returns the default key layout (arrows/WASD, Z/X, Backspace/Enter).
 * @return One list of Qt key codes per button.
 */
std::array<QVector<int>, kJoypadButtonCount> defaultKeyBindings();

/**
 * @brief Loads the bindings from the application settings.
 * @param settings The settings to read.
 * @return The loaded bindings, using the defaults for missing entries.
 */
std::array<QVector<int>, kJoypadButtonCount> loadKeyBindings(const QSettings &settings);

/**
 * @brief Stores the bindings in the application settings.
 * @param settings The settings to write.
 * @param bindings The bindings to store.
 */
void saveKeyBindings(QSettings &settings,
                     const std::array<QVector<int>, kJoypadButtonCount> &bindings);

/**
 * @brief Returns a human readable description of a binding.
 * @param keys The Qt key codes of one button.
 * @return A string like "Right, D" or "A" or "None".
 */
QString describeBinding(const QVector<int> &keys);

// ---------------------------------------------------------------------------
// Global action bindings (pause, fullscreen, exit) - not joypad buttons
// ---------------------------------------------------------------------------

/** @brief Number of global actions bound to the keyboard. */
constexpr int kUiActionCount = 3;

/**
 * @brief Global actions triggered from the screen widget.
 */
enum class UiAction : int {
    Pause = 0,      /**< Start or pause the emulation. */
    Fullscreen = 1, /**< Toggle the full screen mode. */
    Exit = 2        /**< Close the game window. */
};

/**
 * @brief Returns the display name of a global action.
 * @param action The action.
 * @return e.g. "Pause / Resume".
 */
QString uiActionDisplayName(UiAction action);

/**
 * @brief Returns the QSettings key of a global action.
 * @param action The action.
 * @return e.g. "actions/pause" (stored as "ui/actions/pause").
 */
QString uiActionSettingsKey(UiAction action);

/**
 * @brief Returns the default keys of every global action.
 * @return One list of Qt key codes per action.
 */
std::array<QVector<int>, kUiActionCount> defaultUiBindings();

/**
 * @brief Loads the global action bindings from the settings.
 * @param settings The settings to read.
 * @return The loaded bindings, using the defaults for missing entries.
 */
std::array<QVector<int>, kUiActionCount> loadUiBindings(const QSettings &settings);

/**
 * @brief Stores the global action bindings in the settings.
 * @param settings The settings to write.
 * @param bindings The bindings to store.
 */
void saveUiBindings(QSettings &settings,
                    const std::array<QVector<int>, kUiActionCount> &bindings);

} // namespace ui

#endif // GB_UI_KEYBINDINGS_H
