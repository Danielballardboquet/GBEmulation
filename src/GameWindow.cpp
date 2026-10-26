/**
 * @file GameWindow.cpp
 * @brief Implementation of the window that plays one ROM.
 *
 * A faithful copy of the emulation wiring extracted from the former
 * MainWindow: an EmulationWorker on a dedicated QThread, a screen widget, a
 * hidden-by-default debugger dock, screenshot and zoom support and a settings
 * dialog for the key bindings and the theme.
 *
 * @see https://doc.qt.io/qt-6/qthread.html          QThread worker pattern
 * @see https://doc.qt.io/qt-6/signalsandslots.html  Qt signals and slots
 */

#include "ui/GameWindow.h"

#include "ui/DebugPanel.h"
#include "ui/EmulationWorker.h"
#include "ui/GBScreenWidget.h"
#include "ui/PaletteDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

#include "emulation/Emulation.h"
#include "emulation/Types.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ui {

namespace {

/** @brief Interval of the debugger refresh timer in milliseconds. */
constexpr int kDebugRefreshIntervalMs = 100;

/** @brief Minimum period between two FPS statistics updates in milliseconds. */
constexpr int kFpsMeasureIntervalMs = 500;

/**
 * @brief Returns the folder where screenshots are stored.
 */
QString screenshotDirectory()
{
    const QStringList pictures = QStandardPaths::standardLocations(
        QStandardPaths::PicturesLocation);
    if (!pictures.isEmpty()) {
        return pictures.first();
    }
    return QApplication::applicationDirPath();
}

} // namespace

GameWindow::GameWindow(const QString &romPath, QWidget *parent)
    : QMainWindow(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);

    m_stateLabel = new QLabel(tr("Paused"), this);
    m_fpsLabel = new QLabel(tr("FPS: --"), this);
    m_romLabel = new QLabel(tr("No ROM"), this);

    createActions();
    createToolBar();
    createLayout();

    startEmulationThread();

    QSettings settings;
    const int savedSpeed = settings.value(QStringLiteral("emulation/speed"), 100).toInt();
    m_speedSlider->setValue(savedSpeed);
    QMetaObject::invokeMethod(m_worker, "setSpeed", Qt::QueuedConnection,
                              Q_ARG(int, savedSpeed));

    m_debugTimer = new QTimer(this);
    m_debugTimer->setInterval(kDebugRefreshIntervalMs);
    connect(m_debugTimer, &QTimer::timeout, this, &GameWindow::refreshDebugPanel);
    m_debugTimer->start();

    applyKeyBindings();
    applySavedTheme();
    applyPaletteFromSettings();

    // The LCD follows the window size unless a fixed zoom is chosen later.
    setZoomScale(0);

    setWindowTitle(tr("Game Boy Emulator"));
    resize(720, 520);

    if (!romPath.isEmpty()) {
        openRom(romPath);
    }
}

GameWindow::~GameWindow()
{
    stopEmulationThread();
}

void GameWindow::onUiAction(UiAction action)
{
    switch (action) {
    case UiAction::Pause:
        togglePause();
        break;
    case UiAction::Fullscreen:
        toggleFullScreen();
        break;
    case UiAction::Exit:
        close();
        break;
    }
}

void GameWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        // showNormal() alone restores the native window frame; forcing the
        // FramelessWindowHint flag off and on again left the title bar
        // without its minimize/maximize/close buttons on Windows, so the
        // flag is never touched here.
        showNormal();
        // Restore the toolbars that were hidden for the full screen view.
        QSettings settings;
        setMenuBarVisible(settings.value(QStringLiteral("ui/showMenuBar"), true).toBool());
        setToolBarVisible(settings.value(QStringLiteral("ui/showToolBar"), false).toBool());
    } else {
        // showFullScreen() already removes the window frame; only hide the
        // menu bar and toolbar to maximize the usable screen space.
        menuBar()->hide();
        if (m_toolBar) {
            m_toolBar->hide();
        }
        showFullScreen();
    }
}

void GameWindow::setToolBarVisible(bool visible)
{
    if (m_toolBar) {
        m_toolBar->setVisible(visible);
    }
    if (m_toolbarAction) {
        m_toolbarAction->setChecked(visible);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("ui/showToolBar"), visible);
}

void GameWindow::setMenuBarVisible(bool visible)
{
    menuBar()->setVisible(visible);
    if (m_menubarAction) {
        m_menubarAction->setChecked(visible);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("ui/showMenuBar"), visible);
}

