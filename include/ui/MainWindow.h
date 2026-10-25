/**
 * @file MainWindow.h
 * @brief Launcher window of the emulator: it lists the ROM files found in the
 *        library folders and starts a GameWindow for the selected game.
 *
 * The library scans the ROM/ folder next to the executable plus every folder
 * configured in the settings, recursively, and shows the .gb/.gbc files in a
 * list. Pressing Start (or double clicking an entry) opens a new GameWindow
 * where the game plays, so several games can be open at the same time.
 *
 * The window also loads the game database (data/games_database.json) on
 * startup, matches each ROM to its database entry by title and shows rich
 * details (full name, developer/publisher, year, genre, rating, description)
 * in the detail panel. The library can be filtered with the search box and
 * sorted by name, year or genre.
 */

#ifndef GB_UI_MAINWINDOW_H
#define GB_UI_MAINWINDOW_H

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;

namespace ui {

class GameWindow;

/**
 * @brief Launcher window with the ROM library.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Constructs the launcher and scans the library.
     * @param parent Parent widget.
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief Destroys the launcher.
     */
    ~MainWindow() override;

protected:
    /**
     * @brief Refreshes the recent files menu when the window is shown.
     * @param event The show event.
     */
    void showEvent(QShowEvent *event) override;

private slots:
    /**
     * @brief Starts the selected game in a new window.
     */
    void startSelectedGame();

    /**
     * @brief Starts the game at the given path in a new window.
     * @param path Path of the ROM.
     */
    void startGame(const QString &path);

    /**
     * @brief Shows an open file dialog and starts the selected ROM.
     */
    void chooseRom();

    /**
     * @brief Rescans the library folders.
     */
    void refreshLibrary();

    /**
     * @brief Opens the settings dialog.
     */
    void openSettings();

    /**
     * @brief Shows the about dialog.
     */
    void showAbout();

    /**
     * @brief Updates the detail panel for the selected ROM.
     */
    void updateDetail();

    /**
     * @brief Opens one of the recent files.
     */
    void openRecentFile();

private:
    /**
     * @brief Builds the menus and their actions.
     */
    void createActions();

    /**
     * @brief Builds the central widget.
     */
    void createLayout();

    /**
     * @brief Rebuilds the Style submenu inside the Settings menu.
     */
    void buildStyleMenu();

    /**
     * @brief Returns the folders that are scanned for ROMs.
     * @return The user configured folders plus the default ROM folder.
     */
    QStringList libraryFolders() const;

    /**
     * @brief Returns a human readable description of a ROM header.
     * @param path Path of the ROM.
     * @return A string like "POKEMON BLUE - MBC3 + Battery - 1024 KiB".
     */
    static QString romDescription(const QString &path);

    /**
     * @brief Rescans the library folders and repopulates the list.
     */
    void scanLibrary();

    /**
     * @brief Loads the game database (data/games_database.json).
     */
    void loadGameDatabase();

    /**
     * @brief Returns the database entry matching a ROM file, matched by header
     *        title first and by file name as a fallback.
     * @param path Path of the ROM.
     * @return The matching game entry, or an empty object when unknown.
     */
    const QJsonObject gameInfoForPath(const QString &path) const;

    /**
     * @brief Applies the search filter and the sort order to the ROM list.
     */
    void applyFilterAndSort();

    /**
     * @brief Loads the recent files list from the settings.
     */
    void loadRecentFiles();

    /**
     * @brief Stores the recent files list in the settings.
     */
    void saveRecentFiles();

    /**
     * @brief Rebuilds the recent files menu.
     */
    void updateRecentFilesMenu();

    /**
     * @brief Adds a path to the recent files list.
     * @param path The ROM path to remember.
     */
    void addRecentFile(const QString &path);

    QListWidget *m_romList = nullptr;       /**< ROM library list. */
    QLabel *m_detailLabel = nullptr;        /**< Detail text of the selection. */
    QLabel *m_countLabel = nullptr;         /**< Status label with the ROM count. */
    QLineEdit *m_searchEdit = nullptr;      /**< Search/filter box. */
    QComboBox *m_sortCombo = nullptr;       /**< Sort order selector. */
    QAction *m_startAction = nullptr;       /**< Start selected game. */
    QAction *m_openAction = nullptr;        /**< Open ROM file. */
    QAction *m_refreshAction = nullptr;     /**< Rescan library. */
    QAction *m_settingsAction = nullptr;    /**< Open settings. */
    QAction *m_aboutAction = nullptr;       /**< About dialog. */
    QActionGroup *m_styleGroup = nullptr;   /**< Exclusive style actions. */
    QMenu *m_styleMenu = nullptr;           /**< Style submenu in Settings. */
    QMenu *m_recentMenu = nullptr;          /**< File > Recent files. */
    QStringList m_recentFiles;              /**< Recently opened ROMs. */
    QStringList m_romFiles;                 /**< Absolute paths of the library. */
    QHash<QString, QJsonObject> m_games;    /**< Game database keyed by title. */
};

} // namespace ui

#endif // GB_UI_MAINWINDOW_H
