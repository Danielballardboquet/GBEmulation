/**
 * @file Emulation.h
 * @brief Top level facade of the emulation core.
 *
 * Emulation owns every component (memory bus, CPU, GPU, interrupt controller
 * and audio unit), wires them together, drives the frame loop and exposes the
 * state needed by a front-end: framebuffer, audio samples, cartridge metadata
 * and the save file.
 */

#ifndef GB_EMULATION_H
#define GB_EMULATION_H

#include "emulation/GPU.h"
#include "emulation/Interrupt.h"
#include "emulation/Memory.h"
#include "emulation/Opcode.h"
#include "emulation/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gb {

/**
 * @brief Simple square wave audio unit driven by the four sound channels.
 *
 * The unit is intentionally small: it mixes the two square channels and the
 * wave channel using the NR50 volume and NR51 panning registers so games that
 * rely on audio feedback keep running, while keeping the CPU cost negligible.
 */
class AudioUnit {
public:
    /**
     * @brief Constructs a silent audio unit.
     */
    AudioUnit();

    /**
     * @brief Resets every channel and register.
     */
    void reset();

    /**
     * @brief Reads one of the sound registers.
     * @param address Address inside 0xFF10-0xFF3F.
     * @return The register value.
     */
    u8 readRegister(u16 address) const;

    /**
     * @brief Writes one of the sound registers.
     * @param address Address inside 0xFF10-0xFF3F.
     * @param value The value to write.
     */
    void writeRegister(u16 address, u8 value);

    /**
     * @brief Advances the channels by a number of CPU cycles.
     * @param cycles Number of cycles elapsed.
     */
    void tick(int cycles);

    /**
     * @brief Renders the samples produced since the last call.
     * @return Mono 16-bit signed samples at kSampleRate.
     */
    std::vector<std::int16_t> takeSamples();

    /**
     * @brief Tells whether at least one channel is producing sound.
     * @return True when the output is not silent.
     */
    bool isActive() const;

private:
    /**
     * @brief State of a single square wave channel.
     */
    struct SquareChannel {
        bool enabled = false;   /**< Channel running flag. */
        int frequency = 0;      /**< Frequency register value (11 bits). */
        int duty = 2;           /**< Duty cycle index (0..3). */
        int volume = 0;         /**< Current (post-envelope) volume (0..15). */
        int initialVolume = 0;  /**< Volume reloaded into the channel on trigger. */
        int envelopeDirection = -1; /**< +1 to increase the volume, -1 to decrease it. */
        int envelopePeriod = 0;     /**< Envelope step length in 64 Hz ticks (0 = disabled). */
        std::uint32_t envelopeCycles = 0; /**< Cycles accumulated towards the next envelope step. */
        int length = 0;         /**< Remaining length in 256 Hz ticks. */
        bool lengthEnabled = false; /**< NRx4 bit 6: the length counter is running. */
        double phase = 0.0;     /**< Phase accumulator (0..1). */
        int sweepPeriod = 0;        /**< Frequency sweep period in 128 Hz ticks (channel 1 only). */
        int sweepDirection = 1;     /**< +1 increases the frequency, -1 decreases it. */
        int sweepShift = 0;         /**< Frequency sweep shift amount (channel 1 only). */
        std::uint32_t sweepCycles = 0; /**< Cycles accumulated towards the next sweep step. */
    };

    /**
     * @brief State of the wave channel.
     */
    struct WaveChannel {
        bool enabled = false; /**< Channel running flag. */
        int frequency = 0;    /**< Frequency register value (11 bits). */
        int volume = 0;       /**< Volume code (0..3). */
        int length = 0;       /**< Remaining length in 256 Hz ticks. */
        bool lengthEnabled = false; /**< NR34 bit 6: the length counter is running. */
        double phase = 0.0;   /**< Phase accumulator (0..1). */
    };

    /**
     * @brief State of the noise channel (channel 4).
     */
    struct NoiseChannel {
        bool enabled = false;   /**< Channel running flag. */
        int volume = 0;         /**< Current (post-envelope) volume (0..15). */
        int initialVolume = 0;  /**< Volume reloaded into the channel on trigger. */
        int envelopeDirection = -1; /**< +1 to increase the volume, -1 to decrease it. */
        int envelopePeriod = 0;     /**< Envelope step length in 64 Hz ticks (0 = disabled). */
        std::uint32_t envelopeCycles = 0; /**< Cycles accumulated towards the next envelope step. */
        int length = 0;         /**< Remaining length in 256 Hz ticks. */
        bool lengthEnabled = false; /**< NR44 bit 6: the length counter is running. */
        int divisorCode = 0;    /**< Clock divisor selector (0..7). */
        int widthMode = 0;      /**< 0 = 15-bit LFSR, 1 = 7-bit LFSR. */
        int shift = 0;          /**< Clock shift (0..15). */
        std::uint32_t clockCycles = 0; /**< Cycles accumulated towards the next LFSR clock. */
        std::uint16_t lfsr = 0x7FFF;   /**< Linear feedback shift register. */
        bool output = false;    /**< Current LFSR output bit (true = high). */
    };