void GameWindow::applyPaletteMode(int mode)
{
    QSettings settings;
    settings.setValue(QStringLiteral("display/paletteMode"), mode);
    if (m_worker) {
        // When activating the custom palette, push the saved colours to the
        // emulation thread as well: the GPU only switches the mode otherwise
        // and would keep rendering with the previous palette.
        if (mode == static_cast<int>(gb::DisplayPalette::Custom)) {
            const unsigned int c0 = settings.value(QStringLiteral("display/palette0")).toUInt();
            const unsigned int c1 = settings.value(QStringLiteral("display/palette1")).toUInt();
            const unsigned int c2 = settings.value(QStringLiteral("display/palette2")).toUInt();
            const unsigned int c3 = settings.value(QStringLiteral("display/palette3")).toUInt();
            QMetaObject::invokeMethod(m_worker, "setCustomPalette", Qt::QueuedConnection,
                                      Q_ARG(uint, c0), Q_ARG(uint, c1),
                                      Q_ARG(uint, c2), Q_ARG(uint, c3));
        }
        QMetaObject::invokeMethod(m_worker, "setPalette", Qt::QueuedConnection,
                                  Q_ARG(int, mode));
    }
}

void GameWindow::editCustomPalette()
{
    // Default: the original DMG green shades (RRGGBBAA).
    constexpr unsigned int kDefaultShades[4] = {
        0x9BBC0FFFu, 0x8BAC0FFFu, 0x306230FFu, 0x0F380FFFu
    };

    QSettings settings;
    std::array<QRgb, 4> colors;
    for (int index = 0; index < 4; ++index) {
        bool ok = false;
        const unsigned int stored = settings.value(
            QStringLiteral("display/palette%1").arg(index)).toUInt(&ok);
        colors[index] = static_cast<QRgb>(stored != 0u ? stored : kDefaultShades[index]);
    }

    PaletteDialog dialog(colors, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    colors = dialog.colors();
    for (int index = 0; index < 4; ++index) {
        settings.setValue(QStringLiteral("display/palette%1").arg(index),
                          static_cast<unsigned int>(colors[index]));
    }
    applyPaletteMode(static_cast<int>(gb::DisplayPalette::Custom));
    m_paletteCustomAction->setChecked(true);
}

void GameWindow::applyStyle(const QString &name)
{
    applyTheme(name);
    QSettings settings;
    settings.setValue(QStringLiteral("ui/theme"), name);
    settings.remove(QStringLiteral("ui/customStyle"));
    buildStyleActions();
}

void GameWindow::showScreenContextMenu(const QPoint &pos)
{
    QMenu menu(this);

    menu.addAction(m_pauseAction);
    menu.addAction(m_resetAction);
    menu.addSeparator();
    menu.addAction(m_fullscreenAction);
    menu.addAction(m_screenshotAction);
    menu.addAction(m_debugAction);
    menu.addSeparator();
    menu.addAction(m_toolbarAction);
    menu.addAction(m_menubarAction);
    menu.addSeparator();
    menu.addAction(m_quitAction);

    menu.exec(m_screen->mapToGlobal(pos));
}

void GameWindow::openRom(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    if (!m_currentRom.isEmpty()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Replace ROM"),
            tr("A ROM is already loaded. Replace it with this one?"),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        // Persist the battery RAM of the replaced cartridge before switching.
        saveBatteryRam();
    }

    m_currentRom = path;
    QMetaObject::invokeMethod(m_worker, "loadRom", Qt::QueuedConnection,
                              Q_ARG(QString, path));

    // Remember the game in the launcher's recent files list.
    QSettings settings;
    QStringList recent =
        settings.value(QStringLiteral("recentFiles")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 8) {
        recent.removeLast();
    }
    settings.setValue(QStringLiteral("recentFiles"), recent);
}

void GameWindow::closeEvent(QCloseEvent *event)
{
    if (!m_currentRom.isEmpty()) {
        saveBatteryRam();
    }
    stopEmulationThread();
    event->accept();
}

void GameWindow::chooseRom()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Game Boy ROM"),
        QString(),
        tr("Game Boy ROM (*.gb *.gbc);;All files (*)"));
    if (!path.isEmpty()) {
        openRom(path);
    }
}

void GameWindow::reloadRom()
{
    if (!m_currentRom.isEmpty()) {
        QMetaObject::invokeMethod(m_worker, "loadRom", Qt::QueuedConnection,
                                  Q_ARG(QString, m_currentRom));
    }
}

void GameWindow::resetEmulation()
{
    QMetaObject::invokeMethod(m_worker, "reset", Qt::QueuedConnection);
}

