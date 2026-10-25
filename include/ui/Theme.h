/**
 * @file Theme.h
 * @brief Stylesheet ("theme") discovery and application helpers.
 *
 * Themes are plain Qt stylesheets stored in the style/ folder next to the
 * executable (or in the working directory as a fallback). A theme is
 * identified by the base name of its .qss file, e.g. "style" or "neon".
 */

#ifndef GB_UI_THEME_H
#define GB_UI_THEME_H

#include <QStringList>

namespace ui {

/**
 * @brief Returns the default theme name.
 * @return "style".
 */
QString defaultThemeName();

/**
 * @brief Returns the names of every stylesheet found in the style folder.
 * @return Sorted base names of the .qss files (e.g. "neon", "style").
 */
QStringList availableThemes();

/**
 * @brief Returns the absolute path of a theme stylesheet.
 * @param name Theme base name.
 * @return The .qss path, or an empty string when the theme does not exist.
 */
QString themeFilePath(const QString &name);

/**
 * @brief Applies a theme to the whole application.
 * @param name Theme base name.
 */
void applyTheme(const QString &name);

/**
 * @brief Applies an arbitrary stylesheet file to the whole application.
 * @param path Path of the .qss file.
 */
void applyThemeFile(const QString &path);

/**
 * @brief Applies the theme stored in the application settings.
 *
 * When the stored value is not the name of a bundled theme it is treated as
 * the path of a custom stylesheet.
 */
void applySavedTheme();

} // namespace ui

#endif // GB_UI_THEME_H
