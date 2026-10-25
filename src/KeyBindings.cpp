/**
 * @file KeyBindings.cpp
 * @brief Implementation of the joypad key binding helpers.
 */

#include "ui/KeyBindings.h"

#include <QKeySequence>
#include <QSettings>
#include <Qt>

namespace ui {

namespace {

/**
 * @brief Settings key component of each joypad button.
 */
const char *const kButtonKeys[kJoypadButtonCount] = {
    "right", "left", "up", "down", "a", "b", "select", "start"
};

/**
 * @brief Display name of each joypad button.
 */
const char *const kButtonNames[kJoypadButtonCount] = {
    "Right", "Left", "Up", "Down", "A", "B", "Select", "Start"
};

} // namespace

QString joypadButtonDisplayName(int index)
{
    if (index < 0 || index >= kJoypadButtonCount) {
        return QString();
    }
    return QLatin1String(kButtonNames[index]);
}

QString joypadButtonSettingsKey(int index)
{
    if (index < 0 || index >= kJoypadButtonCount) {
        return QString();
    }
    return QStringLiteral("bindings/") + QLatin1String(kButtonKeys[index]);
}

std::array<QVector<int>, kJoypadButtonCount> defaultKeyBindings()
{
    return {{
        {Qt::Key_Right, Qt::Key_D},            // right
        {Qt::Key_Left, Qt::Key_A},             // left
        {Qt::Key_Up, Qt::Key_W},               // up
        {Qt::Key_Down, Qt::Key_S},             // down
        {Qt::Key_Z},                           // A button
        {Qt::Key_X},                           // B button
        {Qt::Key_Backspace},                   // select
        {Qt::Key_Return, Qt::Key_Enter}        // start
    }};
}

std::array<QVector<int>, kJoypadButtonCount> loadKeyBindings(const QSettings &settings)
{
    const auto defaults = defaultKeyBindings();
    std::array<QVector<int>, kJoypadButtonCount> bindings = defaults;

    for (int index = 0; index < kJoypadButtonCount; ++index) {
        const QStringList parts = settings.value(joypadButtonSettingsKey(index))
                                      .toString()
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        QVector<int> keys;
        keys.reserve(parts.size());
        bool ok = false;
        for (const QString &part : parts) {
            const int key = part.trimmed().toInt(&ok);
            if (ok && key > 0) {
                keys.append(key);
            } else {
                keys.clear();
                break;
            }
        }
        if (!keys.isEmpty()) {
            bindings[index] = keys;
        }
    }
    return bindings;
}

void saveKeyBindings(QSettings &settings,
                     const std::array<QVector<int>, kJoypadButtonCount> &bindings)
{
    for (int index = 0; index < kJoypadButtonCount; ++index) {
        QStringList parts;
        parts.reserve(bindings[index].size());
        for (const int key : bindings[index]) {
            parts.append(QString::number(key));
        }
        settings.setValue(joypadButtonSettingsKey(index), parts.join(QLatin1Char(',')));
    }
}

QString describeBinding(const QVector<int> &keys)
{
    if (keys.isEmpty()) {
        return QObject::tr("None");
    }
    QStringList names;
    names.reserve(keys.size());
    for (const int key : keys) {
        names.append(QKeySequence(key).toString(QKeySequence::NativeText));
    }
    return names.join(QStringLiteral(", "));
}

// ---------------------------------------------------------------------------
// Global action bindings
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Settings key component of each global action.
 */
const char *const kActionKeys[kUiActionCount] = {
    "pause", "fullscreen", "exit"
};

/**
 * @brief Display name of each global action.
 */
const char *const kActionNames[kUiActionCount] = {
    "Pause / Resume", "Full Screen", "Exit"
};

} // namespace

QString uiActionDisplayName(UiAction action)
{
    const int index = static_cast<int>(action);
    if (index < 0 || index >= kUiActionCount) {
        return QString();
    }
    return QLatin1String(kActionNames[index]);
}

QString uiActionSettingsKey(UiAction action)
{
    const int index = static_cast<int>(action);
    if (index < 0 || index >= kUiActionCount) {
        return QString();
    }
    return QStringLiteral("ui/actions/") + QLatin1String(kActionKeys[index]);
}

std::array<QVector<int>, kUiActionCount> defaultUiBindings()
{
    return {{
        {Qt::Key_P, Qt::Key_Space},   // pause / resume
        {Qt::Key_F11},                // full screen
        {Qt::Key_Escape}              // exit
    }};
}

std::array<QVector<int>, kUiActionCount> loadUiBindings(const QSettings &settings)
{
    const auto defaults = defaultUiBindings();
    std::array<QVector<int>, kUiActionCount> bindings = defaults;

    for (int index = 0; index < kUiActionCount; ++index) {
        const QStringList parts = settings.value(uiActionSettingsKey(
                                                      static_cast<UiAction>(index)))
                                      .toString()
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        QVector<int> keys;
        keys.reserve(parts.size());
        bool ok = false;
        for (const QString &part : parts) {
            const int key = part.trimmed().toInt(&ok);
            if (ok && key > 0) {
                keys.append(key);
            } else {
                keys.clear();
                break;
            }
        }
        if (!keys.isEmpty()) {
            bindings[index] = keys;
        }
    }
    return bindings;
}

void saveUiBindings(QSettings &settings,
                    const std::array<QVector<int>, kUiActionCount> &bindings)
{
    for (int index = 0; index < kUiActionCount; ++index) {
        QStringList parts;
        parts.reserve(bindings[index].size());
        for (const int key : bindings[index]) {
            parts.append(QString::number(key));
        }
        settings.setValue(uiActionSettingsKey(static_cast<UiAction>(index)),
                          parts.join(QLatin1Char(',')));
    }
}

} // namespace ui
