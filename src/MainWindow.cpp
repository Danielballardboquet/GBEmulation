/**
 * @file MainWindow.cpp
 * @brief Implementation of the launcher window with the ROM library.
 *
 * The window scans the configured library folders (recursively) for .gb and
 * .gbc files and shows them in a list with a short description parsed from the
 * cartridge header. Starting a game opens a separate GameWindow.
 *
 * @see https://gbdev.io/pandocs/The_Cartridge_Header.html  ROM header layout
 */

#include "ui/MainWindow.h"

#include "ui/GameWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace ui {

namespace {

/** @brief Maximum number of ROM paths remembered in the recent files menu. */
constexpr int kMaxRecentFiles = 8;

/** @brief Offsets of the fields parsed from the cartridge header. */
constexpr int kHeaderTitleOffset = 0x134;   /**< Cartridge title. */
constexpr int kHeaderTitleLength = 11;      /**< Length of the title field. */
constexpr int kHeaderCartTypeOffset = 0x147; /**< Cartridge type byte. */
constexpr int kHeaderRomSizeOffset = 0x148;  /**< ROM size byte. */

/**
 * @brief Returns the name of a cartridge type byte (0x147).
 * @param cartType The cartridge type byte.
 * @return A short description, e.g. "MBC3 + Battery".
 */
QString cartTypeName(std::uint8_t cartType)
{
    switch (cartType) {
    case 0x00:
        return QStringLiteral("ROM only");
    case 0x01:
        return QStringLiteral("MBC1");
    case 0x02:
        return QStringLiteral("MBC1 + RAM");
    case 0x03:
        return QStringLiteral("MBC1 + RAM + Battery");
    case 0x05:
        return QStringLiteral("MBC2");
    case 0x06:
        return QStringLiteral("MBC2 + Battery");
    case 0x08:
        return QStringLiteral("ROM + RAM");
    case 0x09:
        return QStringLiteral("ROM + RAM + Battery");
    case 0x0F:
        return QStringLiteral("MBC3 + Timer + Battery");
    case 0x10:
        return QStringLiteral("MBC3 + Timer + RAM + Battery");
    case 0x11:
        return QStringLiteral("MBC3");
    case 0x12:
        return QStringLiteral("MBC3 + RAM");
    case 0x13:
        return QStringLiteral("MBC3 + RAM + Battery");
    case 0x19:
        return QStringLiteral("MBC5");
    case 0x1A:
        return QStringLiteral("MBC5 + RAM");
    case 0x1B:
        return QStringLiteral("MBC5 + RAM + Battery");
    case 0x1C:
        return QStringLiteral("MBC5 + Rumble");
    case 0x1D:
        return QStringLiteral("MBC5 + Rumble + RAM");
    case 0x1E:
        return QStringLiteral("MBC5 + Rumble + RAM + Battery");
    default:
        return QObject::tr("Cart type 0x%1").arg(cartType, 2, 16, QLatin1Char('0'));
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createActions();
    createLayout();

    loadRecentFiles();
    updateRecentFilesMenu();

    applySavedTheme();

    setWindowTitle(tr("Game Boy Library"));
    resize(720, 520);

    scanLibrary();
}

MainWindow::~MainWindow() = default;

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateRecentFilesMenu();
    scanLibrary();
}

void MainWindow::startSelectedGame()
{
    const QListWidgetItem *item = m_romList->currentItem();
    if (!item) {
        statusBar()->showMessage(tr("Select a game first"), 2500);
        return;
    }
    startGame(item->data(Qt::UserRole).toString());
}

void MainWindow::startGame(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    // A new GameWindow is created for every game, so several games can run at
    // the same time. WA_DeleteOnClose (set by GameWindow) releases it when the
    // player closes the window.
    GameWindow *window = new GameWindow(path);
    window->show();
    addRecentFile(path);
    saveRecentFiles();
    updateRecentFilesMenu();
}

void MainWindow::chooseRom()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Game Boy ROM"),
        QString(),
        tr("Game Boy ROM (*.gb *.gbc);;All files (*)"));
    if (!path.isEmpty()) {
        startGame(path);
    }
}

void MainWindow::refreshLibrary()
{
    scanLibrary();
    statusBar()->showMessage(tr("Library refreshed"), 2000);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
    applySavedTheme();
    scanLibrary();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("About Game Boy Library"),
        tr("<b>Game Boy Emulator</b> v%1<br><br>"
           "A Game Boy (DMG/CGB) emulator written in C++ with a Qt 6 "
           "interface.<br><br>"
           "Select a game and press Start, or double click an entry. The game "
           "opens in its own window; the library keeps running so you can "
           "start another one. Configure the controls and the theme in "
           "Settings.")
            .arg(qApp->applicationVersion()));
}