void GameWindow::togglePause()
{
    QMetaObject::invokeMethod(m_worker, "setRunning", Qt::QueuedConnection,
                              Q_ARG(bool, !m_running));
}

void GameWindow::stepInstruction()
{
    QMetaObject::invokeMethod(m_worker, "stepInstruction", Qt::QueuedConnection);
}

void GameWindow::stepFrame()
{
    QMetaObject::invokeMethod(m_worker, "stepFrame", Qt::QueuedConnection);
}

void GameWindow::saveBatteryRam()
{
    const QString path = currentSavePath();
    if (path.isEmpty()) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "saveBatteryRam", Qt::QueuedConnection,
                              Q_ARG(QString, path));
    statusBar()->showMessage(tr("Battery RAM saved to %1").arg(path), 3000);
}

void GameWindow::saveStateSlot(int slot)
{
    const QString path = currentStatePath(slot);
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Load a ROM before saving a state"), 2500);
        return;
    }
    QMetaObject::invokeMethod(m_worker, "saveState", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void GameWindow::refreshStateSlotLabels()
{
    const bool romLoaded = !m_currentRom.isEmpty();
    for (int slot = 1; slot <= kSaveStateSlotCount; ++slot) {
        const QString path = currentStatePath(slot);
        const QFileInfo info(path);
        const bool filled = !path.isEmpty() && info.exists();
        const QString suffix = filled
            ? tr(" (%1)").arg(info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
            : tr(" (empty)");
        const QString label = tr("Slot %1%2").arg(slot).arg(suffix);
        if (QAction *saveAction = m_saveStateActions[slot - 1]) {
            saveAction->setText(label);
            saveAction->setEnabled(romLoaded);
        }
        if (QAction *loadAction = m_loadStateActions[slot - 1]) {
            loadAction->setText(label);
            loadAction->setEnabled(romLoaded && filled);
        }
    }
}

void GameWindow::loadStateSlot(int slot)
{
    const QString path = currentStatePath(slot);
    if (path.isEmpty()) {
        statusBar()->showMessage(tr("Load a ROM before loading a state"), 2500);
        return;
    }
    if (!QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("Slot %1 is empty").arg(slot), 2500);
        return;
    }
    QMetaObject::invokeMethod(m_worker, "loadState", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void GameWindow::onStateSaved(const QString &path)
{
    statusBar()->showMessage(tr("State saved to %1").arg(path), 3000);
    refreshStateSlotLabels();
}

void GameWindow::onStateSaveFailed(const QString &error)
{
    statusBar()->showMessage(tr("Could not save the state: %1").arg(error), 4000);
}

void GameWindow::onStateLoaded(const QString &path)
{
    statusBar()->showMessage(tr("State loaded from %1").arg(path), 3000);
}

void GameWindow::onStateLoadFailed(const QString &error)
{
    statusBar()->showMessage(tr("Could not load the state: %1").arg(error), 4000);
}

void GameWindow::takeScreenshot()
{
    if (!m_worker || !m_worker->emulator() || !m_worker->emulator()->isRunning()) {
        statusBar()->showMessage(tr("Nothing to capture"), 2000);
        return;
    }
    const gb::u8 *buffer = m_worker->emulator()->frameBuffer();
    QImage image(gb::kScreenWidth, gb::kScreenHeight, QImage::Format_RGBA8888);
    std::memcpy(image.bits(), buffer,
                static_cast<std::size_t>(gb::kScreenWidth * gb::kScreenHeight * 4));

    const QFileInfo info(m_currentRom);
    const QString name = info.completeBaseName().isEmpty()
        ? QStringLiteral("gameboy")
        : info.completeBaseName();

    const QDir dir(screenshotDirectory());
    dir.mkpath(QStringLiteral("GB_emulator_screenshots"));
    const QString path = dir.filePath(QStringLiteral("GB_emulator_screenshots") + QDir::separator()
        + QStringLiteral("%1_%2.png")
              .arg(name)
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss"))));

    if (image.save(path, "PNG")) {
        statusBar()->showMessage(tr("Screenshot saved to %1").arg(path), 4000);
    } else {
        statusBar()->showMessage(tr("Could not save the screenshot"), 3000);
    }
}

void GameWindow::setDebugPanelVisible(bool visible)
{
    if (m_debugDock) {
        m_debugDock->setVisible(visible);
    }
    if (m_debugAction) {
        m_debugAction->setChecked(visible);
    }
}

void GameWindow::openSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
    applyKeyBindings();
    applySavedTheme();
    buildStyleActions();
}

void GameWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("About Game Boy Emulator"),
        tr("<b>Game Boy Emulator</b> v%1<br><br>"
           "A Game Boy (DMG/CGB) emulator written in C++ with a Qt 6 "
           "interface.<br>Configure the controller bindings in Settings and "
           "press F12 to capture a screenshot.")
            .arg(qApp->applicationVersion()));
}

void GameWindow::onFrameReady(const QByteArray &frame, quint64 frameNumber)
{
    Q_UNUSED(frame);
    Q_UNUSED(frameNumber);
    m_screen->refreshFrame();
}

void GameWindow::onRomLoaded(const QString &title, const QString &mapper, int romBanks, int ramBanks)
{
    m_romLabel->setText(tr("%1  [%2]").arg(title, mapper));
    m_stateLabel->setText(tr("Running"));
    setWindowTitle(tr("Game Boy Emulator - %1").arg(title));

    m_reloadAction->setEnabled(true);
    m_saveAction->setEnabled(ramBanks > 0);
    m_resetAction->setEnabled(true);
    m_pauseAction->setEnabled(true);
    m_stepAction->setEnabled(false);
    m_frameAction->setEnabled(false);
    refreshStateSlotLabels();

    statusBar()->showMessage(
        tr("Loaded %1 (%2 ROM banks, %3 RAM banks)").arg(title).arg(romBanks).arg(ramBanks),
        3000);
}

void GameWindow::onRomFailed(const QString &message)
{
    QMessageBox::warning(this, tr("Cannot load ROM"), message);
}

void GameWindow::onStatisticsUpdated(double fps)
{
    m_fpsLabel->setText(tr("FPS: %1").arg(fps, 0, 'f', 1));
}

void GameWindow::onRunningChanged(bool running)
{
    m_running = running;
    m_pauseAction->setText(running ? tr("&Pause") : tr("Re&sume"));

    const bool romLoaded = m_resetAction->isEnabled();
    m_stepAction->setEnabled(romLoaded && !running);
    m_frameAction->setEnabled(romLoaded && !running);

    m_stateLabel->setText(running ? tr("Running") : tr("Paused"));
}

void GameWindow::refreshDebugPanel()
{
    if (m_debugDock && m_debugDock->isVisible() && m_debugPanel) {
        m_debugPanel->refresh();
    }
}

void GameWindow::onButtonChanged(int index, bool pressed)
{
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setButton", Qt::QueuedConnection,
                                  Q_ARG(int, index), Q_ARG(bool, pressed));
    }
}

