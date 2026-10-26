/**
 * @file MainWindow.cpp
 * @brief Implementation of the launcher window with the ROM library.
 *
 * The window scans the configured library folders (recursively) for .gb and
 * .gbc files and shows them in a list. On startup the game database
 * (data/games_database.json) is loaded and each ROM is matched to its entry
 * by cartridge header title (file name as fallback), so the detail panel can
 * show the full name, developer/publisher, release year, genre, rating and
 * description. A search box filters the list and a combo box sorts it by
 * name, year or genre.
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
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
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

/**
 * @brief Returns a comparison key: uppercase, spaces and punctuation removed.
 * @param text The text to normalize.
 * @return The normalized key, e.g. "pokemon blue" -> "POKEMONBLUE".
 */
QString normalizedKey(const QString &text)
{
    QString key = text.toUpper();
    key.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
    return key;
}

/**
 * @brief Reads the cartridge header title of a ROM file.
 * @param path Path of the ROM.
 * @return The uppercase, trimmed title, or an empty string.
 */
QString headerTitle(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray header = file.read(kHeaderTitleOffset + kHeaderTitleLength);
    file.close();
    if (header.size() < kHeaderTitleOffset + kHeaderTitleLength) {
        return QString();
    }
    QByteArray title = header.mid(kHeaderTitleOffset, kHeaderTitleLength);
    const int zero = title.indexOf(char(0));
    if (zero >= 0) {
        title.truncate(zero);
    }
    return QString::fromLatin1(title).trimmed().toUpper();
}

/**
 * @brief Returns a representative color for a genre.
 * @param genre The genre string, e.g. "RPG".
 * @return The color used for the list icon.
 */
QColor genreColor(const QString &genre)
{
    const QString g = genre.toLower();
    if (g.contains(QLatin1String("rpg"))) {
        return QColor(0x7B, 0x4F, 0xC9);
    }
    if (g.contains(QLatin1String("puzzle"))) {
        return QColor(0xE0, 0x8B, 0x2D);
    }
    if (g.contains(QLatin1String("platform"))) {
        return QColor(0x2E, 0x8B, 0x57);
    }
    if (g.contains(QLatin1String("adventure")) || g.contains(QLatin1String("action"))) {
        return QColor(0x2B, 0x6C, 0xB0);
    }
    if (g.contains(QLatin1String("racing")) || g.contains(QLatin1String("sport"))) {
        return QColor(0xC0, 0x3A, 0x2B);
    }
    if (g.contains(QLatin1String("shoot")) || g.contains(QLatin1String("fight"))) {
        return QColor(0x8B, 0x2B, 0x2B);
    }
    return QColor(0x55, 0x63, 0x72);
}

/**
 * @brief Builds (and caches) a small colored icon for a genre.
 * @param genre The genre string, e.g. "RPG".
 * @return A rounded square icon with the genre initial.
 */
