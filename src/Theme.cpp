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
 * @return The first existing candidate directory.
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
    return candidates.first();
}

} // namespace

QString defaultThemeName()
{
    return QStringLiteral("style");
}

QStringList availableThemes()
{
    const QDir dir(styleDirectory());
    const QStringList entries = dir.entryList({QStringLiteral("*.qss")},
                                              QDir::Files | QDir::Readable,
                                              QDir::Name);
    QStringList names;
    names.reserve(entries.size());
    for (const QString &entry : entries) {
        names.append(QFileInfo(entry).completeBaseName());
    }
    return names;
}

QString themeFilePath(const QString &name)
{
    if (name.isEmpty()) {
        return QString();
    }
    const QString candidate = QDir(styleDirectory()).filePath(name + QStringLiteral(".qss"));
    return QFile::exists(candidate) ? candidate : QString();
}

void applyTheme(const QString &name)
{
    const QString path = themeFilePath(name);
    if (path.isEmpty()) {
        return;
    }
    QFile styleFile(path);
    if (styleFile.open(QIODevice::ReadOnly)) {
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
}

void applyThemeFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QFile styleFile(path);
    if (styleFile.open(QIODevice::ReadOnly)) {
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
}

void applySavedTheme()
{
    QSettings settings;
    const QString value =
        settings.value(QStringLiteral("ui/theme"), defaultThemeName()).toString();
    if (availableThemes().contains(value)) {
        applyTheme(value);
    } else {
        applyThemeFile(value);
    }
}

} // namespace ui