void GameWindow::createActions()
{
    m_openAction = new QAction(tr("&Open ROM..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("Load a Game Boy ROM file"));
    connect(m_openAction, &QAction::triggered, this, &GameWindow::chooseRom);

    m_reloadAction = new QAction(tr("&Reload"), this);
    m_reloadAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_reloadAction->setStatusTip(tr("Reload the current ROM from disk"));
    m_reloadAction->setEnabled(false);
    connect(m_reloadAction, &QAction::triggered, this, &GameWindow::reloadRom);

    m_saveAction = new QAction(tr("Save &RAM..."), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(tr("Write the battery backed RAM to a .sav file"));
    m_saveAction->setEnabled(false);
    connect(m_saveAction, &QAction::triggered, this, &GameWindow::saveBatteryRam);

    m_quitAction = new QAction(tr("&Quit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    m_quitAction->setStatusTip(tr("Quit the emulator"));
    connect(m_quitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    m_resetAction = new QAction(tr("&Reset"), this);
    m_resetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_resetAction->setStatusTip(tr("Power the machine on again"));
    m_resetAction->setEnabled(false);
    connect(m_resetAction, &QAction::triggered, this, &GameWindow::resetEmulation);

    m_pauseAction = new QAction(tr("&Pause"), this);
    m_pauseAction->setShortcut(QKeySequence(Qt::Key_P));
    m_pauseAction->setStatusTip(tr("Start or pause the emulation"));
    m_pauseAction->setEnabled(false);
    connect(m_pauseAction, &QAction::triggered, this, &GameWindow::togglePause);

    m_stepAction = new QAction(tr("Step &Instruction"), this);
    m_stepAction->setStatusTip(tr("Execute a single CPU instruction while paused"));
    m_stepAction->setEnabled(false);
    connect(m_stepAction, &QAction::triggered, this, &GameWindow::stepInstruction);

    m_frameAction = new QAction(tr("Step &Frame"), this);
    m_frameAction->setShortcut(QKeySequence(Qt::Key_F6));
    m_frameAction->setStatusTip(tr("Execute a single frame while paused"));
    m_frameAction->setEnabled(false);
    connect(m_frameAction, &QAction::triggered, this, &GameWindow::stepFrame);

    m_screenshotAction = new QAction(tr("&Screenshot"), this);
    m_screenshotAction->setShortcut(QKeySequence(Qt::Key_F12));
    m_screenshotAction->setStatusTip(tr("Save a PNG of the current screen"));
    connect(m_screenshotAction, &QAction::triggered, this, &GameWindow::takeScreenshot);

    QMenu *saveStateMenu = new QMenu(tr("&Save State"), this);
    QMenu *loadStateMenu = new QMenu(tr("&Load State"), this);
    for (int slot = 1; slot <= kSaveStateSlotCount; ++slot) {
        QAction *saveAction = saveStateMenu->addAction(tr("Slot %1").arg(slot));
        saveAction->setStatusTip(tr("Save the machine state to slot %1").arg(slot));
        saveAction->setEnabled(false);
        if (slot == 1) {
            saveAction->setShortcut(QKeySequence(Qt::Key_F5));
        }
        connect(saveAction, &QAction::triggered, this, [this, slot]() { saveStateSlot(slot); });
        m_saveStateActions[slot - 1] = saveAction;

        QAction *loadAction = loadStateMenu->addAction(tr("Slot %1").arg(slot));
        loadAction->setStatusTip(tr("Restore the machine state from slot %1").arg(slot));
        loadAction->setEnabled(false);
        if (slot == 1) {
            loadAction->setShortcut(QKeySequence(Qt::Key_F9));
        }
        connect(loadAction, &QAction::triggered, this, [this, slot]() { loadStateSlot(slot); });
        m_loadStateActions[slot - 1] = loadAction;
    }
    connect(saveStateMenu, &QMenu::aboutToShow, this, &GameWindow::refreshStateSlotLabels);
    connect(loadStateMenu, &QMenu::aboutToShow, this, &GameWindow::refreshStateSlotLabels);

    m_debugAction = new QAction(tr("&Debugger"), this);
    m_debugAction->setCheckable(true);
    m_debugAction->setChecked(false); // the debug panel is hidden by default
    m_debugAction->setShortcut(QKeySequence(Qt::Key_F7));
    m_debugAction->setStatusTip(tr("Show or hide the debug panel"));
    connect(m_debugAction, &QAction::toggled, this, &GameWindow::setDebugPanelVisible);

    m_fullscreenAction = new QAction(tr("&Full Screen"), this);
    m_fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_fullscreenAction->setStatusTip(tr("Toggle the full screen mode"));
    connect(m_fullscreenAction, &QAction::triggered, this, &GameWindow::toggleFullScreen);

    m_toolbarAction = new QAction(tr("&Toolbar"), this);
    m_toolbarAction->setCheckable(true);
    m_toolbarAction->setStatusTip(tr("Show or hide the toolbar"));
    connect(m_toolbarAction, &QAction::toggled, this, &GameWindow::setToolBarVisible);

    m_menubarAction = new QAction(tr("&Menu Bar"), this);
    m_menubarAction->setCheckable(true);
    m_menubarAction->setStatusTip(tr("Show or hide the menu bar"));
    connect(m_menubarAction, &QAction::toggled, this, &GameWindow::setMenuBarVisible);

    m_settingsAction = new QAction(tr("&Settings..."), this);
    m_settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    m_settingsAction->setStatusTip(tr("Controller bindings and appearance"));
    connect(m_settingsAction, &QAction::triggered, this, &GameWindow::openSettings);

    m_aboutAction = new QAction(tr("&About..."), this);
    connect(m_aboutAction, &QAction::triggered, this, &GameWindow::showAbout);

    m_aboutQtAction = new QAction(tr("About &Qt..."), this);
    connect(m_aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_reloadAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *emulationMenu = menuBar()->addMenu(tr("&Emulation"));
    emulationMenu->addAction(m_resetAction);
    emulationMenu->addAction(m_pauseAction);
    emulationMenu->addSeparator();
    emulationMenu->addAction(m_stepAction);
    emulationMenu->addAction(m_frameAction);
    emulationMenu->addSeparator();
    emulationMenu->addAction(m_screenshotAction);
    emulationMenu->addSeparator();
    emulationMenu->addMenu(saveStateMenu);
    emulationMenu->addMenu(loadStateMenu);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_debugAction);
    viewMenu->addAction(m_fullscreenAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_toolbarAction);
    viewMenu->addAction(m_menubarAction);
    viewMenu->addSeparator();

    QMenu *zoomMenu = viewMenu->addMenu(tr("&Zoom"));
    QActionGroup *zoomGroup = new QActionGroup(this);
    zoomGroup->setExclusive(true);
    m_fitAction = zoomMenu->addAction(tr("&Fit window"));
    m_fitAction->setCheckable(true);
    m_fitAction->setChecked(true);
    m_fitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    zoomGroup->addAction(m_fitAction);
    connect(m_fitAction, &QAction::triggered, this, [this]() { setZoomScale(0); });
    for (int scale = 1; scale <= 4; ++scale) {
        QAction *action = zoomMenu->addAction(tr("%1x").arg(scale));
        action->setCheckable(true);
        if (scale == 2) {
            action->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
        }
        zoomGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, scale]() { setZoomScale(scale); });
    }

    // Style submenu lives in the Settings menu: one checkable action per
    // available theme, plus an action to load an arbitrary .qss file.
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
    m_styleMenu = settingsMenu->addMenu(tr("&Style"));
    buildStyleActions();

    // The display palette selection lives next to the style: the three preset
    // modes are radio actions and "Custom Colours..." opens the colour editor
    // (the same dialog used for the LCD of the screen).
    QMenu *paletteMenu = settingsMenu->addMenu(tr("&Palette"));
    m_paletteGroup = new QActionGroup(this);
    m_paletteGroup->setExclusive(true);

    m_paletteDmgAction = paletteMenu->addAction(tr("&Original Game Boy"));
    m_paletteDmgAction->setCheckable(true);
    m_paletteDmgAction->setStatusTip(tr("Classic green LCD of the 1989 Game Boy"));
    m_paletteGbcAction = paletteMenu->addAction(tr("Game Boy &Color"));
    m_paletteGbcAction->setCheckable(true);
    m_paletteGbcAction->setStatusTip(tr("Default colours of the Game Boy Color"));
    m_paletteCustomAction = paletteMenu->addAction(tr("&Custom"));
    m_paletteCustomAction->setCheckable(true);
    m_paletteCustomAction->setStatusTip(tr("Use the colours defined in the custom editor"));

    m_paletteGroup->addAction(m_paletteDmgAction);
    m_paletteGroup->addAction(m_paletteGbcAction);
    m_paletteGroup->addAction(m_paletteCustomAction);

    connect(m_paletteDmgAction, &QAction::triggered, this,
            [this]() { applyPaletteMode(static_cast<int>(gb::DisplayPalette::Dmg)); });
    connect(m_paletteGbcAction, &QAction::triggered, this,
            [this]() { applyPaletteMode(static_cast<int>(gb::DisplayPalette::Gbc)); });
    connect(m_paletteCustomAction, &QAction::triggered, this,
            [this]() { applyPaletteMode(static_cast<int>(gb::DisplayPalette::Custom)); });

    paletteMenu->addSeparator();
    m_editPaletteAction = paletteMenu->addAction(tr("&Custom Colours..."));
    m_editPaletteAction->setStatusTip(tr("Open the custom colour editor dialog"));
    connect(m_editPaletteAction, &QAction::triggered, this, &GameWindow::editCustomPalette);

    settingsMenu->addSeparator();
    settingsMenu->addAction(m_settingsAction);
    m_settingsAction->setStatusTip(tr("Controller bindings and library folders"));

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
    helpMenu->addAction(m_aboutQtAction);
}

void GameWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Emulation"));
    m_toolBar->setObjectName(QStringLiteral("emulationToolBar"));
    m_toolBar->setMovable(false);

    m_toolBar->addAction(m_resetAction);
    m_toolBar->addAction(m_pauseAction);
    m_toolBar->addAction(m_stepAction);
    m_toolBar->addAction(m_frameAction);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_screenshotAction);
    m_toolBar->addSeparator();

    m_speedSlider = new QSlider(Qt::Horizontal, m_toolBar);
    m_speedSlider->setRange(10, 400);
    m_speedSlider->setValue(100);
    m_speedSlider->setFixedWidth(120);
    m_speedSlider->setStatusTip(tr("Emulation speed in percent"));
    m_toolBar->addWidget(m_speedSlider);

    m_speedLabel = new QLabel(tr("100%"), m_toolBar);
    m_speedLabel->setMinimumWidth(48);
    m_toolBar->addWidget(m_speedLabel);

    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
        m_speedLabel->setText(tr("%1%").arg(value));
        QSettings settings;
        settings.setValue(QStringLiteral("emulation/speed"), value);
        if (m_worker) {
            QMetaObject::invokeMethod(m_worker, "setSpeed", Qt::QueuedConnection,
                                      Q_ARG(int, value));
        }
    });
}