QIcon genreIcon(const QString &genre)
{
    static QHash<QString, QIcon> cache;
    const QString key = genre.isEmpty() ? QStringLiteral("default") : genre;
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }

    constexpr int kIconSize = 32;
    QPixmap pixmap(kIconSize, kIconSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(genreColor(genre));
    painter.drawRoundedRect(1, 1, kIconSize - 2, kIconSize - 2, 7, 7);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(16);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QString letter = genre.isEmpty() ? QStringLiteral("?")
                                           : genre.left(1).toUpper();
    painter.drawText(pixmap.rect(), Qt::AlignCenter, letter);
    painter.end();

    QIcon icon(pixmap);
    cache.insert(key, icon);
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createActions();
    createLayout();

    loadGameDatabase();
    loadRecentFiles();
    updateRecentFilesMenu();

    applySavedTheme();

    setWindowTitle(tr("Game Boy Library"));
    setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
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
        if (m_romList->count() == 0 && m_searchEdit
            && !m_searchEdit->text().isEmpty()) {
            m_detailLabel->setText(tr("No games match your search."));
        } else {
            m_detailLabel->setText(tr("Select a game to see its details."));
        }
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    const QFileInfo info(path);
    const qint64 sizeKiB = (info.size() + 1023) / 1024;
    const QJsonObject game = gameInfoForPath(path);

    if (game.isEmpty()) {
        // No database entry: keep the compact header description.
        m_detailLabel->setText(tr("<b>%1</b><br>%2<br>%3 KiB &middot; %4")
                                   .arg(info.fileName().toHtmlEscaped())
                                   .arg(romDescription(path))
                                   .arg(sizeKiB)
                                   .arg(QDir::toNativeSeparators(info.absolutePath()).toHtmlEscaped()));
        return;
    }

    const QString fullName = game.value(QLatin1String("full_name")).toString(info.fileName());
    const QString developer = game.value(QLatin1String("developer")).toString();
    const QString publisher = game.value(QLatin1String("publisher")).toString();
    const int year = game.value(QLatin1String("release_year")).toInt();
    const QString genre = game.value(QLatin1String("genre")).toString();
    const QString players = game.value(QLatin1String("players")).toString();
    const QString rating = game.value(QLatin1String("rating")).toString();
    const QString description = game.value(QLatin1String("description")).toString();

    QStringList meta;
    if (year > 0) {
        meta.append(QString::number(year));
    }
    if (!genre.isEmpty()) {
        meta.append(genre.toHtmlEscaped());
    }
    if (!players.isEmpty()) {
        meta.append(tr("%1 player(s)").arg(players));
    }
    if (!rating.isEmpty()) {
        meta.append(tr("ESRB %1").arg(rating.toHtmlEscaped()));
    }

    QString team;
    if (!developer.isEmpty()) {
        team.append(developer.toHtmlEscaped());
    }
    if (!publisher.isEmpty()) {
        if (!team.isEmpty()) {
            team.append(QStringLiteral(" &middot; "));
        }
        team.append(publisher.toHtmlEscaped());
    }
    if (team.isEmpty()) {
        team = romDescription(path).toHtmlEscaped();
    }

    QString html = QStringLiteral("<b>%1</b><br>").arg(fullName.toHtmlEscaped());
    html += QStringLiteral("<span style=\"color:gray;\">%1</span><br>").arg(team);
    if (!meta.isEmpty()) {
        html += QStringLiteral("%1<br>").arg(meta.join(QStringLiteral(" &middot; ")));
    }
    html += QStringLiteral("<small>%1 &middot; %2 KiB &middot; %3</small>")
                .arg(info.fileName().toHtmlEscaped())
                .arg(sizeKiB)
                .arg(QDir::toNativeSeparators(info.absolutePath()).toHtmlEscaped());
    if (!description.isEmpty()) {
        html += QStringLiteral("<br><br>%1").arg(description.toHtmlEscaped());
    }
    m_detailLabel->setText(html);
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

    // Search box and sort selector above the ROM list.
    QHBoxLayout *searchRow = new QHBoxLayout(central);
    m_searchEdit = new QLineEdit(central);
    m_searchEdit->setPlaceholderText(tr("Search games..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setObjectName(QStringLiteral("librarySearch"));
    m_searchEdit->setStatusTip(tr("Filter the library by name"));
    m_sortCombo = new QComboBox(central);
    m_sortCombo->setObjectName(QStringLiteral("librarySort"));
    m_sortCombo->addItem(tr("Sort by Name"), 0);
    m_sortCombo->addItem(tr("Sort by Year"), 1);
    m_sortCombo->addItem(tr("Sort by Genre"), 2);
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_sortCombo);
    layout->addLayout(searchRow);

    m_romList = new QListWidget(central);
    m_romList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_romList->setAlternatingRowColors(true);
    m_romList->setIconSize(QSize(24, 24));
    m_romList->setStatusTip(tr("Double click a game to play it"));
    layout->addWidget(m_romList, 1);

    QGroupBox *detailBox = new QGroupBox(tr("Details"), central);
    detailBox->setObjectName(QStringLiteral("libraryDetailBox"));
    QVBoxLayout *detailLayout = new QVBoxLayout(detailBox);
    detailLayout->setContentsMargins(8, 4, 8, 4);
    m_detailLabel = new QLabel(tr("Select a game to see its details."), detailBox);
    m_detailLabel->setObjectName(QStringLiteral("libraryDetail"));
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setTextFormat(Qt::RichText);
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_detailLabel);
    layout->addWidget(detailBox);

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
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::applyFilterAndSort);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { applyFilterAndSort(); });
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
    applyFilterAndSort();
}

