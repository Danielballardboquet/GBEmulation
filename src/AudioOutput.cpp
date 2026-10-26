/**
 * @file AudioOutput.cpp
 * @brief Implementation of the Qt/emulator audio bridge.
 *
 * @see https://doc.qt.io/qt-6/qaudiosink.html     Qt Multimedia audio output
 * @see https://doc.qt.io/qt-6/qiodevice.html      Pull-mode PCM source
 */

#include "ui/AudioOutput.h"

#include "emulation/Types.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <algorithm>
#include <cstring>

namespace ui {

namespace {

/** @brief Upper bound on buffered audio, about a quarter second at 48 kHz
 *         mono 16-bit: enough to absorb scheduling jitter without the
 *         latency between video and audio becoming noticeable. */
constexpr qint64 kMaxBufferedBytes = gb::kSampleRate / 4 * static_cast<int>(sizeof(std::int16_t));

} // namespace

AudioRingBuffer::AudioRingBuffer(QObject *parent)
    : QIODevice(parent)
{
    open(QIODevice::ReadOnly);
}

void AudioRingBuffer::append(const QByteArray &bytes)
{
    m_buffer.append(bytes);
    if (m_buffer.size() > kMaxBufferedBytes) {
        // Drop the oldest samples instead of letting the backlog grow: a
        // short glitch is far less noticeable than audio drifting further
        // and further behind the picture.
        m_buffer.remove(0, static_cast<int>(m_buffer.size() - kMaxBufferedBytes));
    }
    emit readyRead();
}

void AudioRingBuffer::clear()
{
    m_buffer.clear();
}

qint64 AudioRingBuffer::readData(char *data, qint64 maxSize)
{
    const qint64 available = std::min<qint64>(maxSize, m_buffer.size());
    if (available > 0) {
        std::memcpy(data, m_buffer.constData(), static_cast<std::size_t>(available));
        m_buffer.remove(0, static_cast<int>(available));
    }
    if (available < maxSize) {
        // Starve gracefully on silence instead of stalling the sink: fill
        // the remainder with zero so playback keeps a steady clock.
        std::memset(data + available, 0, static_cast<std::size_t>(maxSize - available));
    }
    return maxSize;
}

qint64 AudioRingBuffer::writeData(const char *data, qint64 maxSize)
{
    Q_UNUSED(data);
    Q_UNUSED(maxSize);
    return -1; // Fed exclusively through append(); direct writes are not supported.
}

qint64 AudioRingBuffer::bytesAvailable() const
{
    return m_buffer.size() + QIODevice::bytesAvailable();
}

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    QAudioFormat format;
    format.setSampleRate(gb::kSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    m_buffer = new AudioRingBuffer(this);
    m_sink = std::make_unique<QAudioSink>(device, format, this);
    m_sink->setBufferSize(static_cast<int>(kMaxBufferedBytes));
    m_sink->start(m_buffer);
}

AudioOutput::~AudioOutput()
{
    if (m_sink) {
        m_sink->stop();
    }
}

void AudioOutput::pushSamples(const std::vector<std::int16_t> &samples)
{
    if (samples.empty() || !m_buffer || m_muted) {
        return;
    }
    QByteArray bytes(reinterpret_cast<const char *>(samples.data()),
                      static_cast<int>(samples.size() * sizeof(std::int16_t)));
    m_buffer->append(bytes);
}

void AudioOutput::setVolume(qreal volume)
{
    if (m_sink) {
        m_sink->setVolume(std::clamp(volume, 0.0, 1.0));
    }
}

void AudioOutput::setMuted(bool muted)
{
    m_muted = muted;
}

void AudioOutput::reset()
{
    if (m_buffer) {
        m_buffer->clear();
    }
}

} // namespace ui