void GameWindow::createLayout()
{
    // A framed bezel around the LCD gives the screen a handheld look; the
    // screen itself expands so it always follows the window size.
    QFrame *container = new QFrame(this);
    container->setObjectName(QStringLiteral("lcdFrame"));
    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(10, 10, 10, 10);
    containerLayout->setAlignment(Qt::AlignCenter);

    m_screen = new GBScreenWidget(container);
    m_screen->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    containerLayout->addWidget(m_screen, 1);
    setCentralWidget(container);

    m_debugPanel = new DebugPanel(this);
    m_debugDock = new QDockWidget(tr("Debugger"), this);
    m_debugDock->setObjectName(QStringLiteral("debugDock"));
    m_debugDock->setWidget(m_debugPanel);
    m_debugDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_debugDock->setVisible(false); // hidden by default
    addDockWidget(Qt::RightDockWidgetArea, m_debugDock);

    statusBar()->addWidget(m_stateLabel);
    statusBar()->addWidget(m_fpsLabel);
    statusBar()->addPermanentWidget(m_romLabel);

    // Right click on the LCD opens a context menu with every essential action,
    // which is also the way to bring the toolbars back after hiding them.
    m_screen->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_screen, &GBScreenWidget::customContextMenuRequested, this,
            &GameWindow::showScreenContextMenu);

    // Restore the toolbar / menu bar visibility from the settings. The
    // toolbar is hidden by default when a game window is created.
    QSettings settings;
    setToolBarVisible(settings.value(QStringLiteral("ui/showToolBar"), false).toBool());
    setMenuBarVisible(settings.value(QStringLiteral("ui/showMenuBar"), true).toBool());
}

