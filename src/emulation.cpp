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
#include <fstream>
#include <iterator>

namespace gb {

namespace {

/** @brief Number of cycles needed to produce one audio sample. */
constexpr double kCyclesPerSample = static_cast<double>(kCyclesPerSecond) / kSampleRate;

/** @brief Number of samples kept when the front-end stops draining the buffer. */
constexpr std::size_t kMaxBufferedSamples = 48000;

/** @brief Duty cycle patterns: number of high quarters per period. */
constexpr int kDutyPatterns[4] = {1, 2, 4, 6};

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
    case 0xFF11:
        m_square1.duty = (value >> 6) & 0x03;
        m_square1.length = 64 - (value & 0x3F);
        break;
    case 0xFF12:
        m_square1.volume = (value >> 4) & 0x0F;
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
        }
        break;
    case 0xFF16:
        m_square2.duty = (value >> 6) & 0x03;
        m_square2.length = 64 - (value & 0x3F);
        break;
    case 0xFF17:
        m_square2.volume = (value >> 4) & 0x0F;
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
    double total = 0.0;

    if (m_square1.enabled && (m_nr51 & 0x11) != 0) {
        const int highQuarters = kDutyPatterns[m_square1.duty & 0x03];
        const double amplitude = (m_square1.volume / 15.0) * 0.5;
        total += (m_square1.phase * 4.0 < highQuarters) ? amplitude : -amplitude;
    }
    if (m_square2.enabled && (m_nr51 & 0x22) != 0) {
        const int highQuarters = kDutyPatterns[m_square2.duty & 0x03];
        const double amplitude = (m_square2.volume / 15.0) * 0.5;
        total += (m_square2.phase * 4.0 < highQuarters) ? amplitude : -amplitude;
    }
    if (m_wave.enabled && (m_nr51 & 0x44) != 0) {
        const int index = static_cast<int>(m_wave.phase * 32.0) & 0x1F;
        const u8 byte = m_waveRam[index / 2];
        const int nibble = (index & 0x01) != 0 ? (byte & 0x0F) : (byte >> 4);
        const double amplitude = nibble / 8.0 - 1.0;
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

Emulation::Emulation()
{
    m_cpu.setMemory(&m_memory);
    m_cpu.setInterrupts(&m_interrupts);
    m_gpu.setMemory(&m_memory);
    m_gpu.setInterrupts(&m_interrupts);
    m_gpu.setVideoRam(m_memory.videoRamBuffer());
    m_memory.setGPU(&m_gpu);
    m_memory.setInterrupts(&m_interrupts);
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
