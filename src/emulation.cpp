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

/** @brief Duty cycle patterns: number of high eighths per period (12.5/25/50/75%). */
constexpr int kDutyPatterns[4] = {1, 2, 4, 6};

/** @brief CPU cycles between two 64 Hz volume envelope steps. */
constexpr double kCyclesPerEnvelopeStep = static_cast<double>(kCyclesPerSecond) / 64.0;

/** @brief CPU cycles between two 128 Hz frequency sweep steps. */
constexpr double kCyclesPerSweepStep = static_cast<double>(kCyclesPerSecond) / 128.0;

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
    m_square1.duty = 0;
    m_square1.volume = 0;
    m_square2.duty = 0;
    m_square2.volume = 0;
    m_wave.volume = 0;
    m_nr50 = 0x77;
    m_nr51 = 0xF3;
    m_nr52 = 0xF1;
    m_waveRam.fill(0);
    m_sampleAccumulator = 0.0;
    m_samples.clear();
    m_lengthCounter = 0;
}

u8 AudioUnit::readRegister(u16 address) const
{
    switch (address) {
    case 0xFF24:
        return m_nr50;
    case 0xFF25:
        return m_nr51;
    case 0xFF26:
        return m_nr52;
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
        if ((value & 0x80) != 0) {
            m_square2.enabled = true;
            m_square2.phase = 0.0;
            m_square2.volume = m_square2.initialVolume;
            m_square2.envelopeCycles = 0;
        }
        break;
    case 0xFF1A:
        m_wave.enabled = (value & 0x80) != 0;
        break;
    case 0xFF1B:
        break; // Wave length counter is not used by this simplified channel.
    case 0xFF1C:
        m_wave.volume = (value >> 5) & 0x03;
        break;
    case 0xFF1D:
        m_wave.frequency = (m_wave.frequency & 0x700) | value;
        break;
    case 0xFF1E:
        m_wave.frequency = (m_wave.frequency & 0xFF) | ((value & 0x07) << 8);
        if ((value & 0x80) != 0) {
            m_wave.enabled = true;
            m_wave.phase = 0.0;
        }
        break;
    case 0xFF24:
        m_nr50 = value;
        break;
    case 0xFF25:
        m_nr51 = value;
        break;
    case 0xFF26:
        m_nr52 = static_cast<u8>((value & 0x80) | (m_nr52 & 0x7F));
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
    // Advance the length counters at 256 Hz.
    m_lengthCounter += static_cast<std::uint32_t>(cycles);
    tickLengthCounters();

    // Advance the volume envelopes at 64 Hz and channel 1's frequency sweep
    // at 128 Hz: without these, every note holds its initial volume and
    // pitch forever instead of the decay/slide most game music relies on,
    // which is the main reason the mix sounded flat and "wrong".
    tickEnvelope(m_square1, cycles);
    tickEnvelope(m_square2, cycles);
    tickSweep(cycles);

    // Advance the phase accumulators of the active channels.
    if (m_square1.enabled) {
        const double period = std::max(4.0, static_cast<double>(2048 - m_square1.frequency) * 4.0);
        m_square1.phase = std::fmod(m_square1.phase + cycles / period, 1.0);
    }
    if (m_square2.enabled) {
        const double period = std::max(4.0, static_cast<double>(2048 - m_square2.frequency) * 4.0);
        m_square2.phase = std::fmod(m_square2.phase + cycles / period, 1.0);
    }
    if (m_wave.enabled) {
        const double period = std::max(2.0, static_cast<double>(2048 - m_wave.frequency) * 2.0);
        m_wave.phase = std::fmod(m_wave.phase + cycles / period, 1.0);
    }

    // Accumulate cycles until one sample can be produced.
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
    return m_square1.enabled || m_square2.enabled || m_wave.enabled;
}

std::int16_t AudioUnit::mixSample() const
{
    // The master power switch (NR52 bit 7) silences every channel on real
    // hardware; without this check the mix kept playing whatever the
    // channels were doing even after a game turned sound off.
    if ((m_nr52 & 0x80) == 0) {
        return 0;
    }

    double total = 0.0;

    if (m_square1.enabled && (m_nr51 & 0x11) != 0) {
        // Patterns are expressed as eighths of the period (12.5/25/50/75%),
        // so the phase must be scaled by 8, not 4: the previous factor of 4
        // squeezed the whole waveform into the first half of the period,
        // turning duty 2 and 3 into a constant (silent) line and distorting
        // duty 0 and 1 into the wrong cycle - the main cause of the bad
        // sound.
        const int highEighths = kDutyPatterns[m_square1.duty & 0x03];
        const double amplitude = (m_square1.volume / 15.0) * 0.5;
        total += (m_square1.phase * 8.0 < highEighths) ? amplitude : -amplitude;
    }
    if (m_square2.enabled && (m_nr51 & 0x22) != 0) {
        const int highEighths = kDutyPatterns[m_square2.duty & 0x03];
        const double amplitude = (m_square2.volume / 15.0) * 0.5;
        total += (m_square2.phase * 8.0 < highEighths) ? amplitude : -amplitude;
    }
    if (m_wave.enabled && (m_nr51 & 0x44) != 0) {
        const int index = static_cast<int>(m_wave.phase * 32.0) & 0x1F;
        const u8 byte = m_waveRam[index / 2];
        const int nibble = (index & 0x01) != 0 ? (byte & 0x0F) : (byte >> 4);
        // Center the 4-bit sample around zero (range -1..1) instead of the
        // previous -1..0.875, which added a constant DC offset to the mix.
        const double amplitude = (nibble - 7.5) / 7.5;
        const double gain = m_wave.volume == 0 ? 0.0 : 1.0 / (1 << (4 - m_wave.volume));
        total += amplitude * 0.5 * gain;
    }

    total /= 3.0;
    const double volume = (((m_nr50 >> 4) & 0x0F) + (m_nr50 & 0x0F)) / 2.0 / 15.0;
    const double sample = total * volume * 32767.0;
    const double clamped = std::max(-32768.0, std::min(32767.0, sample));
    return static_cast<std::int16_t>(clamped);
}

void AudioUnit::tickLengthCounters()
{
    if (m_lengthCounter < static_cast<std::uint32_t>(kCyclesPerSecond) / 256) {
        return;
    }
    m_lengthCounter = 0;
    if (m_square1.length > 0) {
        --m_square1.length;
        if (m_square1.length == 0) {
            m_square1.enabled = false;
        }
    }
    if (m_square2.length > 0) {
        --m_square2.length;
        if (m_square2.length == 0) {
            m_square2.enabled = false;
        }
    }
}

void AudioUnit::tickEnvelope(SquareChannel &channel, int cycles)
{
    // A period of 0 disables the envelope: the channel just holds its
    // current volume, matching real hardware.
    if (channel.envelopePeriod == 0) {
        return;
    }
    channel.envelopeCycles += static_cast<std::uint32_t>(cycles);
    const auto stepCycles = static_cast<std::uint32_t>(channel.envelopePeriod * kCyclesPerEnvelopeStep);
    while (channel.envelopeCycles >= stepCycles) {
        channel.envelopeCycles -= stepCycles;
        const int next = channel.volume + channel.envelopeDirection;
        if (next < 0 || next > 15) {
            // The envelope stops once it hits either bound; it never wraps.
            break;
        }
        channel.volume = next;
    }
}

void AudioUnit::tickSweep(int cycles)
{
    // Only channel 1 has a frequency sweep unit; a period of 0 disables it.
    if (m_square1.sweepPeriod == 0 || !m_square1.enabled) {
        return;
    }
    m_square1.sweepCycles += static_cast<std::uint32_t>(cycles);
    const auto stepCycles = static_cast<std::uint32_t>(m_square1.sweepPeriod * kCyclesPerSweepStep);
    while (m_square1.sweepCycles >= stepCycles) {
        m_square1.sweepCycles -= stepCycles;
        if (m_square1.sweepShift == 0) {
            continue;
        }
        const int delta = m_square1.frequency >> m_square1.sweepShift;
        const int next = m_square1.frequency + m_square1.sweepDirection * delta;
        if (next < 0 || next > 0x7FF) {
            // Overflowing (or underflowing) the 11-bit frequency silences
            // the channel, exactly like the real sweep unit.
            m_square1.enabled = false;
            break;
        }
        m_square1.frequency = next;
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
    const std::uint64_t frameStart = m_gpu.frameCount();
    const std::uint64_t disabledStart = m_gpu.disabledFrames();
    int guard = 0;
    while (m_gpu.frameCount() == frameStart && m_gpu.disabledFrames() == disabledStart
           && guard < kCyclesPerFrame * 2) {
        (void)step();
        ++guard;
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
