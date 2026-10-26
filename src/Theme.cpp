/**
 * @file Theme.cpp
 * @brief Implementation of the stylesheet discovery and application helpers.
 */

#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

namespace ui {

namespace {

/**
 * @brief Returns the style folder that contains the stylesheets.
 * @return The first existing candidate directory, or an empty string when no
 *         folder exists (the embedded copies are used in that case).
 */
QString styleDirectory()
{
    const QStringList candidates = {
        QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("style")),
        QStringLiteral("style")
    };
    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists()) {
            return candidate;
        }
    }
    return QString();
}

/**
 * @brief Returns the embedded resource path of a stylesheet.
 * @param name Theme name without the .qss extension.
 * @return The ":/style/<name>.qss" path.
 */
QString embeddedThemePath(const QString &name)
{
    return QStringLiteral(":/style/%1.qss").arg(name);
}

/**
 * @brief Reads a stylesheet from the file system or the embedded resources.
 * @param path File system path or ":/..." resource path.
 * @return The stylesheet text, or an empty string when it cannot be read.
 */
QString readStylesheet(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    QFile styleFile(path);
    if (!styleFile.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(styleFile.readAll());
}

} // namespace

QString defaultThemeName()
{
    return QStringLiteral("style");
}

QStringList availableThemes()
{
    // The embedded stylesheets are always present, so they define the list of
    // themes. A loose "style" folder next to the executable can add more.
    QStringList names = {
        QStringLiteral("style"),
        QStringLiteral("dark_cobalt"),
        QStringLiteral("dark_purple"),
        QStringLiteral("gb_classic"),
        QStringLiteral("neon"),
        QStringLiteral("polar_light")
    };

    const QString directory = styleDirectory();
    if (!directory.isEmpty()) {
        const QDir dir(directory);
        const QStringList entries = dir.entryList({QStringLiteral("*.qss")},
                                                  QDir::Files | QDir::Readable,
                                                  QDir::Name);
        for (const QString &entry : entries) {
            const QString name = QFileInfo(entry).completeBaseName();
            if (!names.contains(name)) {
                names.append(name);
            }
        }
    }
    return names;
}

QString themeFilePath(const QString &name)
{
    if (name.isEmpty()) {
        return QString();
    }
    const QString directory = styleDirectory();
    if (!directory.isEmpty()) {
        const QString candidate = QDir(directory).filePath(name + QStringLiteral(".qss"));
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    // Fall back to the copy compiled into the executable.
    const QString embedded = embeddedThemePath(name);
    return QFile::exists(embedded) ? embedded : QString();
}

void applyTheme(const QString &name)
{
    const QString stylesheet = readStylesheet(themeFilePath(name));
    if (!stylesheet.isEmpty()) {
        qApp->setStyleSheet(stylesheet);
    }
}

void applyThemeFile(const QString &path)
{
    const QString stylesheet = readStylesheet(path);
    if (!stylesheet.isEmpty()) {
        qApp->setStyleSheet(stylesheet);
    }
}

void applySavedTheme()
{
    // Always start from the built-in default look, then apply the saved
    // choice. Without this the program opened unstyled when the saved theme
    // file was missing (e.g. a custom style from another machine).
    applyTheme(defaultThemeName());

    QSettings settings;
    const QString value =
        settings.value(QStringLiteral("ui/theme"), defaultThemeName()).toString();
    if (value.isEmpty() || value == defaultThemeName()) {
        return;
    }
    if (availableThemes().contains(value)) {
        applyTheme(value);
    } else {
        applyThemeFile(value);
    }
}

} // namespace ui
