/**
 * @file EmulationWorker.cpp
 * @brief Implementation of the emulation thread worker.
 *
 * @see https://doc.qt.io/qt-6/qthread.html          QThread worker pattern
 * @see https://doc.qt.io/qt-6/signalsandslots.html  Qt signals and slots
 * @see https://gbdev.io/pandocs/Video_Processing.html  Frame timing used to pace the loop
 */

#include "ui/EmulationWorker.h"

#include "emulation/Emulation.h"
#include "emulation/Types.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ui {

namespace {

/** @brief Minimum period between two FPS statistics updates in milliseconds. */
constexpr int kFpsMeasureIntervalMs = 500;

} // namespace

EmulationWorker::EmulationWorker(QObject *parent)
    : QObject(parent)
    , m_emulator(std::make_unique<gb::Emulation>())
{
}

EmulationWorker::~EmulationWorker() = default;

void EmulationWorker::initialize()
{
    if (m_loopTimer) {
        return;
    }
    m_loopTimer = new QTimer(this);
    m_loopTimer->setTimerType(Qt::PreciseTimer);
    m_loopTimer->setInterval(10);
    connect(m_loopTimer, &QTimer::timeout, this, &EmulationWorker::processFrame);
}

gb::Emulation *EmulationWorker::emulator()
{
    return m_emulator.get();
}

void EmulationWorker::loadRom(const QString &path)
{
    if (m_loopTimer) {
        m_loopTimer->stop();
    }
    m_running.store(false);
    m_framesSinceMeasure = 0;
    if (m_fpsTimer.isValid()) {
        m_fpsTimer.invalidate();
    }

    std::string error;
    if (!m_emulator->loadRomFile(path.toStdString(), error)) {
        emit romFailed(QString::fromStdString(error));
        return;
    }

    m_romPath = path;

    // Restore the battery backed RAM from the .sav file next to the ROM.
    const QFileInfo info(m_romPath);
    const QString savePath = info.absoluteDir().filePath(
        info.completeBaseName() + QStringLiteral(".sav"));
    QFile saveFile(savePath);
    if (saveFile.exists() && saveFile.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = saveFile.readAll();
        m_emulator->loadSaveData(std::vector<gb::u8>(bytes.constBegin(), bytes.constEnd()));
    }

    m_emulator->reset();
    m_running.store(true);
    emit runningChanged(true);

    const gb::Cartridge &cart = m_emulator->cartridge();
    emit romLoaded(QString::fromStdString(cart.title),
                   QString::fromUtf8(gb::Emulation::mapperName(cart.mapper)),
                   cart.romBanks,
                   cart.ramBanks);

    if (m_loopTimer) {
        m_loopTimer->start();
    }
}

void EmulationWorker::reset()
{
    if (!m_emulator) {
        return;
    }
    m_emulator->reset();
    m_running.store(true);
    emit runningChanged(true);
    if (m_loopTimer) {
        m_loopTimer->start();
    }
}

void EmulationWorker::setRunning(bool running)
{
    if (!m_loopTimer) {
        m_running.store(running);
        return;
    }

    const bool wasRunning = m_running.exchange(running);
    if (wasRunning == running) {
        if (running) {
            m_loopTimer->start();
        }
        return;
    }

    m_framesSinceMeasure = 0;
    if (m_fpsTimer.isValid()) {
        m_fpsTimer.invalidate();
    }

    if (running) {
        m_loopTimer->start();
    } else {
        m_loopTimer->stop();
    }
    emit runningChanged(running);
}

void EmulationWorker::stepInstruction()
{
    if (m_running.load() || !m_emulator || !m_emulator->isRunning()) {
        return;
    }
    m_emulator->step();
    emitFrame();
}

void EmulationWorker::stepFrame()
{
    if (m_running.load() || !m_emulator || !m_emulator->isRunning()) {
        return;
    }
    m_emulator->runFrame();
    emitFrame();
}

void EmulationWorker::setSpeed(int percent)
{
    m_speed.store(qBound(10, percent, 400));
}

void EmulationWorker::setPalette(int mode)
{
    if (!m_emulator) {
        return;
    }
    m_emulator->setDisplayPalette(static_cast<gb::DisplayPalette>(mode));
}

void EmulationWorker::setCustomPalette(unsigned int c0, unsigned int c1,
                                       unsigned int c2, unsigned int c3)
{
    if (!m_emulator) {
        return;
    }
    m_emulator->setCustomPaletteColors({c0, c1, c2, c3});
}

void EmulationWorker::setButton(int index, bool pressed)
{
    if (index < 0 || index > 7) {
        return;
    }
    unsigned int buttons = m_buttons.load(std::memory_order_relaxed);
    if (pressed) {
        buttons = buttons | (1u << index);
    } else {
        buttons = buttons & ~(1u << index);
    }
    m_buttons.store(buttons, std::memory_order_relaxed);
}

