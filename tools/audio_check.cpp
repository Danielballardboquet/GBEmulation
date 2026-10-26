/**
 * @file audio_check.cpp
 * @brief Standalone verification harness for the emulator's APU.
 *
 * Drives the real gb::AudioUnit / gb::Emulation code (no reimplementation)
 * and measures the produced waveform, so the numbers below describe what the
 * emulator actually emits.
 *
 * Usage:
 *   audio_check            run the synthetic channel tests
 *   audio_check <rom>      additionally run the ROM for N frames and report
 *                          the sample rate and signal statistics
 */

#include "emulation/Emulation.h"
#include "emulation/Types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char *what, const std::string &detail = std::string())
{
    std::printf("[%s] %s%s%s\n", ok ? " OK " : "FAIL", what,
                detail.empty() ? "" : " -- ", detail.c_str());
    if (!ok) {
        ++g_failures;
    }
}

/** @brief Duty ratio of a sample block: fraction of samples above zero. */
double dutyRatio(const std::vector<std::int16_t> &s, std::size_t skip = 0)
{
    if (s.size() <= skip) {
        return 0.0;
    }
    std::size_t high = 0;
    for (std::size_t i = skip; i < s.size(); ++i) {
        if (s[i] > 0) {
            ++high;
        }
    }
    return static_cast<double>(high) / static_cast<double>(s.size() - skip);
}

double peak(const std::vector<std::int16_t> &s)
{
    double p = 0.0;
    for (std::int16_t v : s) {
        p = std::max(p, std::fabs(static_cast<double>(v)));
    }
    return p;
}

/** @brief Counts sign changes, i.e. one per half period. */
int zeroCrossings(const std::vector<std::int16_t> &s, double threshold = 1000.0)
{
    int count = 0;
    int lastSign = 0;
    for (std::int16_t v : s) {
        if (std::fabs(static_cast<double>(v)) < threshold) {
            continue;
        }
        const int sign = v > 0 ? 1 : -1;
        if (lastSign != 0 && sign != lastSign) {
            ++count;
        }
        lastSign = sign;
    }
    return count;
}

/** @brief Runs the APU for @p cycles CPU cycles and returns the samples. */
std::vector<std::int16_t> run(gb::AudioUnit &apu, int cycles)
{
    for (int i = 0; i < cycles; ++i) {
        apu.tick(1);
    }
    return apu.takeSamples();
}

void powerOn(gb::AudioUnit &apu)
{
    apu.reset();
    apu.writeRegister(0xFF26, 0x80); // NR52: APU on
    apu.writeRegister(0xFF24, 0x77); // NR50: max volume both sides
    apu.writeRegister(0xFF25, 0xFF); // NR51: all channels to both sides
}

void testDuty()
{
    const double expected[4] = {0.125, 0.25, 0.5, 0.75};
    for (int duty = 0; duty < 4; ++duty) {
        gb::AudioUnit apu;
        powerOn(apu);
        apu.writeRegister(0xFF12, 0xF0); // volume 15, no envelope
        apu.writeRegister(0xFF11, static_cast<gb::u8>(duty << 6));
        apu.writeRegister(0xFF13, 0x00); // freq low
        apu.writeRegister(0xFF14, 0x80); // trigger, freq high 0
        const auto s = run(apu, gb::kCyclesPerSecond / 4);
        const double d = dutyRatio(s, s.size() / 10);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "duty %d: %.1f%% (expect %.1f%%)", duty, d * 100.0,
                      expected[duty] * 100.0);
        check(std::fabs(d - expected[duty]) < 0.03, "square duty cycle", buf);
    }
}

void testSquareFrequency()
{
    // Square period = (2048 - freq) * 4 cycles. Use 0x400 -> 4096 cycles ->
    // 1024 Hz.
    gb::AudioUnit apu;
    powerOn(apu);
    apu.writeRegister(0xFF12, 0xF0);
    apu.writeRegister(0xFF11, 0x80); // duty 2 (50%)
    apu.writeRegister(0xFF13, 0x00);
    apu.writeRegister(0xFF14, 0x84); // trigger, freq high = 4 -> 0x400
    const int seconds = 1;
    const auto s = run(apu, gb::kCyclesPerSecond * seconds);
    const int crossings = zeroCrossings(s, 2000.0);
    const double measured = crossings / 2.0 / seconds;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.1f Hz (expect 1024 Hz)", measured);
    check(std::fabs(measured - 1024.0) < 10.0, "square frequency", buf);
}

