/**
 * @file main.cpp
 * @brief Application entry point of the Game Boy emulator.
 *
 * Sets up the QApplication, registers the application metadata used by the
 * QSettings based storage and shows the library window. An optional command
 * line argument starts the given ROM immediately in its own game window.
 *
 * @see https://doc.qt.io/qt-6/qapplication.html   QApplication overview
 * @see https://doc.qt.io/qt-6/qsettings.html      QSettings storage
 */

#include "ui/GameWindow.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QString>

/**
 * @brief Entry point of the emulator.
 * @param argc Number of command line arguments.
 * @param argv The command line arguments.
 * @return The application exit code.
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("GB_emulator"));
    QCoreApplication::setApplicationName(QStringLiteral("GB_emulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    ui::MainWindow library;
    library.show();

    // A command line ROM argument starts the game immediately.
    if (argc > 1) {
        const QString romPath = QString::fromLocal8Bit(argv[1]);
        ui::GameWindow *game = new ui::GameWindow(romPath);
        game->show();
    }

    return app.exec();
}
