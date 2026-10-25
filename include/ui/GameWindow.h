/**
 * @file GameWindow.h
 * @brief Window that plays one ROM: screen, menus, toolbar, status bar and
 *        the emulation thread. The debugger dock is available but hidden.
 *
 * A GameWindow is created by the library MainWindow when the user presses
 * Start. It owns its own emulation thread so several windows cannot block
 * each other.
 */

#ifndef GB_UI_GAMEWINDOW_H
#define GB_UI_GAMEWINDOW_H

#include "ui/KeyBindings.h"

#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <array>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QSlider;
class QThread;
class QTimer;
class QDockWidget;
class QToolBar;

namespace gb {
class Emulation;
}

namespace ui {

class GBScreenWidget;
class DebugPanel;
class EmulationWorker;
class SettingsDialog;

/**
 * @brief Main window of one emulation session.
 */
class GameWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Constructs the game window and starts the emulation thread.
     * @param romPath Optional ROM to load immediately.
     * @param parent Parent widget.
     */
    explicit GameWindow(const QString &romPath = QString(), QWidget *parent = nullptr);

    /**
     * @brief Stops the emulation thread and saves the battery RAM.
     */
    ~GameWindow() override;

    /**
     * @brief Loads a ROM, asking for confirmation if one is already open.
     * @param path Path of the .gb file.
     */
    void openRom(const QString &path);

protected:
    /**
     * @brief Saves the battery RAM before the window is closed.
     * @param event The close event.
     */
    void closeEvent(QCloseEvent *event) override;

private slots:
    /**
     * @brief Handles a global action binding (pause, fullscreen, exit).
     * @param action The triggered action.
     */
    void onUiAction(UiAction action);

    /**
     * @brief Shows an open file dialog and loads the selected ROM.
     */
    void chooseRom();

    /**
     * @brief Reloads the current ROM from disk.
     */
    void reloadRom();

    /**
     * @brief Powers the machine on again.
     */
    void resetEmulation();

    /**
     * @brief Toggles between running and paused.
     */
    void togglePause();

    /**
     * @brief Executes one instruction while paused.
     */
    void stepInstruction();

    /**
     * @brief Executes one frame while paused.
     */
    void stepFrame();

    /**
     * @brief Writes the battery RAM to the .sav file next to the ROM.
     */
    void saveBatteryRam();

    /**
     * @brief Saves a PNG of the current framebuffer.
     */
    void takeScreenshot();

    /**
     * @brief Shows or hides the debug panel.
     * @param visible True to show the panel.
     */
    void setDebugPanelVisible(bool visible);

    /**
     * @brief Toggles the full screen mode.
     */
    void toggleFullScreen();

    /**
     * @brief Shows or hides the toolbar.
     * @param visible True to show the toolbar.
     */
    void setToolBarVisible(bool visible);

    /**
     * @brief Shows or hides the menu bar.
     * @param visible True to show the menu bar.
     */
    void setMenuBarVisible(bool visible);

    /**
     * @brief Opens the settings dialog (controls, theme).
     */
    void openSettings();

    /**
     * @brief Applies a palette and remembers it in the settings.
     * @param mode DisplayPalette value (0 = DMG, 1 = GBC, 2 = custom).
     */
    void applyPaletteMode(int mode);

    /**
     * @brief Opens the custom colour dialog and applies the result.
     */
    void editCustomPalette();

    /**
     * @brief Applies a stylesheet theme.
     * @param name Theme base name.
     */
    void applyStyle(const QString &name);

    /**
     * @brief Shows the context menu of the LCD.
     * @param pos Position of the right click inside the screen widget.
     */
    void showScreenContextMenu(const QPoint &pos);

    /**
     * @brief Shows the about dialog.
     */
    void showAbout();

    /**
     * @brief Updates the screen widget with a new frame.
     */
    void onFrameReady(const QByteArray &frame, quint64 frameNumber);

    /**
     * @brief Updates the status bar after a ROM has been loaded.
     */
    void onRomLoaded(const QString &title, const QString &mapper, int romBanks, int ramBanks);

    /**
     * @brief Shows an error message when a ROM cannot be loaded.
     * @param message The error message.
     */
    void onRomFailed(const QString &message);

    /**
     * @brief Updates the FPS label.
     * @param fps Measured frames per second.
     */
    void onStatisticsUpdated(double fps);

    /**
     * @brief Updates the actions that depend on the running state.
     * @param running True when the emulation is running.
     */
    void onRunningChanged(bool running);

    /**
     * @brief Refreshes the debug panel (only while it is visible).
     */
    void refreshDebugPanel();