void MainWindow::updateDetail()
{
    const QListWidgetItem *item = m_romList->currentItem();
    if (!item) {
        m_detailLabel->setText(tr("Select a game to see its details."));
        return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    const QFileInfo info(path);
    const qint64 sizeKiB = (info.size() + 1023) / 1024;
    m_detailLabel->setText(tr("<b>%1</b><br>%2<br>%3 KiB &middot; %4")
                               .arg(info.fileName())
                               .arg(romDescription(path))
                               .arg(sizeKiB)
                               .arg(QDir::toNativeSeparators(info.absolutePath())));
}

void MainWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }
    const QString path = action->data().toString();
    if (!path.isEmpty() && QFile::exists(path)) {
        startGame(path);
    }
}

void MainWindow::createActions()
{
    m_startAction = new QAction(tr("&Start"), this);
    m_startAction->setShortcut(QKeySequence(Qt::Key_Return));
    m_startAction->setStatusTip(tr("Play the selected game in a new window"));
    connect(m_startAction, &QAction::triggered, this, &MainWindow::startSelectedGame);

    m_openAction = new QAction(tr("&Open ROM File..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Start a game that is not in the library"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::chooseRom);

    m_refreshAction = new QAction(tr("&Refresh Library"), this);
    m_refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_refreshAction->setStatusTip(tr("Rescan the library folders"));
    connect(m_refreshAction, &QAction::triggered, this, &MainWindow::refreshLibrary);

    m_settingsAction = new QAction(tr("&Settings..."), this);
    m_settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    m_settingsAction->setStatusTip(tr("Controller bindings, theme and library"));
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    m_aboutAction = new QAction(tr("&About..."), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    QAction *quitAction = new QAction(tr("&Quit"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    m_recentMenu = fileMenu->addMenu(tr("Recent &Games"));
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    QMenu *libraryMenu = menuBar()->addMenu(tr("&Library"));
    libraryMenu->addAction(m_startAction);
    libraryMenu->addAction(m_refreshAction);

    // The style selection lives in the Settings menu, like in the game window.
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    m_styleMenu = settingsMenu->addMenu(tr("&Style"));
    buildStyleMenu();
    settingsMenu->addSeparator();
    settingsMenu->addAction(m_settingsAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::buildStyleMenu()
{
    if (!m_styleMenu) {
        return;
    }
    m_styleMenu->clear();
    if (m_styleGroup) {
        delete m_styleGroup;
    }
    m_styleGroup = new QActionGroup(this);
    m_styleGroup->setExclusive(true);

    QSettings settings;
    const QString currentStyle =
        settings.value(QStringLiteral("ui/theme"), defaultThemeName()).toString();
    const QString customStyle =
        settings.value(QStringLiteral("ui/customStyle")).toString();

    bool customActive = false;
    const QStringList styles = availableThemes();
    for (const QString &name : styles) {
        QAction *action = m_styleMenu->addAction(name);
        action->setCheckable(true);
        const bool active = (name == currentStyle) && customStyle.isEmpty();
        action->setChecked(active);
        customActive = customActive || active;
        m_styleGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, name]() {
            applyTheme(name);
            QSettings settings;
            settings.setValue(QStringLiteral("ui/theme"), name);
            settings.remove(QStringLiteral("ui/customStyle"));
            buildStyleMenu();
        });
    }

    m_styleMenu->addSeparator();

    QAction *customStyleAction = m_styleMenu->addAction(tr("Load &custom style..."));
    customStyleAction->setCheckable(true);
    customStyleAction->setChecked(!customStyle.isEmpty() && !customActive);
    m_styleGroup->addAction(customStyleAction);
    connect(customStyleAction, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open a custom style"), QString(),
            tr("Qt stylesheet (*.qss);;All files (*)"));
        if (path.isEmpty()) {
            buildStyleMenu();
            return;
        }
        applyThemeFile(path);
        QSettings settings;
        settings.setValue(QStringLiteral("ui/customStyle"), path);
        settings.setValue(QStringLiteral("ui/theme"), QFileInfo(path).fileName());
        buildStyleMenu();
    });
}

void MainWindow::createLayout()
{
    QToolBar *toolBar = addToolBar(tr("Library"));
    toolBar->setObjectName(QStringLiteral("libraryToolBar"));
    toolBar->setMovable(false);
    toolBar->addAction(m_startAction);
    toolBar->addAction(m_openAction);
    toolBar->addAction(m_refreshAction);
    toolBar->addSeparator();
    toolBar->addAction(m_settingsAction);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    QLabel *title = new QLabel(tr("Game Boy Library"), central);
    title->setObjectName(QStringLiteral("libraryTitle"));
    layout->addWidget(title);

    QLabel *subtitle = new QLabel(
        tr("Select a game and press Start. The game opens in its own window."),
        central);
    subtitle->setObjectName(QStringLiteral("librarySubtitle"));
    layout->addWidget(subtitle);

    m_romList = new QListWidget(central);
    m_romList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_romList->setAlternatingRowColors(true);
    m_romList->setStatusTip(tr("Double click a game to play it"));
    layout->addWidget(m_romList, 1);

    m_detailLabel = new QLabel(tr("Select a game to see its details."), central);
    m_detailLabel->setObjectName(QStringLiteral("libraryDetail"));
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setTextFormat(Qt::RichText);
    layout->addWidget(m_detailLabel);

    QHBoxLayout *buttons = new QHBoxLayout(central);
    QPushButton *startButton = new QPushButton(tr("Start"), central);
    startButton->setObjectName(QStringLiteral("primaryButton"));
    startButton->setMinimumHeight(32);
    QPushButton *openButton = new QPushButton(tr("Open ROM File..."), central);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), central);
    QPushButton *settingsButton = new QPushButton(tr("Settings..."), central);
    buttons->addWidget(startButton);
    buttons->addWidget(openButton);
    buttons->addWidget(refreshButton);
    buttons->addStretch(1);
    buttons->addWidget(settingsButton);
    layout->addLayout(buttons);

    setCentralWidget(central);

    m_countLabel = new QLabel(this);
    statusBar()->addWidget(m_countLabel);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSelectedGame);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseRom);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshLibrary);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(m_romList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { startGame(item->data(Qt::UserRole).toString()); });
    connect(m_romList, &QListWidget::itemSelectionChanged, this, &MainWindow::updateDetail);
}

