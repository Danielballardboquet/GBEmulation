/**
 * @file SettingsDialog.cpp
 * @brief Implementation of the settings dialog (controls, theme, library).
 *
 * The key capture button grabs the keyboard when clicked and records the next
 * key press; Escape or loosing focus cancels the capture. Up to two keys per
 * binding are supported. The Game Boy diagram is repainted whenever a binding
 * changes so the player always sees the current layout. Nothing is stored
 * until the dialog is accepted with OK.
 *
 * @see https://doc.qt.io/qt-6/qpainter.html QPainter used by the diagram
 */

#include "ui/SettingsDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

namespace {

/** @brief Maximum number of keys gathered by the capture button. */
constexpr int kMaxKeysPerButton = 2;

/**
 * @brief Returns true when a Qt key should be accepted as a key binding.
 * @param key The Qt key code.
 */
bool isUsableKey(int key)
{
    if (key == Qt::Key_Escape || key == Qt::Key_Shift || key == Qt::Key_Control
        || key == Qt::Key_Alt || key == Qt::Key_Meta || key == Qt::Key_CapsLock
        || key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_unknown) {
        return false;
    }
    return key > 0;
}

/**
 * @brief Joins up to two keys into one short label for the diagram.
 * @param keys The assigned keys.
 * @return e.g. "Z" or "Z/X" or "None".
 */
QString shortBinding(const QVector<int> &keys)
{
    QStringList names;
    for (const int key : keys) {
        names.append(QKeySequence(key).toString(QKeySequence::NativeText));
        if (names.size() >= 2) {
            break;
        }
    }
    return names.join(QStringLiteral("/"));
}

} // namespace

// ---------------------------------------------------------------------------
// KeyCaptureButton
// ---------------------------------------------------------------------------

KeyCaptureButton::KeyCaptureButton(const QString &description, const QVector<int> &keys,
                                   QWidget *parent)
    : QPushButton(parent)
    , m_description(description)
    , m_keys(keys)
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(150);
    updateText();
}

void KeyCaptureButton::setKeys(const QVector<int> &keys)
{
    m_keys = keys;
    updateText();
}

void KeyCaptureButton::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_capturing = !m_capturing;
    if (m_capturing) {
        setText(tr("Press a key..."));
        grabKeyboard();
    } else {
        updateText();
        releaseKeyboard();
    }
}

void KeyCaptureButton::keyPressEvent(QKeyEvent *event)
{
    if (!m_capturing) {
        QPushButton::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        m_capturing = false;
        releaseKeyboard();
        updateText();
        event->accept();
        return;
    }

    if (isUsableKey(event->key())) {
        QVector<int> keys = m_keys;
        keys.removeAll(event->key());
        if (keys.size() >= kMaxKeysPerButton) {
            keys.pop_back();
        }
        keys.append(event->key());
        m_keys = keys;
        emit keysChanged();
        updateText();
    }
    event->accept();
}

void KeyCaptureButton::focusOutEvent(QFocusEvent *event)
{
    if (m_capturing) {
        m_capturing = false;
        releaseKeyboard();
        updateText();
    }
    QPushButton::focusOutEvent(event);
}

void KeyCaptureButton::updateText()
{
    QString keyName = describeBinding(m_keys);
    setText(keyName == tr("None") ? tr("Click to assign") : keyName);
    setToolTip(tr("Click, then press a key to bind it to %1.")
                   .arg(m_description));
}

// ---------------------------------------------------------------------------
// GameBoyDiagram
// ---------------------------------------------------------------------------

GameBoyDiagram::GameBoyDiagram(
    const std::array<QVector<int>, kJoypadButtonCount> &bindings, QWidget *parent)
    : QWidget(parent)
    , m_bindings(bindings)
{
    setMinimumSize(320, 238);
}

void GameBoyDiagram::setBindings(
    const std::array<QVector<int>, kJoypadButtonCount> &bindings)
{
    m_bindings = bindings;
    update();
}

QSize GameBoyDiagram::sizeHint() const
{
    return QSize(380, 226);
}