    /**
     * @brief Forwards a joypad change to the emulation thread.
     * @param index Button index.
     * @param pressed True when the button is held down.
     */
    void onButtonChanged(int index, bool pressed);

private:
    /**
     * @brief Builds the menus and their actions.
     */
    void createActions();

    /**
     * @brief Builds the toolbar.
     */
    void createToolBar();

    /**
     * @brief Builds the central widget, the dock and the status bar.
     */
    void createLayout();

    /**
     * @brief Rebuilds the Style submenu from the available themes.
     */
    void buildStyleActions();

    /**
     * @brief Applies the saved keyboard bindings to the screen widget.
     */
    void applyKeyBindings();

    /**
     * @brief Applies the saved display palette to the emulation thread.
     */
    void applyPaletteFromSettings();

    /**
     * @brief Starts the emulation thread and moves the worker to it.
     */
    void startEmulationThread();

    /**
     * @brief Stops the emulation thread and waits for it to finish.
     */
    void stopEmulationThread();

    /**
     * @brief Changes the fixed scaling of the screen widget.
     * @param scale Scale factor, 0 = fit the window.
     */
    void setZoomScale(int scale);

    /**
     * @brief Returns the path of the .sav file of a ROM.
     * @param romPath Path of the ROM.
     * @return The save file path (same name, .sav extension).
     */
    static QString savePathFor(const QString &romPath);

    /**
     * @brief Returns the path of the battery RAM file of the current ROM.
     * @return The save file path or an empty string.
     */
    QString currentSavePath() const;

    QThread *m_thread = nullptr;            /**< Emulation thread. */
    EmulationWorker *m_worker = nullptr;    /**< Worker living on the thread. */
    GBScreenWidget *m_screen = nullptr;     /**< LCD widget. */
    DebugPanel *m_debugPanel = nullptr;     /**< Debugger dock content. */
    QDockWidget *m_debugDock = nullptr;     /**< Debugger dock. */
    QTimer *m_debugTimer = nullptr;         /**< Timer that refreshes the debugger. */
    QLabel *m_stateLabel = nullptr;         /**< Status bar: running / paused. */
    QLabel *m_fpsLabel = nullptr;           /**< Status bar: frames per second. */
    QLabel *m_romLabel = nullptr;           /**< Status bar: cartridge title. */
    QAction *m_openAction = nullptr;        /**< File > Open. */
    QAction *m_reloadAction = nullptr;      /**< File > Reload. */
    QAction *m_saveAction = nullptr;        /**< File > Save RAM. */
    QAction *m_quitAction = nullptr;        /**< File > Quit. */
    QAction *m_resetAction = nullptr;       /**< Emulation > Reset. */
    QAction *m_pauseAction = nullptr;       /**< Emulation > Pause. */
    QAction *m_stepAction = nullptr;        /**< Emulation > Step instruction. */
    QAction *m_frameAction = nullptr;       /**< Emulation > Step frame. */
    QAction *m_screenshotAction = nullptr;  /**< Emulation > Screenshot. */
    QAction *m_debugAction = nullptr;       /**< View > Debugger. */
    QAction *m_fullscreenAction = nullptr;  /**< View > Full screen. */
    QAction *m_toolbarAction = nullptr;     /**< View > Toolbar. */
    QAction *m_menubarAction = nullptr;     /**< View > Menu bar. */
    QAction *m_fitAction = nullptr;         /**< View > Zoom > Fit window. */
    QAction *m_settingsAction = nullptr;    /**< Settings action. */
    QAction *m_aboutAction = nullptr;       /**< Help > About. */
    QAction *m_aboutQtAction = nullptr;     /**< Help > About Qt. */
    QAction *m_paletteDmgAction = nullptr;  /**< Palette > Original DMG. */
    QAction *m_paletteGbcAction = nullptr;  /**< Palette > Game Boy Color. */
    QAction *m_paletteCustomAction = nullptr; /**< Palette > Custom. */
    QAction *m_editPaletteAction = nullptr;   /**< Palette > Edit custom colours. */
    QActionGroup *m_paletteGroup = nullptr;   /**< Exclusive palette modes. */
    QActionGroup *m_styleGroup = nullptr;     /**< Exclusive theme actions. */
    QSlider *m_speedSlider = nullptr;       /**< Toolbar speed slider. */
    QLabel *m_speedLabel = nullptr;         /**< Toolbar speed label. */
    QToolBar *m_toolBar = nullptr;          /**< The emulation toolbar. */
    QMenu *m_styleMenu = nullptr;           /**< Style submenu. */
    QString m_currentRom;                   /**< Path of the loaded ROM. */
    bool m_running = false;                 /**< Last known running state. */
    int m_zoomScale = 0;                    /**< Fixed scale, 0 = fit. */
};

} // namespace ui

#endif // GB_UI_GAMEWINDOW_H
