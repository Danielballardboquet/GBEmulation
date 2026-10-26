/**
 * @file AudioOutput.h
 * @brief Qt audio sink that plays the mono PCM samples produced by the
 *        emulator's AudioUnit through the operating system's default output
 *        device.
 */

#ifndef GB_UI_AUDIOOUTPUT_H
#define GB_UI_AUDIOOUTPUT_H

#include <QByteArray>
#include <QIODevice>
#include <QObject>

#include <cstdint>
#include <memory>
#include <vector>

class QAudioSink;

namespace ui {

/**
 * @brief Small ring buffer exposed as a QIODevice so QAudioSink can pull
 *        samples from it in push mode without ever blocking the emulation
 *        thread that feeds it.
 */
class AudioRingBuffer : public QIODevice {
    Q_OBJECT

public:
    explicit AudioRingBuffer(QObject *parent = nullptr);

    /**
     * @brief Appends freshly rendered samples, dropping the oldest buffered
     *        audio when the backlog grows too large so playback latency
     *        never accumulates (this keeps the emulation feeling responsive
     *        even if a frame occasionally takes longer to render).
     * @param bytes Raw little-endian 16-bit PCM bytes.
     */
    void append(const QByteArray &bytes);

    /** @brief Discards any buffered-but-unplayed audio. */
    void clear();

    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;
    qint64 bytesAvailable() const override;

private:
    QByteArray m_buffer; /**< Pending PCM bytes waiting to be played. */
};

/**
 * @brief Owns the QAudioSink that renders the emulator's audio in real time.
 *
 * The object must live on a thread that runs a Qt event loop (the emulation
 * worker thread qualifies) because QAudioSink delivers data asynchronously
 * through that loop.
 */
class AudioOutput : public QObject {
    Q_OBJECT

public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput() override;

    /**
     * @brief Pushes newly rendered mono samples to the output device.
     * @param samples Signed 16-bit samples at the emulator's sample rate.
     */
    void pushSamples(const std::vector<std::int16_t> &samples);

    /**
     * @brief Sets the output volume.
     * @param volume Linear volume in the 0.0-1.0 range.
     */
    void setVolume(qreal volume);

    /**
     * @brief Mutes or unmutes the output without losing the volume setting.
     * @param muted True to silence the output.
     */
    void setMuted(bool muted);

    /**
     * @brief Drops any audio queued from a previous ROM so it never bleeds
     *        into the next one after loading or resetting a cartridge.
     */
    void reset();

private:
    std::unique_ptr<QAudioSink> m_sink;   /**< Qt audio output device. */
    AudioRingBuffer *m_buffer = nullptr;  /**< Backing store fed to the sink. */
    bool m_muted = false;                 /**< Current mute state. */
};

} // namespace ui

#endif // GB_UI_AUDIOOUTPUT_H
