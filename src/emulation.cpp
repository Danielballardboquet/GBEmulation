/**
 * @file emulation.cpp
 * @brief Implementation of the top level emulator facade.
 *
 * Emulation owns the memory bus, the CPU, the GPU, the interrupt controller
 * and the audio unit, wires them into each other and drives the frame loop.
 * The audio unit is a simplified square/wave mixer driven by the NR50/NR51
 * registers so games relying on audio feedback keep running.
 */

#include "emulation/Emulation.h"
#include "emulation/Types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace gb {

namespace {

/** @brief Number of cycles needed to produce one audio sample. */
constexpr double kCyclesPerSample = static_cast<double>(kCyclesPerSecond) / kSampleRate;

/** @brief Number of samples kept when the front-end stops draining the buffer. */
constexpr std::size_t kMaxBufferedSamples = 48000;

/**
 * @brief Duty cycle patterns as 8-step waveforms, exactly as the hardware
 *        generates them (step 0 is the first step after the phase resets).
 *
 * Duty 0: 00000001 (12.5%), 1: 10000001 (25%), 2: 10000111 (50%),
 * 3: 01111110 (75%). Using a single "high for the first N eighths" rule
 * gives the right pulse *width* but the wrong *shape*: hardware duty 3 is a
 * pulse that starts low and is high in the middle, and duty 0 is high only
 * on the last step. Getting the shape wrong changes the timbre of every
 * note, which is what made the output sound distorted.
 */
constexpr u8 kDutyWaveforms[4] = {0x80, 0x81, 0xE1, 0x7E};

/** @brief CPU cycles between two 64 Hz volume envelope steps. */
constexpr double kCyclesPerEnvelopeStep = static_cast<double>(kCyclesPerSecond) / 64.0;

/** @brief CPU cycles between two 128 Hz frequency sweep steps. */
constexpr double kCyclesPerSweepStep = static_cast<double>(kCyclesPerSecond) / 128.0;

/** @brief CPU cycles between two 256 Hz length counter steps. */
constexpr double kCyclesPerLengthStep = static_cast<double>(kCyclesPerSecond) / 256.0;

/**
 * @brief Clock divisors of the noise channel, in CPU cycles per LFSR shift.
 *
 * The hardware divides by 8, 16, 32, ... 112 for divisor codes 0..7, and the
 * programmed shift multiplies that by 2^shift.
 */
constexpr int kNoiseDivisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};

/**
 * @brief Coefficient of the one-pole output low pass filter.
 *
 * A square wave sampled at 48 kHz folds a lot of energy back into the
 * audible band; the hardware's analog output stage rolls that off. With
 * y += (x - y) * a and a sample rate of 48 kHz, a = 0.5 puts the -3 dB
 * point at about 11 kHz, which removes the worst of the aliasing while
 * leaving every note the Game Boy can play (up to ~4 kHz) untouched.
 */
constexpr double kOutputFilterCoefficient = 0.5;

/** @brief Magic identifier of a state file: "GBST". */
constexpr char kStateMagic[4] = {'G', 'B', 'S', 'T'};

/** @brief Format version of the state files written by this build. */
constexpr std::uint32_t kStateVersion = 1;

/**
 * @brief Appends a byte to a serialized state.
 * @param out The byte array being built.
 * @param value The value to append.
 */
void appendByte(std::vector<u8> &out, u8 value)
{
    out.push_back(value);
}

/**
 * @brief Appends a little endian 16-bit value to a serialized state.
 * @param out The byte array being built.
 * @param value The value to append.
 */
void appendWord(std::vector<u8> &out, u16 value)
{
    out.push_back(static_cast<u8>(value & 0xFF));
    out.push_back(static_cast<u8>(value >> 8));
}

/**
 * @brief Appends a little endian 32-bit value to a serialized state.
 * @param out The byte array being built.
 * @param value The value to append.
 */
void appendDWord(std::vector<u8> &out, std::uint32_t value)
{
    out.push_back(static_cast<u8>(value & 0xFF));
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<u8>((value >> 24) & 0xFF));
}

/**
 * @brief Appends a little endian 64-bit value to a serialized state.
 * @param out The byte array being built.
 * @param value The value to append.
 */
void appendQWord(std::vector<u8> &out, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<u8>((value >> shift) & 0xFF));
    }
}

/**
 * @brief Appends a block of bytes to a serialized state.
 * @param out The byte array being built.
 * @param data The bytes to append.
 * @param size Number of bytes to append.
 */
void appendBytes(std::vector<u8> &out, const u8 *data, std::size_t size)
{
    if (data != nullptr && size != 0) {
        out.insert(out.end(), data, data + size);
    }
}

/**
 * @brief Sequentially reads the fields written by the append helpers.
 *
 * Every read checks the remaining size so a truncated or corrupt file fails
 * cleanly instead of reading past the end of the buffer.
 */
class StateReader {
public:
    /**
     * @brief Constructs a reader over a byte array.
     * @param data The state bytes.
     * @param size Number of bytes available.
     */
    StateReader(const u8 *data, std::size_t size)
        : m_data(data), m_size(size)
    {
    }

    /**
     * @brief Reads a block of bytes.
     * @param dst Destination buffer.
     * @param size Number of bytes to read.
     * @return False when fewer than size bytes remain.
     */
    bool readBytes(u8 *dst, std::size_t size)
    {
        if (m_pos > m_size || size > m_size - m_pos) {
            return false;
        }
        if (size != 0) {
            std::memcpy(dst, m_data + m_pos, size);
            m_pos += size;
        }
        return true;
    }

    /**
     * @brief Reads a byte.
     * @param value Receives the value.
     * @return False when the input is exhausted.
     */
    bool readByte(u8 &value)
    {
        return readBytes(&value, sizeof(value));
    }