void testWaveVolume()
{
    const double expected[4] = {0.0, 1.0, 0.5, 0.25};
    for (int code = 0; code < 4; ++code) {
        gb::AudioUnit apu;
        powerOn(apu);
        // Square wave pattern in wave RAM: 0x0F 0x0F ... gives full swing.
        for (int i = 0; i < 16; ++i) {
            apu.writeRegister(static_cast<gb::u16>(0xFF30 + i), 0x0F);
        }
        apu.writeRegister(0xFF1A, 0x80); // DAC on
        apu.writeRegister(0xFF1C, static_cast<gb::u8>(code << 5));
        apu.writeRegister(0xFF1D, 0x00);
        apu.writeRegister(0xFF1E, 0x84); // trigger, freq 0x400
        const auto s = run(apu, gb::kCyclesPerSecond / 4);
        const double p = peak(s) / 32767.0;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "NR32 code %d: peak %.3f (expect %.3f)", code, p,
                      expected[code]);
        check(std::fabs(p - expected[code]) < 0.02, "wave volume", buf);
    }
}

void testEnvelope()
{
    gb::AudioUnit apu;
    powerOn(apu);
    apu.writeRegister(0xFF12, 0xF1); // volume 15, decrease, period 1
    apu.writeRegister(0xFF11, 0x80);
    apu.writeRegister(0xFF14, 0x80);
    // Period 1 means one step every 1/64 s, so after ~0.25 s the volume has
    // dropped from 15 to about 0 and the channel must be silent.
    const auto early = run(apu, gb::kCyclesPerSecond / 10);
    const auto late = run(apu, gb::kCyclesPerSecond * 3 / 10);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "peak %.0f -> %.0f", peak(early), peak(late));
    check(peak(early) > 20000.0 && peak(late) < peak(early) * 0.5, "envelope decay", buf);
}

void testNoise()
{
    gb::AudioUnit apu;
    powerOn(apu);
    apu.writeRegister(0xFF21, 0xF0); // volume 15, no envelope
    apu.writeRegister(0xFF22, 0x04); // divisor 4, shift 0 -> 64 cycles
    apu.writeRegister(0xFF23, 0x80); // trigger
    const auto s = run(apu, gb::kCyclesPerSecond / 20);
    const int crossings = zeroCrossings(s, 500.0);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "peak %.0f, %d sign changes", peak(s), crossings);
    check(peak(s) > 20000.0 && crossings > 100, "noise channel active", buf);
}

void testPowerOff()
{
    gb::AudioUnit apu;
    powerOn(apu);
    apu.writeRegister(0xFF12, 0xF0);
    apu.writeRegister(0xFF14, 0x80);
    (void)run(apu, 2000);
    apu.writeRegister(0xFF26, 0x00); // power off
    const auto s = run(apu, gb::kCyclesPerSecond / 10);
    check(peak(s) == 0.0, "NR52 power off silences output");
    const gb::u8 nr52 = apu.readRegister(0xFF26);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "NR52 = 0x%02X", nr52);
    check((nr52 & 0x80) == 0 && (nr52 & 0x0F) == 0, "NR52 reads back powered off", buf);
}

void testIdleSilence()
{
    gb::AudioUnit apu;
    powerOn(apu);
    const auto s = run(apu, gb::kCyclesPerSecond / 10);
    check(peak(s) == 0.0, "idle APU is silent");
}

void testSampleRate()
{
    // One frame must yield exactly the number of samples the sound card
    // consumes in one frame; this is the property whose violation made the
    // output crackle.
    gb::AudioUnit apu;
    powerOn(apu);
    const int frames = 600;
    std::size_t total = 0;
    for (int f = 0; f < frames; ++f) {
        total += run(apu, gb::kCyclesPerFrame).size();
    }
    const double rate = static_cast<double>(total) / frames * gb::kFramesPerSecond;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%.0f samples/s over %d frames (expect %d)", rate, frames,
                  gb::kSampleRate);
    check(std::fabs(rate - gb::kSampleRate) < 50.0, "sample rate matches playback rate", buf);
}

void testRom(const std::string &path)
{
    gb::Emulation emu;
    std::string error;
    if (!emu.loadRomFile(path, error)) {
        check(false, "ROM load", error);
        return;
    }
    emu.reset();

    const int frames = 600; // ~10 s of emulated time
    std::size_t total = 0;
    std::size_t nonZero = 0;
    double peakValue = 0.0;
    for (int f = 0; f < frames; ++f) {
        emu.runFrame();
        const auto s = emu.takeAudioSamples();
        total += s.size();
        for (std::int16_t v : s) {
            if (v != 0) {
                ++nonZero;
            }
            peakValue = std::max(peakValue, std::fabs(static_cast<double>(v)));
        }
    }
    const double rate = static_cast<double>(total) / frames * gb::kFramesPerSecond;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%.0f samples/s, %.1f%% non-silent, peak %.0f", rate,
                  100.0 * static_cast<double>(nonZero) / static_cast<double>(total), peakValue);
    check(std::fabs(rate - gb::kSampleRate) < 50.0, "ROM sample rate", buf);
    check(nonZero > 0, "ROM produces audible output");
}

} // namespace

int main(int argc, char **argv)
{
    testDuty();
    testSquareFrequency();
    testWaveVolume();
    testEnvelope();
    testNoise();
    testPowerOff();
    testIdleSilence();
    testSampleRate();
    if (argc > 1) {
        testRom(argv[1]);
    }
    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