void GameBoyDiagram::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect area = rect().adjusted(6, 6, -6, -6);

    // ---- Legend column on the right: name + key binding per control ----
    const QRect legendRect(area.x() + 222, area.y(), area.width() - 222, area.height());
    painter.setPen(QPen(QColor(0x9A, 0x9A, 0xA4), 1));
    painter.setBrush(QColor(0xF1, 0xF1, 0xF5));
    painter.drawRoundedRect(legendRect, 8, 8);

    QFont headerFont = font();
    headerFont.setBold(true);
    headerFont.setPixelSize(10);
    painter.setFont(headerFont);
    painter.setPen(QColor(0x33, 0x33, 0x3C));
    painter.drawText(legendRect.adjusted(0, 10, 0, -10), Qt::AlignHCenter | Qt::AlignTop,
                     tr("KEY BINDINGS"));

    const char *const names[8] = {"Right", "Left", "Up", "Down",
                                  "A", "B", "Select", "Start"};
    const int rowTop = legendRect.y() + 34;
    const int rowHeight = (legendRect.height() - 40) / 8;

    QFont rowFont = font();
    rowFont.setPixelSize(9);
    painter.setFont(rowFont);

    for (int row = 0; row < 8; ++row) {
        const int y = rowTop + row * rowHeight;
        painter.setPen(QColor(0x4C, 0x4C, 0x56));
        painter.drawText(QRect(legendRect.x() + 8, y, 58, rowHeight),
                         Qt::AlignVCenter | Qt::AlignLeft, QLatin1String(names[row]));
        painter.setPen(QColor(0x8A, 0x2F, 0x55));
        painter.drawText(QRect(legendRect.x() + 66, y, legendRect.width() - 76, rowHeight),
                         Qt::AlignVCenter | Qt::AlignRight, shortBinding(m_bindings[row]));
    }

    // ---- Console body of the Game Boy ----
    const QRect body(area.x(), area.y() + 6, 206, 208);
    painter.setPen(QPen(QColor(0x8A, 0x8A, 0x94), 2));
    painter.setBrush(QColor(0xB8, 0xB8, 0xC4));
    painter.drawRoundedRect(body, 18, 18);

    const QFont oldFont = painter.font();

    // Brand badge above the screen.
    QFont badgeFont = oldFont;
    badgeFont.setPixelSize(9);
    painter.setFont(badgeFont);
    painter.setPen(QColor(0x66, 0x66, 0x70));
    painter.drawText(QRect(body.x() + 14, body.y() + 6, 178, 12), Qt::AlignCenter,
                     QStringLiteral("GAME BOY"));
    painter.setFont(oldFont);

    // LCD screen glass.
    painter.setPen(QPen(QColor(0x3A, 0x3A, 0x44), 2));
    painter.setBrush(QColor(0x7E, 0x8F, 0x5E));
    painter.drawRoundedRect(QRect(body.x() + 18, body.y() + 20, 170, 100), 6, 6);
    painter.setBrush(QColor(0x5A, 0x6B, 0x40));
    painter.drawRoundedRect(QRect(body.x() + 26, body.y() + 28, 154, 84), 5, 5);

    QFont subFont = oldFont;
    subFont.setPixelSize(7);
    painter.setFont(subFont);
    painter.setPen(QColor(0x33, 0x33, 0x3C));
    painter.drawText(QRect(body.x() + 15, body.y() + 122, 176, 10), Qt::AlignCenter,
                     QStringLiteral("DOT MATRIX WITH STEREO SOUND"));
    painter.setFont(oldFont);

    // D-pad cross.
    const QColor padColor(0x4C, 0x4C, 0x56);
    const int padCx = body.x() + 56;
    const int padCy = body.y() + 172;
    painter.setPen(QPen(QColor(0x33, 0x33, 0x3C), 1));
    painter.setBrush(padColor);
    painter.drawRoundedRect(QRect(padCx - 8, padCy - 24, 16, 48), 4, 4);   // vertical arm
    painter.drawRoundedRect(QRect(padCx - 25, padCy - 7, 50, 16), 4, 4);   // horizontal arm
    painter.setBrush(QColor(0x3A, 0x3A, 0x44));
    painter.drawRoundedRect(QRect(padCx - 4, padCy - 4, 8, 8), 2, 2);

    // Arrow glyphs on the D-pad arms.
    const int tip = 10;
    const int base = 4;
    painter.setPen(QPen(QColor(0xFF, 0xFF, 0xFF), 1));
    painter.setBrush(QColor(0xFF, 0xFF, 0xFF));
    painter.drawPolygon(QPolygon() << QPoint(padCx + tip, padCy)
                                   << QPoint(padCx + tip - base, padCy - base)
                                   << QPoint(padCx + tip - base, padCy + base)); // right
    painter.drawPolygon(QPolygon() << QPoint(padCx - tip, padCy)
                                   << QPoint(padCx - tip + base, padCy - base)
                                   << QPoint(padCx - tip + base, padCy + base)); // left
    painter.drawPolygon(QPolygon() << QPoint(padCx, padCy - tip)
                                   << QPoint(padCx - base, padCy - tip + base)
                                   << QPoint(padCx + base, padCy - tip + base)); // up
    painter.drawPolygon(QPolygon() << QPoint(padCx, padCy + tip)
                                   << QPoint(padCx - base, padCy + tip - base)
                                   << QPoint(padCx + base, padCy + tip - base)); // down

    // Direction letters on the arrows.
    QFont dirFont = oldFont;
    dirFont.setPixelSize(8);
    painter.setFont(dirFont);
    painter.drawText(QRect(padCx + 4, padCy - 7, 13, 14), Qt::AlignCenter, tr("R"));
    painter.drawText(QRect(padCx - 17, padCy - 7, 13, 14), Qt::AlignCenter, tr("L"));
    painter.drawText(QRect(padCx - 7, padCy - 22, 14, 15), Qt::AlignCenter, tr("U"));
    painter.drawText(QRect(padCx - 7, padCy + 7, 14, 15), Qt::AlignCenter, tr("D"));
    painter.setFont(oldFont);

    // A and B buttons: B on the left, A on the right, like the real hardware.
    const QColor aColor(0xC0, 0x48, 0x6C);
    const QColor bColor(0x90, 0x48, 0xC0);
    const int bX = body.x() + 142;
    const int bY = body.y() + 164;
    const int aX = body.x() + 162;
    const int aY = body.y() + 150;
    painter.setPen(QPen(QColor(0x33, 0x33, 0x3C), 1));
    painter.setBrush(bColor);
    painter.drawEllipse(QPoint(bX, bY), 11, 11);
    painter.setBrush(aColor);
    painter.drawEllipse(QPoint(aX, aY), 11, 11);

    QFont letterFont = oldFont;
    letterFont.setBold(true);
    letterFont.setPixelSize(10);
    painter.setFont(letterFont);
    painter.setPen(QColor(0xFF, 0xFF, 0xFF));
    painter.drawText(bX - 7, bY - 7, 14, 14, Qt::AlignCenter, QStringLiteral("B"));
    painter.drawText(aX - 7, aY - 7, 14, 14, Qt::AlignCenter, QStringLiteral("A"));
    painter.setFont(oldFont);

    // Start (left pill) and Select (right pill).
    painter.setBrush(padColor);
    painter.drawRoundedRect(QRect(body.x() + 78, body.y() + 186, 30, 8), 3, 3);
    painter.drawRoundedRect(QRect(body.x() + 112, body.y() + 186, 30, 8), 3, 3);

    QFont pillFont = oldFont;
    pillFont.setPixelSize(6);
    painter.setFont(pillFont);
    painter.setPen(QColor(0x33, 0x33, 0x3C));
    painter.drawText(QRect(body.x() + 72, body.y() + 178, 42, 8), Qt::AlignCenter,
                     QStringLiteral("START"));
    painter.drawText(QRect(body.x() + 106, body.y() + 178, 42, 8), Qt::AlignCenter,
                     QStringLiteral("SELECT"));
    painter.setFont(oldFont);
}