    /**
     * @brief Reads a little endian 16-bit value.
     * @param value Receives the value.
     * @return False when the input is exhausted.
     */
    bool readWord(u16 &value)
    {
        u8 low = 0;
        u8 high = 0;
        if (!readByte(low) || !readByte(high)) {
            return false;
        }
        value = makeWord(high, low);
        return true;
    }

    /**
     * @brief Reads a little endian 32-bit value.
     * @param value Receives the value.
     * @return False when the input is exhausted.
     */
    bool readDWord(std::uint32_t &value)
    {
        u8 b0 = 0;
        u8 b1 = 0;
        u8 b2 = 0;
        u8 b3 = 0;
        if (!readByte(b0) || !readByte(b1) || !readByte(b2) || !readByte(b3)) {
            return false;
        }
        value = static_cast<std::uint32_t>(b0)
                | (static_cast<std::uint32_t>(b1) << 8)
                | (static_cast<std::uint32_t>(b2) << 16)
                | (static_cast<std::uint32_t>(b3) << 24);
        return true;
    }

    /**
     * @brief Reads a little endian 64-bit value.
     * @param value Receives the value.
     * @return False when the input is exhausted.
     */
    bool readQWord(std::uint64_t &value)
    {
        u8 bytes[8] = {};
        if (!readBytes(bytes, sizeof(bytes))) {
            return false;
        }
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(bytes[shift / 8]) << shift;
        }
        return true;
    }

private:
    const u8 *m_data = nullptr; /**< State bytes. */
    std::size_t m_size = 0;     /**< Size of the state bytes. */
    std::size_t m_pos = 0;      /**< Read position. */
};

} // namespace

AudioUnit::AudioUnit()
{
    reset();
}

void AudioUnit::reset()
{
    m_square1 = SquareChannel{};
    m_square2 = SquareChannel{};
    m_wave = WaveChannel{};
    m_noise = NoiseChannel{};
    m_square1.duty = 0;
    m_square1.volume = 0;
    m_square2.duty = 0;
    m_square2.volume = 0;
    m_wave.volume = 0;
    m_noise.volume = 0;
    m_noise.lfsr = 0x7FFF;
    m_nr50 = 0x77;
    m_nr51 = 0xF3;
    m_nr52 = 0xF1;
    m_waveRam.fill(0);
    m_sampleAccumulator = 0.0;
    m_samples.clear();
    m_lengthCounter = 0;
    m_levelSquare1 = 0.0;
    m_levelSquare2 = 0.0;
    m_levelWave = 0.0;
    m_levelNoise = 0.0;
    m_filterState = 0.0;
}

u8 AudioUnit::readRegister(u16 address) const
{
    switch (address) {
    case 0xFF10:
        return static_cast<u8>(0x80 | (m_square1.sweepPeriod << 4)
                               | (m_square1.sweepDirection < 0 ? 0x08 : 0x00)
                               | m_square1.sweepShift);
    case 0xFF11:
        return static_cast<u8>((m_square1.duty << 6) | 0x3F);
    case 0xFF12:
        return static_cast<u8>((m_square1.initialVolume << 4)
                               | (m_square1.envelopeDirection > 0 ? 0x08 : 0x00)
                               | m_square1.envelopePeriod);
    case 0xFF14:
        return static_cast<u8>(0xBF | (m_square1.lengthEnabled ? 0x40 : 0x00)
                               | ((m_square1.frequency >> 8) & 0x07));
    case 0xFF16:
        return static_cast<u8>((m_square2.duty << 6) | 0x3F);
    case 0xFF17:
        return static_cast<u8>((m_square2.initialVolume << 4)
                               | (m_square2.envelopeDirection > 0 ? 0x08 : 0x00)
                               | m_square2.envelopePeriod);
    case 0xFF19:
        return static_cast<u8>(0xBF | (m_square2.lengthEnabled ? 0x40 : 0x00)
                               | ((m_square2.frequency >> 8) & 0x07));
    case 0xFF1A:
        return static_cast<u8>((m_wave.enabled ? 0x80 : 0x00) | 0x7F);
    case 0xFF1C:
        return static_cast<u8>((m_wave.volume << 5) | 0x9F);
    case 0xFF1E:
        return static_cast<u8>(0xBF | (m_wave.lengthEnabled ? 0x40 : 0x00)
                               | ((m_wave.frequency >> 8) & 0x07));
    case 0xFF21:
        return static_cast<u8>((m_noise.initialVolume << 4)
                               | (m_noise.envelopeDirection > 0 ? 0x08 : 0x00)
                               | m_noise.envelopePeriod);
    case 0xFF22:
        return static_cast<u8>((m_noise.shift << 4) | (m_noise.widthMode << 3)
                               | m_noise.divisorCode);
    case 0xFF23:
        return static_cast<u8>(0xBF | (m_noise.lengthEnabled ? 0x40 : 0x00));
    case 0xFF24:
        return m_nr50;
    case 0xFF25:
        return m_nr51;
    case 0xFF26:
        // Bit 7 = power, bits 0-3 = live channel status, bits 4-6 read 1.
        return static_cast<u8>((m_nr52 & 0x80)
                               | (m_square1.enabled ? 0x01 : 0x00)
                               | (m_square2.enabled ? 0x02 : 0x00)
                               | (m_wave.enabled ? 0x04 : 0x00)
                               | (m_noise.enabled ? 0x08 : 0x00)
                               | 0x70);
    default:
        if (address >= 0xFF30 && address <= 0xFF3F) {
            return m_waveRam[address - 0xFF30];
        }
        return 0xFF;
    }
}