void GameWindow::applyKeyBindings()
{
    QSettings settings;
    m_screen->setKeyBindings(loadKeyBindings(settings));
    m_screen->setActionBindings(loadUiBindings(settings));
}

void GameWindow::buildStyleActions()
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
    const QString current =
        settings.value(QStringLiteral("ui/theme"), defaultThemeName()).toString();
    const QString customStyle =
        settings.value(QStringLiteral("ui/customStyle")).toString();

    bool customActive = false;
    const QStringList themes = availableThemes();
    for (const QString &name : themes) {
        QAction *action = m_styleMenu->addAction(name);
        action->setCheckable(true);
        const bool active = (name == current) && customStyle.isEmpty();
        action->setChecked(active);
        customActive = customActive || action->isChecked();
        action->setStatusTip(tr("Switch to the %1 style").arg(name));
        m_styleGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, name]() { applyStyle(name); });
    }

    m_styleMenu->addSeparator();

    QAction *customAction = m_styleMenu->addAction(tr("Load &custom style..."));
    customAction->setCheckable(true);
    customAction->setChecked(!customStyle.isEmpty() && !customActive);
    customAction->setStatusTip(tr("Apply a stylesheet file of your own"));
    m_styleGroup->addAction(customAction);
    connect(customAction, &QAction::triggered, this, [this, customAction]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open a custom style"), QString(),
            tr("Qt stylesheet (*.qss);;All files (*)"));
        if (path.isEmpty()) {
            // Cancel: fall back to the previous theme.
            applySavedTheme();
            buildStyleActions();
            return;
        }
        applyThemeFile(path);
        QSettings settings;
        settings.setValue(QStringLiteral("ui/customStyle"), path);
        settings.setValue(QStringLiteral("ui/theme"), QFileInfo(path).fileName());
        customAction->setChecked(true);
    });
}

