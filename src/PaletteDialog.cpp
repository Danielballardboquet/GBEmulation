/**
 * @file PaletteDialog.cpp
 * @brief Implementation of the custom LCD colour editor.
 *
 * The four shades are stored in the same layout as the emulator framebuffer:
 * little-endian RGBA, i.e. R in the low byte and A in byte 3. The helpers
 * convert between that layout and QColor so the colour dialog shows the right
 * colours. A button saves the current palette to a file named "pallettes".
 *
 * @see https://doc.qt.io/qt-6/qcolordialog.html System colour dialog
 */

#include "ui/PaletteDialog.h"

#include <QColor>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include <array>
#include <cstdint>

namespace ui {

namespace {

/** @brief Original DMG green shades (RGBA), lightest first. */
constexpr std::array<QRgb, 4> kDmgColors = {
    0x9BBC0FFF, 0x8BAC0FFF, 0x306230FF, 0x0F380FFF
};

/**
 * @brief Converts a stored RGBA value (R in the high byte) into a QColor.
 */
QColor colorFromArgb(QRgb argb)
{
    return QColor::fromRgb((argb >> 24) & 0xFF, (argb >> 16) & 0xFF,
                           (argb >> 8) & 0xFF, argb & 0xFF);
}

/**
 * @brief Converts a QColor into the stored RGBA layout (R in the high byte).
 */
QRgb colorToArgb(const QColor &color)
{
    return (static_cast<QRgb>(color.red()) << 24)
         | (static_cast<QRgb>(color.green()) << 16)
         | (static_cast<QRgb>(color.blue()) << 8)
         | static_cast<QRgb>(color.alpha());
}

/**
 * @brief Returns the folder where the pallettes file is kept.
 */
QString pallettesFilePath()
{
    const QString folder = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir dir(folder);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(QStringLiteral("pallettes"));
}

} // namespace

PaletteDialog::PaletteDialog(const std::array<QRgb, 4> &colors, QWidget *parent)
    : QDialog(parent)
    , m_colors(colors)
{
    setWindowTitle(tr("Custom Palette"));
    setMinimumWidth(360);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *shadesBox = new QGroupBox(tr("LCD shades (lightest to darkest)"), this);
    QFormLayout *shadesLayout = new QFormLayout(shadesBox);
    shadesLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    const char *const shadeNames[4] = {QT_TRANSLATE_NOOP("PaletteDialog", "Light"),
                                       QT_TRANSLATE_NOOP("PaletteDialog", "Medium light"),
                                       QT_TRANSLATE_NOOP("PaletteDialog", "Medium dark"),
                                       QT_TRANSLATE_NOOP("PaletteDialog", "Dark")};

    for (int index = 0; index < 4; ++index) {
        QPushButton *swatch = new QPushButton(shadesBox);
        swatch->setObjectName(QStringLiteral("colorSwatch"));
        swatch->setMinimumHeight(26);
        shadesLayout->addRow(tr(shadeNames[index]), swatch);
        m_swatches[index] = swatch;
        connect(swatch, &QPushButton::clicked, this, [this, index]() { chooseColor(index); });
    }
    mainLayout->addWidget(shadesBox);

    m_preview = new QPushButton(this);
    m_preview->setObjectName(QStringLiteral("palettePreview"));
    m_preview->setMinimumHeight(46);
    m_preview->setEnabled(false);
    mainLayout->addWidget(m_preview);

    QPushButton *resetButton = new QPushButton(tr("Reset to Game Boy colours"), this);
    connect(resetButton, &QPushButton::clicked, this, &PaletteDialog::resetColors);
    mainLayout->addWidget(resetButton, 0, Qt::AlignLeft);

    QPushButton *saveButton = new QPushButton(tr("Save to pallettes file..."), this);
    connect(saveButton, &QPushButton::clicked, this, &PaletteDialog::saveToFile);
    mainLayout->addWidget(saveButton, 0, Qt::AlignLeft);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PaletteDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PaletteDialog::reject);
    mainLayout->addWidget(buttons);

    refreshSwatches();
}

void PaletteDialog::refreshSwatches()
{
    for (int index = 0; index < 4; ++index) {
        m_swatches[index]->setStyleSheet(
            QStringLiteral("QPushButton#colorSwatch { background-color: %1; }")
                .arg(colorFromArgb(m_colors[index]).name()));
    }
    m_preview->setStyleSheet(
        QStringLiteral("QPushButton#palettePreview {"
                       "background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
                       " stop:0 %1, stop:0.33 %2, stop:0.66 %3, stop:1 %4); }")
            .arg(colorFromArgb(m_colors[0]).name())
            .arg(colorFromArgb(m_colors[1]).name())
            .arg(colorFromArgb(m_colors[2]).name())
            .arg(colorFromArgb(m_colors[3]).name()));
}

void PaletteDialog::chooseColor(int index)
{
    const QColor current = colorFromArgb(m_colors[index]);
    const QColor chosen = QColorDialog::getColor(current, this,
                                                 tr("Choose the shade colour"));
    if (chosen.isValid()) {
        m_colors[index] = colorToArgb(chosen);
        refreshSwatches();
    }
}

void PaletteDialog::resetColors()
{
    m_colors = kDmgColors;
    refreshSwatches();
}

void PaletteDialog::saveToFile()
{
    const QString path = pallettesFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_statusLabel->setText(tr("Could not save the palette to %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    stream << tr("# Game Boy custom palette\n");
    stream << tr("# One RGBA colour per line, lightest first.\n");
    for (const QRgb color : m_colors) {
        stream << QStringLiteral("0x%1").arg(color, 8, 16, QLatin1Char('0')).toUpper()
               << QLatin1Char('\n');
    }
    file.close();
    m_statusLabel->setText(tr("Palette saved to <b>%1</b>").arg(path.toHtmlEscaped()));
}

} // namespace ui