void AudioUnit::writeRegister(u16 address, u8 value)
{
    switch (address) {
    case 0xFF10:
        m_square1.sweepPeriod = (value >> 4) & 0x07;
        m_square1.sweepDirection = (value & 0x08) != 0 ? -1 : 1;
        m_square1.sweepShift = value & 0x07;
        break;
    case 0xFF11:
        m_square1.duty = (value >> 6) & 0x03;
        m_square1.length = 64 - (value & 0x3F);
        break;
    case 0xFF12:
        m_square1.initialVolume = (value >> 4) & 0x0F;
        m_square1.envelopeDirection = (value & 0x08) != 0 ? 1 : -1;
        m_square1.envelopePeriod = value & 0x07;
        m_square1.enabled = (value & 0xF8) != 0;
        break;
    case 0xFF13:
        m_square1.frequency = (m_square1.frequency & 0x700) | value;
        break;
    case 0xFF14:
        m_square1.frequency = (m_square1.frequency & 0xFF) | ((value & 0x07) << 8);
        m_square1.lengthEnabled = (value & 0x40) != 0;
        if ((value & 0x80) != 0) {
            m_square1.enabled = true;
            m_square1.phase = 0.0;
            m_square1.volume = m_square1.initialVolume;
            m_square1.envelopeCycles = 0;
            m_square1.sweepCycles = 0;
        }
        break;
    case 0xFF16:
        m_square2.duty = (value >> 6) & 0x03;
        m_square2.length = 64 - (value & 0x3F);
        break;
    case 0xFF17:
        m_square2.initialVolume = (value >> 4) & 0x0F;
        m_square2.envelopeDirection = (value & 0x08) != 0 ? 1 : -1;
        m_square2.envelopePeriod = value & 0x07;
        m_square2.enabled = (value & 0xF8) != 0;
        break;
    case 0xFF18:
        m_square2.frequency = (m_square2.frequency & 0x700) | value;
        break;
    case 0xFF19:
        m_square2.frequency = (m_square2.frequency & 0xFF) | ((value & 0x07) << 8);
        m_square2.lengthEnabled = (value & 0x40) != 0;
        if ((value & 0x80) != 0) {
            m_square2.enabled = true;
            m_square2.phase = 0.0;
            m_square2.volume = m_square2.initialVolume;
            m_square2.envelopeCycles = 0;
        }
        break;
    case 0xFF1A:
        // NR30: bit 7 is the DAC enable. Clearing it stops the channel at
        // once; the channel can only restart through a trigger.
        m_wave.enabled = (value & 0x80) != 0;
        break;
    case 0xFF1B:
        m_wave.length = 256 - value;
        break;
    case 0xFF1C:
        m_wave.volume = (value >> 5) & 0x03;
        break;
    case 0xFF1D:
        m_wave.frequency = (m_wave.frequency & 0x700) | value;
        break;
    case 0xFF1E:
        m_wave.frequency = (m_wave.frequency & 0xFF) | ((value & 0x07) << 8);
        m_wave.lengthEnabled = (value & 0x40) != 0;
        if ((value & 0x80) != 0) {
            m_wave.enabled = true;
            m_wave.phase = 0.0;
        }
        break;
    case 0xFF20:
        m_noise.length = 64 - (value & 0x3F);
        break;
    case 0xFF21:
        m_noise.initialVolume = (value >> 4) & 0x0F;
        m_noise.envelopeDirection = (value & 0x08) != 0 ? 1 : -1;
        m_noise.envelopePeriod = value & 0x07;
        m_noise.enabled = (value & 0xF8) != 0;
        break;
    case 0xFF22:
        m_noise.shift = (value >> 4) & 0x0F;
        m_noise.widthMode = (value >> 3) & 0x01;
        m_noise.divisorCode = value & 0x07;
        break;
    case 0xFF23:
        m_noise.lengthEnabled = (value & 0x40) != 0;
        if ((value & 0x80) != 0) {
            m_noise.enabled = true;
            m_noise.volume = m_noise.initialVolume;
            m_noise.envelopeCycles = 0;
            m_noise.clockCycles = 0;
            // The LFSR restarts all ones; the first shift happens
            // immediately on hardware.
            m_noise.lfsr = 0x7FFF;
            m_noise.output = false;
        }
        break;
    case 0xFF24:
        m_nr50 = value;
        break;
    case 0xFF25:
        m_nr51 = value;
        break;
    case 0xFF26:
        // Only bit 7 is writable; bits 0-3 are the live channel status and
        // are derived from the channels in readRegister().
        m_nr52 = static_cast<u8>((value & 0x80) | (m_nr52 & 0x7F));
        if ((value & 0x80) == 0) {
            // Powering the APU off resets every register and silences all
            // channels, exactly like the hardware.
            const u8 nr52 = m_nr52;
            reset();
            m_nr52 = static_cast<u8>(nr52 & 0x80);
        }
        break;
    default:
        if (address >= 0xFF30 && address <= 0xFF3F) {
            m_waveRam[address - 0xFF30] = value;
        }
        break;
    }
}

