/**
 * @file EmulationWorker.h
 * @brief Worker object that runs the emulation on a dedicated QThread.
 *
 * The worker owns the Emulation instance. It renders frames, pushes them to
 * the GUI thread through a queued signal and polls the joypad atomically so
 * the GUI never blocks on the emulation loop.
 */

#ifndef GB_UI_EMULATIONWORKER_H
#define GB_UI_EMULATIONWORKER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QTimer;

namespace gb {
class Emulation;
}

namespace ui {

class AudioOutput;

/**
 * @brief Owns the emulator and drives the frame loop from a worker thread.
 *
 * The worker is constructed on the GUI thread and moved to a QThread. It
 * communicates with the GUI exclusively through queued signals.
 */
class EmulationWorker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the worker.
     * @param parent Parent object.
     */
    explicit EmulationWorker(QObject *parent = nullptr);

    /**
     * @brief Destroys the worker and its emulator.
     */
    ~EmulationWorker() override;

    /**
     * @brief Returns the emulator owned by the worker.
     * @return Pointer to the emulator (may be null).
     */
    gb::Emulation *emulator();

public slots:
    /**
     * @brief Creates the frame loop timer in the thread the worker lives in.
     *
     * A QTimer belongs to the thread it is created in, so it cannot be created
     * in the constructor. This slot must be invoked once the thread is running.
     */
    void initialize();

    /**
     * @brief Loads a ROM and starts running it.
     * @param path Path of the .gb file.
     */
    void loadRom(const QString &path);

    /**
     * @brief Powers the machine on again with the inserted cartridge.
     */
    void reset();

    /**
     * @brief Starts or pauses the frame loop.
     * @param running True to run, false to pause.
     */
    void setRunning(bool running);

    /**
     * @brief Executes a single CPU instruction while paused.
     */
    void stepInstruction();

    /**
     * @brief Executes a single frame while paused.
     */
    void stepFrame();

    /**
     * @brief Changes the emulation speed.
     * @param percent Speed in percent of the real hardware (10..400).
     */
    void setSpeed(int percent);

    /**
     * @brief Selects the colour source of the four LCD shades.
     * @param mode DisplayPalette value (0 = DMG, 1 = GBC, 2 = custom).
     */
    void setPalette(int mode);

    /**
     * @brief Replaces the four custom shades.
     * @param c0 Lightest colour, RRGGBBAA.
     * @param c3 Darkest colour, RRGGBBAA.
     */
    void setCustomPalette(unsigned int c0, unsigned int c1, unsigned int c2, unsigned int c3);

    /**
     * @brief Sets the state of one joypad button.
     * @param index Button index (0 = right ... 7 = start).
     * @param pressed True when the button is held down.
     */
    void setButton(int index, bool pressed);

    /**
     * @brief Writes the battery backed RAM to disk.
     * @param path Path of the .sav file.
     */
    void saveBatteryRam(const QString &path);

    /**
     * @brief Restores the battery backed RAM from disk.
     * @param path Path of the .sav file.
     */
    void loadBatteryRam(const QString &path);

    /**
     * @brief Writes the current machine state to a .state file.
     * @param path Path of the .state file.
     */
    void saveState(const QString &path);

    /**
     * @brief Restores the machine state from a .state file.
     * @param path Path of the .state file.
     */
    void loadState(const QString &path);

    /**
     * @brief Stops the frame loop and releases the emulator.
     */
    void shutdown();

signals:
    /**
     * @brief Emitted once per rendered frame with a copy of the framebuffer.
     * @param frame RGBA8888 pixel data (160*144*4 bytes).
     * @param frameNumber Frame counter of the emulator.
     */
    void frameReady(const QByteArray &frame, quint64 frameNumber);

    /**
     * @brief Emitted when the ROM has been loaded and the machine is running.
     * @param title Cartridge title.
     * @param mapper Mapper name.
     * @param romBanks Number of ROM banks.
     * @param ramBanks Number of RAM banks.
     */
    void romLoaded(const QString &title, const QString &mapper, int romBanks, int ramBanks);

    /**
     * @brief Emitted when a ROM cannot be loaded.
     * @param message The error message.
     */
    void romFailed(const QString &message);

    /**
     * @brief Emitted after every frame with the emulation speed.
     * @param fps Measured frames per second.
     */
    void statisticsUpdated(double fps);

    /**
     * @brief Emitted when the emulation state changes.
     * @param running True when the frame loop is running.
     */
    void runningChanged(bool running);

    /**
     * @brief Emitted after the machine state has been written to disk.
     * @param path Path of the .state file.
     */
    void stateSaved(const QString &path);

    /**
     * @brief Emitted when the machine state could not be written.
     * @param error Description of the failure.
     */
    void stateSaveFailed(const QString &error);

    /**
     * @brief Emitted after the machine state has been restored.
     * @param path Path of the .state file.
     */
    void stateLoaded(const QString &path);

    /**
     * @brief Emitted when the machine state could not be restored.
     * @param error Description of the failure.
     */
    void stateLoadFailed(const QString &error);

private slots:
    /**
     * @brief Body of the frame loop, scheduled by a zero timer.
     */
    void processFrame();

private:
    /**
     * @brief Runs the frame loop until the target time of the frame is reached.
     */
    void runFrameLoop();

    /**
     * @brief Emits the current framebuffer to the GUI thread.
     */
    void emitFrame();

    /**
     * @brief Restarts the frame pacing clock so the next frame runs
     *        immediately instead of waiting out a deadline computed before
     *        the loop was paused or the ROM was (re)loaded.
     */
    void resetPacing();

    std::unique_ptr<gb::Emulation> m_emulator; /**< Emulator instance. */
    std::unique_ptr<AudioOutput> m_audio;      /**< Qt audio output bridge. */
    QTimer *m_loopTimer = nullptr;             /**< Timer that drives the loop. */
    std::atomic<bool> m_running{false};        /**< Frame loop running flag. */
    std::atomic<bool> m_shutdown{false};       /**< Shutdown requested flag. */
    std::atomic<int> m_speed{100};             /**< Speed in percent. */
    std::atomic<unsigned int> m_buttons{0};    /**< Bitmask of held buttons. */
    QElapsedTimer m_fpsTimer;                  /**< Timer used to measure the FPS. */
    int m_framesSinceMeasure = 0;              /**< Frames since the last FPS measure. */
    QElapsedTimer m_pacingClock;                /**< Free running clock used to pace frames. */
    qint64 m_nextFrameDeadlineUs = 0;           /**< Scheduled time of the next frame, in us. */
    QString m_romPath;                         /**< Path of the loaded ROM. */
};

} // namespace ui

#endif // GB_UI_EMULATIONWORKER_H