void MainWindow::loadGameDatabase()
{
    m_games.clear();

    // Look for the database in the install layout (next to the executable),
    // one level up (project root when running from build/), the current
    // directory and the application path itself.
    QDir appDir(QApplication::applicationDirPath());
    QDir appParent = QDir(appDir);
    appParent.cdUp();

    QStringList candidates;
    candidates.append(appDir.filePath(QStringLiteral("data/games_database.json")));
    candidates.append(appDir.filePath(QStringLiteral("games_database.json")));
    candidates.append(appParent.filePath(QStringLiteral("data/games_database.json")));
    candidates.append(QDir::current().filePath(QStringLiteral("data/games_database.json")));
#ifdef GB_SOURCE_DATA_DIR
    // Running from a build tree: the source folder holds the database.
    candidates.append(QStringLiteral(GB_SOURCE_DATA_DIR "/games_database.json"));
#endif
    // Last resort: the copy compiled into the executable, so the library
    // always has its game information even in a bare installation.
    candidates.append(QStringLiteral(":/data/games_database.json"));

    QString path;
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    const QJsonArray games =
        document.object().value(QLatin1String("games")).toArray();
    for (const QJsonValue &value : games) {
        const QJsonObject game = value.toObject();
        const QString title = game.value(QLatin1String("title")).toString();
        if (title.isEmpty()) {
            continue;
        }
        m_games.insert(normalizedKey(title), game);
    }
}

const QJsonObject MainWindow::gameInfoForPath(const QString &path) const
{
    // Primary match: the cartridge header title.
    const QString titleKey = normalizedKey(headerTitle(path));
    auto it = m_games.constFind(titleKey);
    if (it != m_games.constEnd()) {
        return it.value();
    }

    // Fallback match: the file name without the extension and without the
    // usual " (USA)", " [Rev 1]" style release tags.
    const QFileInfo info(path);
    QString base = info.completeBaseName();
    base.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    base.remove(QRegularExpression(QStringLiteral("\\s*\\[[^]]*\\]")));
    it = m_games.constFind(normalizedKey(base));
    if (it != m_games.constEnd()) {
        return it.value();
    }
    return QJsonObject();
}

void MainWindow::applyFilterAndSort()
{
    struct Entry {
        QString path;
        QString name;
        QString genre;
        int year = 0;
    };

    const QString filter = m_searchEdit->text().trimmed();
    const int sortMode = m_sortCombo->currentData().toInt();

    QList<Entry> entries;
    entries.reserve(m_romFiles.size());
    for (const QString &path : m_romFiles) {
        const QFileInfo info(path);
        const QJsonObject game = gameInfoForPath(path);
        QString name = info.fileName();
        QString genre;
        int year = 0;
        if (!game.isEmpty()) {
            name = game.value(QLatin1String("full_name")).toString(name);
            genre = game.value(QLatin1String("genre")).toString();
            year = game.value(QLatin1String("release_year")).toInt();
        }

        if (!filter.isEmpty()) {
            const QString needle = normalizedKey(filter);
            const QString haystack =
                normalizedKey(name + QLatin1Char(' ') + info.completeBaseName());
            if (!haystack.contains(needle)) {
                continue;
            }
        }
        entries.append({path, name, genre, year});
    }

    switch (sortMode) {
    case 1: // Year, unknown years last.
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) {
            const int ay = a.year > 0 ? a.year : 2147483647;
            const int by = b.year > 0 ? b.year : 2147483647;
            if (ay != by) {
                return ay < by;
            }
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        break;
    case 2: // Genre, unknown genres last.
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) {
            const QString ag = a.genre.isEmpty() ? QStringLiteral("~") : a.genre;
            const QString bg = b.genre.isEmpty() ? QStringLiteral("~") : b.genre;
            const int cmp = QString::compare(ag, bg, Qt::CaseInsensitive);
            if (cmp != 0) {
                return cmp < 0;
            }
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        break;
    default: // Name.
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        break;
    }

    // Remember the selection so it survives a re-filter/re-sort.
    const QString previousPath = m_romList->currentItem()
        ? m_romList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    m_romList->clear();
    for (const Entry &entry : entries) {
        QListWidgetItem *item = new QListWidgetItem(entry.name, m_romList);
        item->setData(Qt::UserRole, entry.path);
        item->setIcon(genreIcon(entry.genre));
        item->setToolTip(entry.path);
    }

    if (entries.isEmpty() && m_romFiles.isEmpty()) {
        m_countLabel->setText(tr("No games found"));
    } else if (filter.isEmpty()) {
        m_countLabel->setText(tr("%1 game(s) found").arg(entries.size()));
    } else {
        m_countLabel->setText(
            tr("%1 of %2 game(s)").arg(entries.size()).arg(m_romFiles.size()));
    }

    if (!previousPath.isEmpty()) {
        for (int i = 0; i < m_romList->count(); ++i) {
            if (m_romList->item(i)->data(Qt::UserRole).toString() == previousPath) {
                m_romList->setCurrentRow(i);
                break;
            }
        }
    }
    if (m_romList->currentRow() < 0 && m_romList->count() > 0) {
        m_romList->setCurrentRow(0);
    }
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