void AudioUnit::tick(int cycles)
{
    if (cycles <= 0) {
        return;
    }

    // The four frame sequencer clocks all run off the same cycle counter:
    // length at 256 Hz, sweep at 128 Hz, envelope at 64 Hz.
    m_lengthCounter += static_cast<std::uint32_t>(cycles);
    tickLengthCounters();
    tickEnvelope(m_square1, cycles);
    tickEnvelope(m_square2, cycles);
    tickNoiseEnvelope(cycles);
    tickSweep(cycles);
    tickNoise(cycles);

    // Advance the phase accumulators of the active channels. The period is
    // the number of CPU cycles for one full waveform:
    //   square: (2048 - freq) * 4
    //   wave:   (2048 - freq) * 64
    // The phase is wrapped with a subtraction rather than std::fmod: fmod is
    // exact, so once a period happened to divide the phase exactly the
    // accumulator could land on 0.0 and stay there, freezing the channel on
    // one step (a stuck DC level instead of a tone). The periods are clamped
    // so a pathological frequency cannot divide by zero.
    if (m_square1.enabled) {
        const double period = std::max(4.0, static_cast<double>(2048 - m_square1.frequency) * 4.0);
        m_square1.phase += cycles / period;
        if (m_square1.phase >= 1.0) {
            m_square1.phase -= 1.0;
        }
    }
    if (m_square2.enabled) {
        const double period = std::max(4.0, static_cast<double>(2048 - m_square2.frequency) * 4.0);
        m_square2.phase += cycles / period;
        if (m_square2.phase >= 1.0) {
            m_square2.phase -= 1.0;
        }
    }
    if (m_wave.enabled) {
        // The wave channel advances one sample every (2048 - freq) * 2 CPU
        // cycles, i.e. one full 32-sample waveform every (2048 - freq) * 64
        // cycles. The phase is normalised over the whole waveform, so the
        // period must be the full waveform period too; using the per-sample
        // period made the channel play 32 times too high.
        const double period = std::max(64.0, static_cast<double>(2048 - m_wave.frequency) * 64.0);
        m_wave.phase += cycles / period;
        if (m_wave.phase >= 1.0) {
            m_wave.phase -= 1.0;
        }
    }

    // Refresh the cached channel levels once per tick, then emit however many
    // output samples are due. Decoupling the two means the mixer never sees a
    // half-updated channel state.
    updateChannelLevels();

    m_sampleAccumulator += static_cast<double>(cycles);
    while (m_sampleAccumulator >= kCyclesPerSample) {
        m_sampleAccumulator -= kCyclesPerSample;
        if (m_samples.size() < kMaxBufferedSamples) {
            m_samples.push_back(mixSample());
        }
    }
}

std::vector<std::int16_t> AudioUnit::takeSamples()
{
    std::vector<std::int16_t> samples = std::move(m_samples);
    m_samples.clear();
    return samples;
}

bool AudioUnit::isActive() const
{
    return m_square1.enabled || m_square2.enabled || m_wave.enabled || m_noise.enabled;
}

void AudioUnit::updateChannelLevels()
{
    // Square 1. The duty pattern is an 8-step waveform; step 0 is the first
    // step after the phase resets, matching the hardware shift order.
    if (m_square1.enabled) {
        const int step = static_cast<int>(m_square1.phase * 8.0) & 0x07;
        const bool high = ((kDutyWaveforms[m_square1.duty & 0x03] >> step) & 0x01) != 0;
        m_levelSquare1 = high ? (m_square1.volume / 15.0) : -(m_square1.volume / 15.0);
    } else {
        m_levelSquare1 = 0.0;
    }

    if (m_square2.enabled) {
        const int step = static_cast<int>(m_square2.phase * 8.0) & 0x07;
        const bool high = ((kDutyWaveforms[m_square2.duty & 0x03] >> step) & 0x01) != 0;
        m_levelSquare2 = high ? (m_square2.volume / 15.0) : -(m_square2.volume / 15.0);
    } else {
        m_levelSquare2 = 0.0;
    }

    // Wave channel: 32 4-bit samples, high nibble first. The nibble is
    // centred on zero so a silent waveform (all 0x88) produces no DC offset.
    if (m_wave.enabled) {
        const int index = static_cast<int>(m_wave.phase * 32.0) & 0x1F;
        const u8 byte = m_waveRam[index / 2];
        const int nibble = (index & 0x01) != 0 ? (byte & 0x0F) : (byte >> 4);
        // NR32 volume codes: 0 = mute, 1 = 100%, 2 = 50%, 3 = 25%.
        const int code = m_wave.volume & 0x03;
        const double gain = code == 0 ? 0.0 : 1.0 / (1 << (code - 1));
        m_levelWave = ((nibble - 7.5) / 7.5) * gain;
    } else {
        m_levelWave = 0.0;
    }

    if (m_noise.enabled) {
        m_levelNoise = m_noise.output ? (m_noise.volume / 15.0) : -(m_noise.volume / 15.0);
    } else {
        m_levelNoise = 0.0;
    }
}

std::int16_t AudioUnit::mixSample()
{
    // The master power switch (NR52 bit 7) silences every channel.
    if ((m_nr52 & 0x80) == 0) {
        return 0;
    }

    // NR51 routes each channel to the left and/or right output. The emulator
    // produces a single mono stream, so a channel is audible when it is
    // routed to at least one side.
    double total = 0.0;
    if ((m_nr51 & 0x11) != 0) {
        total += m_levelSquare1;
    }
    if ((m_nr51 & 0x22) != 0) {
        total += m_levelSquare2;
    }
    if ((m_nr51 & 0x44) != 0) {
        total += m_levelWave;
    }
    if ((m_nr51 & 0x88) != 0) {
        total += m_levelNoise;
    }

    // Each channel is already in the -1..+1 range, so a single channel at
    // full volume must reach full scale: dividing by the channel count here
    // made every game 12 dB too quiet. The headroom for several channels
    // playing at once is provided by the clamp below, which only engages
    // when the sum really exceeds full scale.
    const int leftVolume = (m_nr50 >> 4) & 0x07;
    const int rightVolume = m_nr50 & 0x07;
    const double volume = std::max(leftVolume, rightVolume) / 7.0;

    // A one-pole low pass at ~11 kHz removes the aliasing the square waves
    // fold back into the audible band at 48 kHz, which is what made the
    // output sound harsh rather than like a Game Boy.
    m_filterState += (total - m_filterState) * kOutputFilterCoefficient;

    const double sample = m_filterState * volume * 32767.0;
    const double clamped = std::max(-32768.0, std::min(32767.0, sample));
    return static_cast<std::int16_t>(clamped);
}