    /**
     * @brief Mixes the active channels into one sample.
     *
     * Updates the output filter state, so it is not const.
     * @return A signed 16-bit sample.
     */
    std::int16_t mixSample();

    /**
     * @brief Advances the length counters at 256 Hz.
     */
    void tickLengthCounters();

    /**
     * @brief Advances a channel's volume envelope at 64 Hz.
     * @param channel The square channel to update.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tickEnvelope(SquareChannel &channel, int cycles);

    /**
     * @brief Advances the noise channel's volume envelope at 64 Hz.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tickNoiseEnvelope(int cycles);

    /**
     * @brief Advances channel 1's frequency sweep at 128 Hz.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tickSweep(int cycles);

    /**
     * @brief Clocks the noise channel's LFSR at its programmed rate.
     * @param cycles Number of CPU cycles elapsed.
     */
    void tickNoise(int cycles);

    /**
     * @brief Recomputes the cached per-channel output levels.
     *
     * The mixer reads these instead of the raw registers so that a channel
     * that is disabled or routed to neither output contributes exactly zero.
     */
    void updateChannelLevels();

    SquareChannel m_square1; /**< Channel 1 (NR10-NR14). */
    SquareChannel m_square2; /**< Channel 2 (NR21-NR24). */
    WaveChannel m_wave;      /**< Channel 3 (NR30-NR34). */
    NoiseChannel m_noise;    /**< Channel 4 (NR41-NR44). */
    u8 m_nr50 = 0x77;        /**< Master volume (0xFF24). */
    u8 m_nr51 = 0xF3;        /**< Channel routing (0xFF25). */
    u8 m_nr52 = 0xF1;        /**< Channel status (0xFF26). */
    std::array<u8, 16> m_waveRam{}; /**< Wave pattern RAM (0xFF30-0xFF3F). */
    double m_sampleAccumulator = 0.0; /**< Cycles accumulated for the next sample. */
    std::vector<std::int16_t> m_samples; /**< Samples waiting to be played. */
    std::uint32_t m_lengthCounter = 0;   /**< Divider of the 256 Hz length clock. */
    // Cached output levels, in the -1..+1 range, refreshed every tick.
    double m_levelSquare1 = 0.0; /**< Channel 1 output. */
    double m_levelSquare2 = 0.0; /**< Channel 2 output. */
    double m_levelWave = 0.0;    /**< Channel 3 output. */
    double m_levelNoise = 0.0;   /**< Channel 4 output. */
    double m_filterState = 0.0;  /**< One-pole output low pass filter state. */
};

/**
 * @brief Runs a Game Boy cartridge.
 *
 * Typical use:
 * @code
 * gb::Emulation emulator;
 * emulator.loadRom(romBytes, error);
 * emulator.reset();
 * while (running) {
 *     emulator.runFrame();
 *     const gb::u8 *pixels = emulator.frameBuffer();
 * }
 * @endcode
 */
class Emulation {
public:
    /**
     * @brief Constructs an emulator with no cartridge inserted.
     */
    Emulation();

    /**
     * @brief Destroys the emulator.
     */
    ~Emulation();

    Emulation(const Emulation &) = delete;
    Emulation &operator=(const Emulation &) = delete;

    /**
     * @brief Loads a ROM image and prepares the machine for execution.
     * @param data Raw bytes of the .gb file.
     * @param error Receives a message when the ROM cannot be loaded.
     * @return True when the ROM was accepted.
     */
    bool loadRom(const std::vector<u8> &data, std::string &error);

    /**
     * @brief Loads a ROM image from disk.
     * @param path Path of the .gb file.
     * @param error Receives a message when the file cannot be loaded.
     * @return True when the ROM was accepted.
     */
    bool loadRomFile(const std::string &path, std::string &error);

    /**
     * @brief Ejects the cartridge and powers the machine off.
     */
    void unloadRom();

    /**
     * @brief Powers the machine on with the inserted cartridge.
     */
    void reset();

    /**
     * @brief Executes one instruction and advances the peripherals.
     * @return Number of cycles consumed.
     */
    int step();

    /**
     * @brief Runs until the next frame has been rendered.
     */
    void runFrame();

    /**
     * @brief Runs a number of CPU cycles.
     * @param cycles Number of cycles to execute.
     */
    void runCycles(int cycles);

    /**
     * @brief Tells whether a cartridge is inserted.
     * @return True when a ROM is loaded.
     */
    bool isRomLoaded() const;

    /**
     * @brief Tells whether the machine is powered on.
     * @return True after a successful reset().
     */
    bool isRunning() const;

    /**
     * @brief Returns the RGBA8888 framebuffer of the last frame.
     * @return Pointer to 160*144*4 bytes.
     */
    const u8 *frameBuffer() const;

    /**
     * @brief Returns the number of frames rendered since power on.
     * @return The frame counter.
     */
    std::uint64_t frameCount() const;

    /**
     * @brief Returns the CPU.
     * @return Reference to the CPU.
     */
    Opcode &cpu();

    /**
     * @brief Returns the CPU (read only).
     * @return Constant reference to the CPU.
     */
    const Opcode &cpu() const;