QStringList MainWindow::libraryFolders() const
{
    QSettings settings;
    QStringList folders =
        settings.value(QStringLiteral("library/folders")).toStringList();
    folders.removeAll(QString());
    folders.removeDuplicates();
    return folders;
}

QString MainWindow::romDescription(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray header = file.read(kHeaderRomSizeOffset + 1);
    file.close();
    if (header.size() < kHeaderRomSizeOffset + 1) {
        return QObject::tr("Invalid ROM");
    }

    // Title: 0x134 .. 0x13E, trimmed at the first zero byte.
    QByteArray title = header.mid(kHeaderTitleOffset, kHeaderTitleLength);
    const int zero = title.indexOf(char(0));
    if (zero >= 0) {
        title.truncate(zero);
    }
    const QString titleText = QString::fromLatin1(title).trimmed();

    const std::uint8_t cartType =
        static_cast<std::uint8_t>(header.at(kHeaderCartTypeOffset));
    const std::uint8_t romSizeCode =
        static_cast<std::uint8_t>(header.at(kHeaderRomSizeOffset));

    // ROM size: byte 0x148 encodes 32 KiB << n.
    const qint64 romBytes = romSizeCode < 8
        ? (static_cast<qint64>(32768) << romSizeCode)
        : 0;

    QStringList parts;
    if (!titleText.isEmpty()) {
        parts.append(titleText.toUpper());
    }
    parts.append(cartTypeName(cartType));
    if (romBytes > 0) {
        parts.append(QObject::tr("%1 KiB").arg(romBytes / 1024));
    }
    return parts.join(QStringLiteral(" &middot; "));
}

void MainWindow::scanLibrary()
{
    const QStringList filters = {QStringLiteral("*.gb"), QStringLiteral("*.gbc")};
    QStringList folders = libraryFolders();

    // Always include the ROM folder next to the executable.
    const QString defaultRomFolder =
        QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("ROM"));
    if (QDir(defaultRomFolder).exists() && !folders.contains(defaultRomFolder)) {
        folders.prepend(defaultRomFolder);
    }

    QStringList files;
    for (const QString &folder : folders) {
        QDirIterator it(folder, filters, QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!files.contains(path)) {
                files.append(path);
            }
        }
    }
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    m_romFiles = files;
    m_romList->clear();
    for (const QString &path : m_romFiles) {
        const QFileInfo info(path);
        QListWidgetItem *item = new QListWidgetItem(info.fileName(), m_romList);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }

    m_countLabel->setText(files.isEmpty()
                              ? tr("No games found")
                              : tr("%1 game(s) found").arg(files.size()));
    updateDetail();
}

void MainWindow::loadRecentFiles()
{
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
}

void MainWindow::saveRecentFiles()
{
    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
}

void MainWindow::updateRecentFilesMenu()
{
    m_recentMenu->clear();

    if (m_recentFiles.isEmpty()) {
        QAction *none = m_recentMenu->addAction(tr("No recent games"));
        none->setEnabled(false);
        return;
    }

    int index = 1;
    for (const QString &path : m_recentFiles) {
        const QFileInfo info(path);
        const QString label = QStringLiteral("%1 %2").arg(index).arg(info.fileName());
        QAction *action = m_recentMenu->addAction(label);
        action->setData(path);
        action->setStatusTip(path);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentFile);
        ++index;
    }
}

void MainWindow::addRecentFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > kMaxRecentFiles) {
        m_recentFiles.removeLast();
    }
}

} // namespace ui