void AudioUnit::tickLengthCounters()
{
    if (m_lengthCounter < static_cast<std::uint32_t>(kCyclesPerLengthStep)) {
        return;
    }
    m_lengthCounter -= static_cast<std::uint32_t>(kCyclesPerLengthStep);

    // The length counter only runs when the channel is enabled *and* the
    // length-enable bit (NRx4 bit 6) is set; reaching zero stops the
    // channel. Games that play a note without a length (the common case for
    // music) leave the bit clear, and honouring the counter anyway cut every
    // note off after a few hundred milliseconds.
    if (m_square1.lengthEnabled && m_square1.length > 0) {
        --m_square1.length;
        if (m_square1.length == 0) {
            m_square1.enabled = false;
        }
    }
    if (m_square2.lengthEnabled && m_square2.length > 0) {
        --m_square2.length;
        if (m_square2.length == 0) {
            m_square2.enabled = false;
        }
    }
    if (m_wave.lengthEnabled && m_wave.length > 0) {
        --m_wave.length;
        if (m_wave.length == 0) {
            m_wave.enabled = false;
        }
    }
    if (m_noise.lengthEnabled && m_noise.length > 0) {
        --m_noise.length;
        if (m_noise.length == 0) {
            m_noise.enabled = false;
        }
    }
}

void AudioUnit::tickEnvelope(SquareChannel &channel, int cycles)
{
    // A period of 0 disables the envelope: the channel holds its volume.
    if (channel.envelopePeriod == 0 || !channel.enabled) {
        return;
    }
    channel.envelopeCycles += static_cast<std::uint32_t>(cycles);
    const auto stepCycles =
        static_cast<std::uint32_t>(channel.envelopePeriod * kCyclesPerEnvelopeStep);
    while (channel.envelopeCycles >= stepCycles) {
        channel.envelopeCycles -= stepCycles;
        const int next = channel.volume + channel.envelopeDirection;
        if (next < 0 || next > 15) {
            // The envelope stops at either bound; it never wraps.
            break;
        }
        channel.volume = next;
    }
}

void AudioUnit::tickNoiseEnvelope(int cycles)
{
    if (m_noise.envelopePeriod == 0 || !m_noise.enabled) {
        return;
    }
    m_noise.envelopeCycles += static_cast<std::uint32_t>(cycles);
    const auto stepCycles =
        static_cast<std::uint32_t>(m_noise.envelopePeriod * kCyclesPerEnvelopeStep);
    while (m_noise.envelopeCycles >= stepCycles) {
        m_noise.envelopeCycles -= stepCycles;
        const int next = m_noise.volume + m_noise.envelopeDirection;
        if (next < 0 || next > 15) {
            break;
        }
        m_noise.volume = next;
    }
}

void AudioUnit::tickSweep(int cycles)
{
    // Only channel 1 has a frequency sweep unit; a period of 0 disables it.
    if (m_square1.sweepPeriod == 0 || !m_square1.enabled) {
        return;
    }
    m_square1.sweepCycles += static_cast<std::uint32_t>(cycles);
    const auto stepCycles =
        static_cast<std::uint32_t>(m_square1.sweepPeriod * kCyclesPerSweepStep);
    while (m_square1.sweepCycles >= stepCycles) {
        m_square1.sweepCycles -= stepCycles;
        if (m_square1.sweepShift == 0) {
            continue;
        }
        const int delta = m_square1.frequency >> m_square1.sweepShift;
        const int next = m_square1.frequency + m_square1.sweepDirection * delta;
        if (next < 0 || next > 0x7FF) {
            // Overflowing the 11-bit frequency silences the channel.
            m_square1.enabled = false;
            break;
        }
        m_square1.frequency = next;
    }
}

void AudioUnit::tickNoise(int cycles)
{
    if (!m_noise.enabled) {
        return;
    }
    // Clock period = divisor[code] << shift CPU cycles.
    const int divisor = kNoiseDivisors[m_noise.divisorCode & 0x07];
    const auto period = static_cast<std::uint32_t>(divisor << (m_noise.shift & 0x0F));
    if (period == 0) {
        return;
    }
    m_noise.clockCycles += static_cast<std::uint32_t>(cycles);
    while (m_noise.clockCycles >= period) {
        m_noise.clockCycles -= period;
        // 15-bit LFSR: XOR bits 0 and 1, shift right, feed the result back
        // into bit 14. In 7-bit mode bit 6 is also overwritten, which gives
        // the metallic noise the hardware produces.
        const std::uint16_t bit = static_cast<std::uint16_t>(
            (m_noise.lfsr ^ (m_noise.lfsr >> 1)) & 0x01);
        m_noise.lfsr = static_cast<std::uint16_t>((m_noise.lfsr >> 1) | (bit << 14));
        if (m_noise.widthMode != 0) {
            m_noise.lfsr = static_cast<std::uint16_t>(
                (m_noise.lfsr & ~0x40u) | (bit << 6));
        }
        // The channel outputs the inverted LSB of the LFSR.
        m_noise.output = (m_noise.lfsr & 0x01) == 0;
    }
}

Emulation::Emulation()
{
    m_cpu.setMemory(&m_memory);
    m_cpu.setInterrupts(&m_interrupts);
    m_gpu.setMemory(&m_memory);
    m_gpu.setInterrupts(&m_interrupts);
    m_gpu.setVideoRam(m_memory.videoRamBuffer());
    m_memory.setGPU(&m_gpu);
    m_memory.setInterrupts(&m_interrupts);
    m_memory.setAudio(&m_audio);
}