    /**
     * @brief Returns the memory bus.
     * @return Reference to the memory bus.
     */
    Memory &memory();

    /**
     * @brief Returns the memory bus (read only).
     * @return Constant reference to the memory bus.
     */
    const Memory &memory() const;

    /**
     * @brief Returns the GPU.
     * @return Reference to the GPU.
     */
    GPU &gpu();

    /**
     * @brief Returns the GPU (read only).
     * @return Constant reference to the GPU.
     */
    const GPU &gpu() const;

    /**
     * @brief Selects the colour source of the four LCD shades.
     * @param palette The palette to use.
     */
    void setDisplayPalette(DisplayPalette palette);

    /**
     * @brief Returns the colour source of the four LCD shades.
     * @return The active palette.
     */
    DisplayPalette displayPalette() const;

    /**
     * @brief Replaces the four custom shades (RRGGBBAA).
     * @param colors The four colours, index 0 = lightest.
     */
    void setCustomPaletteColors(const std::array<u32, 4> &colors);

    /**
     * @brief Returns the four shades used with the active palette.
     * @return The colours, index 0 = lightest, in RRGGBBAA order.
     */
    std::array<u32, 4> activePaletteColors() const;

    /**
     * @brief Returns the interrupt controller.
     * @return Reference to the interrupt controller.
     */
    InterruptController &interrupts();

    /**
     * @brief Returns the interrupt controller (read only).
     * @return Constant reference to the interrupt controller.
     */
    const InterruptController &interrupts() const;

    /**
     * @brief Returns the audio unit.
     * @return Reference to the audio unit.
     */
    AudioUnit &audio();

    /**
     * @brief Returns the audio unit (read only).
     * @return Constant reference to the audio unit.
     */
    const AudioUnit &audio() const;

    /**
     * @brief Returns the metadata of the inserted cartridge.
     * @return Constant reference to the cartridge descriptor.
     */
    const Cartridge &cartridge() const;

    /**
     * @brief Returns the battery backed RAM for saving.
     * @return The raw save data (empty when the cartridge has no battery).
     */
    const std::vector<u8> &saveData() const;

    /**
     * @brief Restores the battery backed RAM from a save file.
     * @param data Bytes previously returned by saveData().
     */
    void loadSaveData(const std::vector<u8> &data);

    /**
     * @brief Serializes the complete machine state into a byte array.
     *
     * The byte array is self contained (CPU, memory, GPU, interrupts, timer and
     * cartridge RAM) and can be stored in a .state file next to the ROM.
     * @return The raw state data.
     */
    std::vector<u8> saveState() const;

    /**
     * @brief Restores the machine from a byte array produced by saveState().
     * @param data The state data.
     * @param error Receives a message when the data is invalid or was saved
     *              with a different cartridge.
     * @return True when the state was restored.
     */
    bool loadState(const std::vector<u8> &data, std::string &error);

    /**
     * @brief Presses or releases a button of the joypad.
     * @param index Button index (0 = right, 1 = left, 2 = up, 3 = down,
     *              4 = A, 5 = B, 6 = select, 7 = start).
     * @param pressed True when the button is held down.
     */
    void setButton(int index, bool pressed);

    /**
     * @brief Returns the text written to the serial port by the running ROM.
     * @return The captured serial output.
     */
    const std::string &serialOutput() const;

    /**
     * @brief Clears the captured serial output.
     */
    void clearSerialOutput();

    /**
     * @brief Enables or disables the internal boot ROM.
     * @param enabled True to start from the boot ROM on the next reset.
     */
    void setBootRomEnabled(bool enabled);

    /**
     * @brief Tells whether the internal boot ROM will be executed.
     * @return True when the boot ROM is enabled.
     */
    bool isBootRomEnabled() const;

    /**
     * @brief Returns the number of CPU cycles executed since power on.
     * @return The cycle counter.
     */
    std::uint64_t cycleCount() const;

    /**
     * @brief Returns the samples generated since the last call to takeAudioSamples().
     * @return Mono 16-bit samples.
     */
    std::vector<std::int16_t> takeAudioSamples();

    /**
     * @brief Returns the name of the mapper of the inserted cartridge.
     * @return A static string such as "MBC1".
     */
    static const char *mapperName(MapperType mapper);

private:
    /**
     * @brief Advances every peripheral by a number of cycles.
     * @param cycles Number of CPU cycles elapsed.
     */
    void advancePeripherals(int cycles);

    Memory m_memory;                 /**< Memory bus. */
    Opcode m_cpu;                    /**< CPU. */
    GPU m_gpu;                       /**< Pixel processing unit. */
    InterruptController m_interrupts; /**< Interrupt controller. */
    AudioUnit m_audio;               /**< Audio unit. */
    bool m_running = false;          /**< Power state. */
    bool m_bootRomEnabled = true;    /**< Boot ROM preference. */
    std::uint64_t m_cycles = 0;      /**< Cycle counter. */
    std::uint64_t m_frameBaseline = 0; /**< GPU frame counter at the last runFrame(). */
};

} // namespace gb

#endif // GB_EMULATION_H
