/**
 * @file PaletteDialog.h
 * @brief Dialog that edits the four custom LCD shades.
 *
 * Each of the four Game Boy shades has a colour swatch button; clicking one
 * opens the system colour dialog. The preview strip shows the resulting
 * gradient immediately, and the palette can be reset to the original DMG
 * colours with one button.
 */

#ifndef GB_UI_PALETTEDIALOG_H
#define GB_UI_PALETTEDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QtGui/QRgb>

#include <array>

class QPushButton;

namespace ui {

/**
 * @brief Editor for the four custom LCD colours.
 */
class PaletteDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the dialog with the colours currently stored.
     * @param colors The four shades, index 0 = lightest, stored as RRGGBBAA.
     * @param parent Parent widget.
     */
    explicit PaletteDialog(const std::array<QRgb, 4> &colors, QWidget *parent = nullptr);

    /**
     * @brief Returns the edited colours.
     * @return The four shades, index 0 = lightest, as RRGGBBAA.
     */
    std::array<QRgb, 4> colors() const { return m_colors; }

private:
    /**
     * @brief Redraws the swatches and the preview strip.
     */
    void refreshSwatches();

    /**
     * @brief Opens the colour dialog for one shade.
     * @param index Shade index (0 = lightest).
     */
    void chooseColor(int index);

    /**
     * @brief Restores the original DMG green shades.
     */
    void resetColors();

    /**
     * @brief Writes the four shades to the file "pallettes".
     */
    void saveToFile();

    std::array<QRgb, 4> m_colors;              /**< Edited shades (RGBA). */
    std::array<QPushButton *, 4> m_swatches = {nullptr, nullptr, nullptr, nullptr}; /**< Swatch buttons. */
    QPushButton *m_preview = nullptr;          /**< Gradient preview strip. */
    QLabel *m_statusLabel = nullptr;           /**< Feedback label after saving. */
};

} // namespace ui

#endif // GB_UI_PALETTEDIALOG_H