Emulation::~Emulation() = default;

bool Emulation::loadRom(const std::vector<u8> &data, std::string &error)
{
    m_running = false;
    return m_memory.loadCartridge(data, error);
}

bool Emulation::loadRomFile(const std::string &path, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot open file: " + path;
        return false;
    }
    std::vector<u8> data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadRom(data, error);
}

void Emulation::unloadRom()
{
    m_memory.unloadCartridge();
    m_running = false;
}

void Emulation::reset()
{
    m_memory.setBootRomEnabled(m_bootRomEnabled);
    m_memory.reset();
    m_cpu.reset();
    m_gpu.reset();
    m_interrupts.reset();
    m_audio.reset();
    m_running = true;
    m_cycles = 0;
}

int Emulation::step()
{
    const int cycles = m_cpu.step();
    advancePeripherals(cycles);
    return cycles;
}

void Emulation::runFrame()
{
    // Run exactly one full frame: kDotsPerLine * kLinesPerFrame = 70224 CPU
    // cycles, i.e. all 154 scanlines including VBlank.
    //
    // The previous version stopped as soon as the GPU reported a *new* frame,
    // which happens at the start of VBlank (LY == 144), so only
    // 144 * 456 = 65664 cycles were executed per call. The worker then paced
    // that short frame at 59.7275 Hz, so the emulator ran at 93.5 % of the
    // real speed. Video merely looked slightly slow, but audio is generated
    // from the same cycle counter: 44883 samples/s were produced while the
    // sound card consumed 48000/s, so the output buffer underran about 3100
    // times per second. That constant starvation is what made the sound
    // crackle and sound distorted.
    //
    // Counting cycles instead of watching the frame counter keeps the
    // emulated clock locked to the host clock, which is what audio needs.
    int remaining = kCyclesPerFrame;
    while (remaining > 0) {
        remaining -= step();
    }
}

void Emulation::runCycles(int cycles)
{
    for (int i = 0; i < cycles; ++i) {
        (void)step();
    }
}

bool Emulation::isRomLoaded() const
{
    return m_memory.hasCartridge();
}

bool Emulation::isRunning() const
{
    return m_running;
}

const u8 *Emulation::frameBuffer() const
{
    return m_gpu.frameBuffer();
}

std::uint64_t Emulation::frameCount() const
{
    return m_gpu.frameCount();
}

Opcode &Emulation::cpu()
{
    return m_cpu;
}

const Opcode &Emulation::cpu() const
{
    return m_cpu;
}

Memory &Emulation::memory()
{
    return m_memory;
}

const Memory &Emulation::memory() const
{
    return m_memory;
}

GPU &Emulation::gpu()
{
    return m_gpu;
}

const GPU &Emulation::gpu() const
{
    return m_gpu;
}

void Emulation::setDisplayPalette(DisplayPalette palette)
{
    m_gpu.setDisplayPalette(palette);
}

DisplayPalette Emulation::displayPalette() const
{
    return m_gpu.displayPalette();
}

void Emulation::setCustomPaletteColors(const std::array<u32, 4> &colors)
{
    m_gpu.setCustomPaletteColors(colors);
}

std::array<u32, 4> Emulation::activePaletteColors() const
{
    return m_gpu.activePaletteColors();
}

InterruptController &Emulation::interrupts()
{
    return m_interrupts;
}

const InterruptController &Emulation::interrupts() const
{
    return m_interrupts;
}

AudioUnit &Emulation::audio()
{
    return m_audio;
}

const AudioUnit &Emulation::audio() const
{
    return m_audio;
}

const Cartridge &Emulation::cartridge() const
{
    return m_memory.cartridge();
}

const std::vector<u8> &Emulation::saveData() const
{
    return m_memory.saveData();
}

void Emulation::loadSaveData(const std::vector<u8> &data)
{
    m_memory.loadSaveData(data);
}