void GameWindow::applyPaletteFromSettings()
{
    QSettings settings;
    int mode = settings.value(QStringLiteral("display/paletteMode"),
                              static_cast<int>(gb::DisplayPalette::Dmg)).toInt();
    if (mode < 0 || mode > 2) {
        mode = static_cast<int>(gb::DisplayPalette::Dmg);
    }

    QAction *target = nullptr;
    switch (static_cast<gb::DisplayPalette>(mode)) {
    case gb::DisplayPalette::Dmg:
        target = m_paletteDmgAction;
        break;
    case gb::DisplayPalette::Gbc:
        target = m_paletteGbcAction;
        break;
    case gb::DisplayPalette::Custom:
        target = m_paletteCustomAction;
        break;
    }
    if (target) {
        target->setChecked(true);
    }

    bool ok = false;
    const unsigned int stored = settings.value(QStringLiteral("display/palette0")).toUInt(&ok);
    if (!ok || stored == 0u) {
        // First run: remember the DMG greens so the custom editor has a base.
        constexpr unsigned int kDefaultShades[4] = {
            0x9BBC0FFFu, 0x8BAC0FFFu, 0x306230FFu, 0x0F380FFFu
        };
        for (int index = 0; index < 4; ++index) {
            settings.setValue(QStringLiteral("display/palette%1").arg(index),
                              kDefaultShades[index]);
        }
    }

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setPalette", Qt::QueuedConnection,
                                  Q_ARG(int, mode));
        const unsigned int c0 = settings.value(QStringLiteral("display/palette0"),
                                               0x9BBC0FFFu).toUInt();
        const unsigned int c1 = settings.value(QStringLiteral("display/palette1"),
                                               0x8BAC0FFFu).toUInt();
        const unsigned int c2 = settings.value(QStringLiteral("display/palette2"),
                                               0x306230FFu).toUInt();
        const unsigned int c3 = settings.value(QStringLiteral("display/palette3"),
                                               0x0F380FFFu).toUInt();
        QMetaObject::invokeMethod(m_worker, "setCustomPalette", Qt::QueuedConnection,
                                  Q_ARG(unsigned int, c0), Q_ARG(unsigned int, c1),
                                  Q_ARG(unsigned int, c2), Q_ARG(unsigned int, c3));
    }
}