// ---------------------------------------------------------------------------
// SettingsDialog
// ---------------------------------------------------------------------------

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(500);

    QTabWidget *tabs = new QTabWidget(this);

    // --- Controls page ---
    QSettings bareSettings;
    const auto bindings = loadKeyBindings(bareSettings);
    const auto uiBindings = loadUiBindings(bareSettings);
    m_editedBindings = bindings;
    m_editedUiBindings = uiBindings;

    QWidget *controlsPage = new QWidget(this);
    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsPage);

    QGroupBox *diagramBox = new QGroupBox(tr("Controller layout"), controlsPage);
    QVBoxLayout *diagramLayout = new QVBoxLayout(diagramBox);
    m_diagram = new GameBoyDiagram(bindings, diagramBox);
    diagramLayout->addWidget(m_diagram, 0, Qt::AlignHCenter);
    QLabel *diagramHint = new QLabel(
        tr("The labels show which key is bound to every control. Click a button "
           "below, then press a key to change it."),
        diagramBox);
    diagramHint->setWordWrap(true);
    diagramLayout->addWidget(diagramHint);
    controlsLayout->addWidget(diagramBox);

    QGroupBox *joypadBox = new QGroupBox(tr("Joypad buttons"), controlsPage);
    QFormLayout *joypadLayout = new QFormLayout(joypadBox);
    joypadLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    for (int index = 0; index < kJoypadButtonCount; ++index) {
        KeyCaptureButton *button = new KeyCaptureButton(
            joypadButtonDisplayName(index), bindings[index], joypadBox);
        connect(button, &KeyCaptureButton::keysChanged, this, [this, index, button]() {
            m_editedBindings[index] = button->keys();
            m_diagram->setBindings(m_editedBindings);
        });
        joypadLayout->addRow(joypadButtonDisplayName(index), button);
    }
    controlsLayout->addWidget(joypadBox);

    QGroupBox *actionBox = new QGroupBox(tr("Global actions"), controlsPage);
    QFormLayout *actionLayout = new QFormLayout(actionBox);
    actionLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    for (int index = 0; index < kUiActionCount; ++index) {
        const UiAction action = static_cast<UiAction>(index);
        KeyCaptureButton *button = new KeyCaptureButton(
            uiActionDisplayName(action), uiBindings[index], actionBox);
        connect(button, &KeyCaptureButton::keysChanged, this,
                [this, index, button]() { m_editedUiBindings[index] = button->keys(); });
        actionLayout->addRow(uiActionDisplayName(action), button);
    }
    QLabel *actionHint = new QLabel(
        tr("Pause / Resume toggles the emulation, Full Screen toggles the "
           "full screen mode and Exit closes the game window."),
        actionBox);
    actionHint->setWordWrap(true);
    actionLayout->addRow(QString(), actionHint);
    controlsLayout->addWidget(actionBox);
    controlsLayout->addStretch(1);

    // --- Library page ---
    QWidget *libraryPage = new QWidget(this);
    QVBoxLayout *libraryLayout = new QVBoxLayout(libraryPage);
    m_folderList = new QListWidget(libraryPage);
    m_folderList->addItems(
        bareSettings.value(QStringLiteral("library/folders")).toStringList());
    libraryLayout->addWidget(m_folderList);

    QHBoxLayout *folderButtons = new QHBoxLayout(libraryPage);
    QPushButton *addFolderButton = new QPushButton(tr("Add folder..."), libraryPage);
    m_removeFolderButton = new QPushButton(tr("Remove"), libraryPage);
    m_removeFolderButton->setEnabled(false);
    folderButtons->addWidget(addFolderButton);
    folderButtons->addWidget(m_removeFolderButton);
    folderButtons->addStretch(1);
    libraryLayout->addLayout(folderButtons);

    QLabel *libraryHint = new QLabel(
        tr("Folders are scanned recursively for .gb and .gbc files. The ROM "
           "folder next to the executable is always shown as well."),
        libraryPage);
    libraryHint->setWordWrap(true);
    libraryLayout->addWidget(libraryHint);

    tabs->addTab(controlsPage, tr("Controls"));
    tabs->addTab(libraryPage, tr("Library"));

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        QSettings settings;
        saveKeyBindings(settings, m_editedBindings);
        saveUiBindings(settings, m_editedUiBindings);
        QStringList folders;
        folders.reserve(m_folderList->count());
        for (int row = 0; row < m_folderList->count(); ++row) {
            folders.append(m_folderList->item(row)->text());
        }
        settings.setValue(QStringLiteral("library/folders"), folders);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tabs);
    mainLayout->addWidget(buttons);

    connect(addFolderButton, &QPushButton::clicked, this, &SettingsDialog::addLibraryFolder);
    connect(m_removeFolderButton, &QPushButton::clicked, this,
            &SettingsDialog::removeLibraryFolder);
    connect(m_folderList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeFolderButton->setEnabled(!m_folderList->selectedItems().isEmpty());
    });
}

void SettingsDialog::addLibraryFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Choose a ROM folder"), QString());
    if (folder.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_folderList->count(); ++row) {
        if (m_folderList->item(row)->text() == folder) {
            return; // already present
        }
    }
    m_folderList->addItem(folder);
}

void SettingsDialog::removeLibraryFolder()
{
    const int row = m_folderList->currentRow();
    if (row >= 0) {
        delete m_folderList->takeItem(row);
    }
}

} // namespace ui