std::vector<u8> Emulation::saveState() const
{
    std::vector<u8> out;

    // Header: magic and format version.
    appendBytes(out, reinterpret_cast<const u8 *>(kStateMagic), sizeof(kStateMagic));
    appendDWord(out, kStateVersion);

    // CPU registers and control flags.
    const Opcode::CpuState cpu = m_cpu.cpuState();
    appendByte(out, cpu.regs.a);
    appendByte(out, cpu.regs.f);
    appendByte(out, cpu.regs.b);
    appendByte(out, cpu.regs.c);
    appendByte(out, cpu.regs.d);
    appendByte(out, cpu.regs.e);
    appendByte(out, cpu.regs.h);
    appendByte(out, cpu.regs.l);
    appendWord(out, cpu.regs.sp);
    appendWord(out, cpu.regs.pc);
    appendByte(out, cpu.ime ? 1 : 0);
    appendByte(out, cpu.imeScheduled ? 1 : 0);
    appendByte(out, cpu.halted ? 1 : 0);
    appendByte(out, cpu.stopped ? 1 : 0);
    appendQWord(out, cpu.instructions);
    appendWord(out, cpu.lastAddress);
    appendByte(out, cpu.lastOpcode);

    // Interrupt controller: IF and IE registers.
    appendByte(out, m_interrupts.flags());
    appendByte(out, m_interrupts.enableRegister());

    // Memory: RAM arrays, registers, mapper state and cartridge RAM.
    const Memory::MemoryState mem = m_memory.memoryState();
    appendBytes(out, mem.wram.data(), mem.wram.size());
    appendBytes(out, mem.vram.data(), mem.vram.size());
    appendBytes(out, mem.oam.data(), mem.oam.size());
    appendBytes(out, mem.hram.data(), mem.hram.size());
    appendBytes(out, mem.io.data(), mem.io.size());
    appendWord(out, mem.timer.divider);
    appendByte(out, mem.timer.tima);
    appendByte(out, mem.timer.tma);
    appendByte(out, mem.timer.tac);
    appendByte(out, mem.timer.overflow ? 1 : 0);
    appendByte(out, mem.serial.sb);
    appendByte(out, mem.serial.sc);
    appendDWord(out, static_cast<std::uint32_t>(mem.serial.bitTimer));
    appendByte(out, mem.serial.outgoing);
    appendByte(out, mem.serial.incoming);
    appendByte(out, mem.joypadSelect);
    appendByte(out, mem.joypadButtons);
    appendByte(out, mem.romBank);
    appendByte(out, mem.ramBank);
    appendByte(out, mem.ramEnabled ? 1 : 0);
    appendByte(out, mem.bankingMode ? 1 : 0);
    appendByte(out, mem.bootRomEnabled ? 1 : 0);
    appendByte(out, mem.ie);
    appendDWord(out, static_cast<std::uint32_t>(mem.cartRam.size()));
    appendBytes(out, mem.cartRam.data(), mem.cartRam.size());

    // GPU: LCD registers, timing counters, palettes and framebuffer.
    const GPU::GpuState gpu = m_gpu.gpuState();
    appendByte(out, gpu.lcdc);
    appendByte(out, gpu.stat);
    appendByte(out, gpu.scy);
    appendByte(out, gpu.scx);
    appendByte(out, gpu.ly);
    appendByte(out, gpu.lyc);
    appendByte(out, gpu.bgp);
    appendByte(out, gpu.obp0);
    appendByte(out, gpu.obp1);
    appendByte(out, gpu.wy);
    appendByte(out, gpu.wx);
    appendByte(out, gpu.dma);
    appendByte(out, gpu.bgpi);
    appendByte(out, gpu.obpi);
    appendDWord(out, static_cast<std::uint32_t>(gpu.dot));
    appendDWord(out, static_cast<std::uint32_t>(gpu.mode));
    appendDWord(out, static_cast<std::uint32_t>(gpu.windowLine));
    appendDWord(out, static_cast<std::uint32_t>(gpu.spritesOnLine));
    appendQWord(out, gpu.frameCount);
    appendQWord(out, gpu.disabledFrames);
    appendByte(out, gpu.statLine ? 1 : 0);
    appendBytes(out, gpu.bgPaletteRam.data(), gpu.bgPaletteRam.size());
    appendBytes(out, gpu.objPaletteRam.data(), gpu.objPaletteRam.size());
    appendBytes(out, gpu.frameBuffer.data(), gpu.frameBuffer.size());

    // Emulator facade: counters and power flags.
    appendQWord(out, m_cycles);
    appendByte(out, m_running ? 1 : 0);
    appendByte(out, m_bootRomEnabled ? 1 : 0);

    return out;
}