void GameWindow::startEmulationThread()
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("emulationThread"));

    m_worker = new EmulationWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &EmulationWorker::frameReady, this, &GameWindow::onFrameReady);
    connect(m_worker, &EmulationWorker::romLoaded, this, &GameWindow::onRomLoaded);
    connect(m_worker, &EmulationWorker::romFailed, this, &GameWindow::onRomFailed);
    connect(m_worker, &EmulationWorker::statisticsUpdated, this, &GameWindow::onStatisticsUpdated);
    connect(m_worker, &EmulationWorker::runningChanged, this, &GameWindow::onRunningChanged);
    connect(m_worker, &EmulationWorker::stateSaved, this, &GameWindow::onStateSaved);
    connect(m_worker, &EmulationWorker::stateSaveFailed, this, &GameWindow::onStateSaveFailed);
    connect(m_worker, &EmulationWorker::stateLoaded, this, &GameWindow::onStateLoaded);
    connect(m_worker, &EmulationWorker::stateLoadFailed, this, &GameWindow::onStateLoadFailed);
    connect(m_screen, &GBScreenWidget::buttonChanged, this, &GameWindow::onButtonChanged);
    connect(m_screen, &GBScreenWidget::uiActionTriggered, this, &GameWindow::onUiAction);

    m_thread->start();

    QMetaObject::invokeMethod(m_worker, "initialize", Qt::QueuedConnection);

    m_screen->setEmulator(m_worker->emulator());
    m_debugPanel->setEmulator(m_worker->emulator());
}

void GameWindow::stopEmulationThread()
{
    if (!m_thread || !m_worker) {
        return;
    }

    QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();

    m_worker = nullptr;
    m_thread = nullptr;
}

void GameWindow::setZoomScale(int scale)
{
    m_zoomScale = scale;
    if (scale <= 0) {
        // Fit mode: make the screen widget resizable again and let the
        // container layout center it inside the window.
        m_screen->setMinimumSize(gb::kScreenWidth, gb::kScreenHeight);
        m_screen->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        m_screen->updateGeometry();
        return;
    }
    m_screen->setFixedSize(gb::kScreenWidth * scale, gb::kScreenHeight * scale);
}

QString GameWindow::savePathFor(const QString &romPath)
{
    const QFileInfo info(romPath);
    return info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".sav"));
}

QString GameWindow::currentSavePath() const
{
    return m_currentRom.isEmpty() ? QString() : savePathFor(m_currentRom);
}

QString GameWindow::statePathFor(const QString &romPath, int slot)
{
    const QFileInfo info(romPath);
    const QString suffix = slot == 1
        ? QStringLiteral(".state")
        : QStringLiteral(".state%1").arg(slot);
    return info.absoluteDir().filePath(info.completeBaseName() + suffix);
}

QString GameWindow::currentStatePath(int slot) const
{
    return m_currentRom.isEmpty() ? QString() : statePathFor(m_currentRom, slot);
}

} // namespace ui