void EmulationWorker::saveBatteryRam(const QString &path)
{
    if (!m_emulator || path.isEmpty()) {
        return;
    }
    const std::vector<gb::u8> &data = m_emulator->saveData();
    if (data.empty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(reinterpret_cast<const char *>(data.data()), static_cast<qint64>(data.size()));
}

void EmulationWorker::loadBatteryRam(const QString &path)
{
    if (!m_emulator || path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray bytes = file.readAll();
    m_emulator->loadSaveData(std::vector<gb::u8>(bytes.constBegin(), bytes.constEnd()));
}

void EmulationWorker::saveState(const QString &path)
{
    if (!m_emulator || path.isEmpty()) {
        emit stateSaveFailed(tr("No emulator is available"));
        return;
    }
    const std::vector<gb::u8> state = m_emulator->saveState();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit stateSaveFailed(tr("Cannot open %1 for writing").arg(path));
        return;
    }
    const qint64 written = file.write(reinterpret_cast<const char *>(state.data()),
                                      static_cast<qint64>(state.size()));
    if (written != static_cast<qint64>(state.size())) {
        emit stateSaveFailed(tr("Could not write the state file %1").arg(path));
        return;
    }
    emit stateSaved(path);
}

void EmulationWorker::loadState(const QString &path)
{
    if (!m_emulator || path.isEmpty()) {
        emit stateLoadFailed(tr("No emulator is available"));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit stateLoadFailed(tr("Cannot open %1 for reading").arg(path));
        return;
    }
    const QByteArray bytes = file.readAll();
    std::string error;
    if (!m_emulator->loadState(
            std::vector<gb::u8>(bytes.constBegin(), bytes.constEnd()), error)) {
        emit stateLoadFailed(QString::fromStdString(error));
        return;
    }
    // Refresh the screen with the restored framebuffer.
    emitFrame();
    emit stateLoaded(path);
}

void EmulationWorker::shutdown()
{
    m_shutdown.store(true);
    m_running.store(false);
    if (m_loopTimer) {
        m_loopTimer->stop();
    }
}

void EmulationWorker::processFrame()
{
    if (m_shutdown.load() || !m_running.load()) {
        return;
    }
    if (!m_loopTimer) {
        return;
    }
    if (!m_emulator || !m_emulator->isRunning()) {
        m_loopTimer->stop();
        return;
    }

    runFrameLoop();

    if (m_running.load() && !m_shutdown.load()) {
        m_loopTimer->start();
    }
}

void EmulationWorker::runFrameLoop()
{
    if (!m_emulator || !m_emulator->isRunning()) {
        return;
    }

    QElapsedTimer clock;
    clock.start();

    // Copy the atomic joypad bitmap into the emulator once per frame.
    const unsigned int buttons = m_buttons.load(std::memory_order_relaxed);
    for (int index = 0; index < 8; ++index) {
        m_emulator->setButton(index, ((buttons >> index) & 1u) != 0);
    }

    m_emulator->runFrame();
    ++m_framesSinceMeasure;
    emitFrame();

    // Sleep for the remainder of the frame slot so the average frame rate
    // matches the selected speed (10..400 percent of the real hardware).
    const double speedFactor = static_cast<double>(m_speed.load()) / 100.0;
    const qint64 slotMicros = static_cast<qint64>(1000000.0 / (gb::kFramesPerSecond * speedFactor));
    qint64 remaining = slotMicros - clock.nsecsElapsed() / 1000;
    while (remaining > 200 && m_running.load() && !m_shutdown.load()) {
        QThread::usleep(static_cast<ulong>(qMin<qint64>(remaining, 5000)));
        remaining = slotMicros - clock.nsecsElapsed() / 1000;
    }

    if (m_fpsTimer.isValid() && m_fpsTimer.elapsed() >= kFpsMeasureIntervalMs) {
        const double fps = static_cast<double>(m_framesSinceMeasure) * 1000.0
                           / static_cast<double>(m_fpsTimer.restart());
        m_framesSinceMeasure = 0;
        emit statisticsUpdated(fps);
    } else if (!m_fpsTimer.isValid()) {
        m_fpsTimer.start();
    }
}

void EmulationWorker::emitFrame()
{
    if (!m_emulator) {
        return;
    }
    const gb::u8 *buffer = m_emulator->frameBuffer();
    QByteArray frame(reinterpret_cast<const char *>(buffer), static_cast<int>(gb::kFrameBufferBytes));
    emit frameReady(frame, m_emulator->frameCount());
}

} // namespace ui