bool Emulation::loadState(const std::vector<u8> &data, std::string &error)
{
    constexpr const char *kCorrupt = "Corrupted or truncated state file";
    StateReader reader(data.data(), data.size());

    // Header: magic and format version.
    u8 magic[4] = {};
    if (!reader.readBytes(magic, sizeof(magic))
        || std::memcmp(magic, kStateMagic, sizeof(magic)) != 0) {
        error = "Not a Game Boy state file";
        return false;
    }
    std::uint32_t version = 0;
    if (!reader.readDWord(version) || version != kStateVersion) {
        error = "Unsupported state file version";
        return false;
    }

    // CPU registers and control flags.
    Opcode::CpuState cpu;
    if (!reader.readByte(cpu.regs.a) || !reader.readByte(cpu.regs.f)
        || !reader.readByte(cpu.regs.b) || !reader.readByte(cpu.regs.c)
        || !reader.readByte(cpu.regs.d) || !reader.readByte(cpu.regs.e)
        || !reader.readByte(cpu.regs.h) || !reader.readByte(cpu.regs.l)
        || !reader.readWord(cpu.regs.sp) || !reader.readWord(cpu.regs.pc)) {
        error = kCorrupt;
        return false;
    }
    u8 byte = 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    cpu.ime = byte != 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    cpu.imeScheduled = byte != 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    cpu.halted = byte != 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    cpu.stopped = byte != 0;
    if (!reader.readQWord(cpu.instructions) || !reader.readWord(cpu.lastAddress)
        || !reader.readByte(cpu.lastOpcode)) {
        error = kCorrupt;
        return false;
    }

    // Interrupt controller: IF and IE registers.
    u8 interruptFlags = 0;
    u8 interruptEnable = 0;
    if (!reader.readByte(interruptFlags) || !reader.readByte(interruptEnable)) {
        error = kCorrupt;
        return false;
    }

    // Memory: RAM arrays, registers, mapper state and cartridge RAM.
    Memory::MemoryState mem;
    if (!reader.readBytes(mem.wram.data(), mem.wram.size())
        || !reader.readBytes(mem.vram.data(), mem.vram.size())
        || !reader.readBytes(mem.oam.data(), mem.oam.size())
        || !reader.readBytes(mem.hram.data(), mem.hram.size())
        || !reader.readBytes(mem.io.data(), mem.io.size())
        || !reader.readWord(mem.timer.divider)
        || !reader.readByte(mem.timer.tima) || !reader.readByte(mem.timer.tma)
        || !reader.readByte(mem.timer.tac)) {
        error = kCorrupt;
        return false;
    }
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    mem.timer.overflow = byte != 0;
    if (!reader.readByte(mem.serial.sb) || !reader.readByte(mem.serial.sc)) {
        error = kCorrupt;
        return false;
    }
    std::uint32_t dword = 0;
    if (!reader.readDWord(dword)) {
        error = kCorrupt;
        return false;
    }
    mem.serial.bitTimer = static_cast<int>(dword);
    if (!reader.readByte(mem.serial.outgoing) || !reader.readByte(mem.serial.incoming)
        || !reader.readByte(mem.joypadSelect) || !reader.readByte(mem.joypadButtons)
        || !reader.readByte(mem.romBank) || !reader.readByte(mem.ramBank)) {
        error = kCorrupt;
        return false;
    }
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    mem.ramEnabled = byte != 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    mem.bankingMode = byte != 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    mem.bootRomEnabled = byte != 0;
    if (!reader.readByte(mem.ie)) {
        error = kCorrupt;
        return false;
    }

    // The cartridge RAM length also tells whether the state belongs to the
    // cartridge that is currently loaded; the check runs before any state is
    // applied so a mismatched file cannot corrupt the running machine.
    std::uint32_t cartRamLen = 0;
    if (!reader.readDWord(cartRamLen)) {
        error = kCorrupt;
        return false;
    }
    if (static_cast<std::size_t>(cartRamLen) != m_memory.cartridge().ram.size()) {
        error = "The state was saved with a different cartridge";
        return false;
    }
    mem.cartRam.resize(cartRamLen);
    if (!reader.readBytes(mem.cartRam.data(), mem.cartRam.size())) {
        error = kCorrupt;
        return false;
    }

    // GPU: LCD registers, timing counters, palettes and framebuffer.
    GPU::GpuState gpu;
    if (!reader.readByte(gpu.lcdc) || !reader.readByte(gpu.stat)
        || !reader.readByte(gpu.scy) || !reader.readByte(gpu.scx)
        || !reader.readByte(gpu.ly) || !reader.readByte(gpu.lyc)
        || !reader.readByte(gpu.bgp) || !reader.readByte(gpu.obp0)
        || !reader.readByte(gpu.obp1) || !reader.readByte(gpu.wy)
        || !reader.readByte(gpu.wx) || !reader.readByte(gpu.dma)
        || !reader.readByte(gpu.bgpi) || !reader.readByte(gpu.obpi)) {
        error = kCorrupt;
        return false;
    }
    if (!reader.readDWord(dword)) {
        error = kCorrupt;
        return false;
    }
    gpu.dot = static_cast<int>(dword);
    if (!reader.readDWord(dword)) {
        error = kCorrupt;
        return false;
    }
    gpu.mode = static_cast<int>(dword);
    if (!reader.readDWord(dword)) {
        error = kCorrupt;
        return false;
    }
    gpu.windowLine = static_cast<int>(dword);
    if (!reader.readDWord(dword)) {
        error = kCorrupt;
        return false;
    }
    gpu.spritesOnLine = static_cast<int>(dword);
    if (!reader.readQWord(gpu.frameCount) || !reader.readQWord(gpu.disabledFrames)
        || !reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    gpu.statLine = byte != 0;
    gpu.frameBuffer.resize(kFrameBufferBytes);
    if (!reader.readBytes(gpu.bgPaletteRam.data(), gpu.bgPaletteRam.size())
        || !reader.readBytes(gpu.objPaletteRam.data(), gpu.objPaletteRam.size())
        || !reader.readBytes(gpu.frameBuffer.data(), gpu.frameBuffer.size())) {
        error = kCorrupt;
        return false;
    }

    // Emulator facade: counters and power flags.
    std::uint64_t cycles = 0;
    if (!reader.readQWord(cycles)) {
        error = kCorrupt;
        return false;
    }
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    const bool running = byte != 0;
    if (!reader.readByte(byte)) {
        error = kCorrupt;
        return false;
    }
    const bool bootRomEnabled = byte != 0;

    // Everything validated: apply the state.
    m_cpu.restoreCpuState(cpu);
    m_interrupts.setFlags(interruptFlags);
    m_interrupts.setEnableRegister(interruptEnable);
    m_memory.restoreMemoryState(mem);
    m_gpu.restoreGpuState(gpu);
    m_cycles = cycles;
    m_running = running;
    m_bootRomEnabled = bootRomEnabled;
    return true;
}

void Emulation::setButton(int index, bool pressed)
{
    m_memory.setButton(index, pressed);
}

const std::string &Emulation::serialOutput() const
{
    return m_memory.serialOutput();
}

void Emulation::clearSerialOutput()
{
    m_memory.clearSerialOutput();
}

void Emulation::setBootRomEnabled(bool enabled)
{
    m_bootRomEnabled = enabled;
}

bool Emulation::isBootRomEnabled() const
{
    return m_bootRomEnabled;
}

std::uint64_t Emulation::cycleCount() const
{
    return m_cycles;
}

std::vector<std::int16_t> Emulation::takeAudioSamples()
{
    return m_audio.takeSamples();
}

const char *Emulation::mapperName(MapperType mapper)
{
    switch (mapper) {
    case MapperType::None:
        return "None";
    case MapperType::MBC1:
        return "MBC1";
    case MapperType::MBC2:
        return "MBC2";
    case MapperType::MBC3:
        return "MBC3";
    case MapperType::MBC5:
        return "MBC5";
    case MapperType::Unknown:
    default:
        return "Unknown";
    }
}

void Emulation::advancePeripherals(int cycles)
{
    m_cycles += static_cast<std::uint64_t>(cycles);
    m_memory.tick(cycles);
    m_gpu.tick(cycles);
    m_audio.tick(cycles);
}

} // namespace gb
