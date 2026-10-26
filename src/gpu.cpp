/**
 * @file gpu.cpp
 * @brief Implementation of the pixel processing unit of the Game Boy.
 *
 * The GPU advances a dot clock shared with the CPU cycle counter)Skip. Every
 * scanline is produced by rendering the background/window and the sprites into
 * an internal RGBA framebuffer of 160x144 pixels. The LCD registers 0xFF40 to
 * 0xFF4B (plus the CGB palette registers 0xFF68-0xFF6B) are owned here and
 * forwarded by the memory bus.
 */

#include "emulation/GPU.h"
#include "emulation/Interrupt.h"
#include "emulation/Memory.h"

#include <algorithm>

namespace gb {

namespace {

/** @brief Number of dots spent in OAM search mode (mode 2). */
constexpr int kMode2Dots = 80;
/** @brief Number of dots spent in pixel transfer mode (mode 3). */
constexpr int kMode3Dots = 172;
/** @brief Number of dots spent in HBlank mode (mode 0). */
constexpr int kMode0Dots = kDotsPerLine - kMode2Dots - kMode3Dots;

/**
 * @brief Updates a CGB palette index, applying the auto increment flag.
 * @param index The index register (bit 7 selects auto increment).
 */
void stepPaletteIndex(u8 &index)
{
    if (isBitSet(index, 7)) {
        index = static_cast<u8>((index & 0x80) | ((index + 1) & 0x3F));
    }
}

} // namespace

GPU::GPU()
{
    m_frameBuffer.assign(kFrameBufferBytes, 0xFF);
    reset();
}

void GPU::setMemory(Memory *memory)
{
    m_memory = memory;
}

void GPU::setInterrupts(InterruptController *interrupts)
{
    m_interrupts = interrupts;
}

void GPU::setVideoRam(u8 *vram)
{
    m_vram = vram;
}

void GPU::reset()
{
    m_lcdc = 0x91;
    m_stat = 0x85;
    m_scy = 0;
    m_scx = 0;
    m_ly = 0;
    m_lyc = 0;
    m_bgp = 0xFC;
    m_obp0 = 0xFF;
    m_obp1 = 0xFF;
    m_wy = 0;
    m_wx = 0;
    m_dma = 0xFF;
    m_bgpi = 0;
    m_obpi = 0;
    m_bgPaletteRam.fill(0xFF);
    m_objPaletteRam.fill(0xFF);
    m_dot = 0;
    m_mode = 2;
    m_windowLine = 0;
    m_spritesOnLine = 0;
    m_frameCount = 0;
    m_disabledFrames = 0;
    m_statLine = false;
    clearFrameBuffer();
}

void GPU::tick(int cycles)
{
    if (cycles <= 0) {
        return;
    }
    if (!isLcdEnabled()) {
        m_dot += cycles;
        if (m_dot >= kDotsPerLine * kLinesPerFrame) {
            m_dot = 0;
            m_ly = 0;
            ++m_disabledFrames;
            clearFrameBuffer();
        }
        return;
    }

    m_dot += cycles;
    if (m_mode == 2) {
        if (m_dot >= kMode2Dots) {
            m_mode = 3;
            renderLine();
            updateStatInterrupt();
        }
    } else if (m_mode == 3) {
        if (m_dot >= kMode2Dots + kMode3Dots) {
            m_mode = 0;
            updateStatInterrupt();
        }
    } else if (m_mode == 0) {
        if (m_dot >= kDotsPerLine) {
            m_dot -= kDotsPerLine;
            m_ly = static_cast<u8>(m_ly + 1);
            updateCoincidence();
            if (m_ly == kScreenHeight) {
                m_mode = 1;
                if (m_interrupts != nullptr) {
                    m_interrupts->request(InterruptType::VBlank);
                }
                ++m_frameCount;
                updateStatInterrupt();
            } else {
                m_mode = 2;
                updateStatInterrupt();
            }
        }
    } else if (m_mode == 1) {
        if (m_dot >= kDotsPerLine) {
            m_dot -= kDotsPerLine;
            m_ly = static_cast<u8>(m_ly + 1);
            updateCoincidence();
            if (m_ly > kLinesPerFrame - 1) {
                m_ly = 0;
                m_windowLine = 0;
                m_mode = 2;
                updateCoincidence();
                updateStatInterrupt();
            }
        }
    }
}

u8 GPU::readRegister(u16 address) const
{
    switch (address) {
    case 0xFF40:
        return m_lcdc;
    case 0xFF41:
        return statRegister();
    case 0xFF42:
        return m_scy;
    case 0xFF43:
        return m_scx;
    case 0xFF44:
        return m_ly;
    case 0xFF45:
        return m_lyc;
    case 0xFF46:
        return m_dma;
    case 0xFF47:
        return m_bgp;
    case 0xFF48:
        return m_obp0;
    case 0xFF49:
        return m_obp1;
    case 0xFF4A:
        return m_wy;
    case 0xFF4B:
        return m_wx;
    case 0xFF68:
        return m_bgpi;
    case 0xFF69:
        return readPaletteRam(address);
    case 0xFF6A:
        return m_obpi;
    case 0xFF6B:
        return readPaletteRam(address);
    default:
        return 0xFF;
    }
}

void GPU::writeRegister(u16 address, u8 value)
{
    switch (address) {
    case 0xFF40:
        m_lcdc = value;
        if (!isLcdEnabled()) {
            m_mode = 0;
            m_ly = 0;
            m_dot = 0;
            m_windowLine = 0;
            clearFrameBuffer();
        }
        break;
    case 0xFF41:
        m_stat = static_cast<u8>((m_stat & 0x07) | (value & 0xF8));
        updateStatInterrupt();
        break;
    case 0xFF42:
        m_scy = value;
        break;
    case 0xFF43:
        m_scx = value;
        break;
    case 0xFF44:
        break; // LY is read only.
    case 0xFF45:
        m_lyc = value;
        updateCoincidence();
        updateStatInterrupt();
        break;
    case 0xFF46: {
        m_dma = value;
        if (m_memory != nullptr) {
            const u16 source = static_cast<u16>(value) << 8;
            for (int i = 0; i < 0xA0; ++i) {
                m_memory->write(static_cast<u16>(0xFE00 + i),
                                m_memory->read(static_cast<u16>(source + i)));
            }
        }
        break;
    }
    case 0xFF47:
        m_bgp = value;
        break;
    case 0xFF48:
        m_obp0 = value;
        break;
    case 0xFF49:
        m_obp1 = value;
        break;
    case 0xFF4A:
        m_wy = value;
        break;
    case 0xFF4B:
        m_wx = value;
        break;
    case 0xFF68:
        m_bgpi = value & 0xBF;
        break;
    case 0xFF69:
        writePaletteRam(address, value);
        break;
    case 0xFF6A:
        m_obpi = value & 0xBF;
        break;
    case 0xFF6B:
        writePaletteRam(address, value);
        break;
    default:
        break;
    }
}

const u8 *GPU::frameBuffer() const
{
    return m_frameBuffer.data();
}

std::uint64_t GPU::frameCount() const
{
    return m_frameCount;
}

int GPU::currentLine() const
{
    return m_ly;
}

int GPU::mode() const
{
    return m_mode;
}

bool GPU::isLcdEnabled() const
{
    return isBitSet(m_lcdc, 7);
}

u8 GPU::lcdControl() const
{
    return m_lcdc;
}

u8 GPU::statRegister() const
{
    return static_cast<u8>((m_stat & 0xF8) | (m_mode & 0x03)
                           | (m_ly == m_lyc ? 0x04 : 0x00));
}

void GPU::scrollRegisters(u8 &scrollX, u8 &scrollY) const
{
    scrollX = m_scx;
    scrollY = m_scy;
}

void GPU::paletteRegisters(u8 &background, u8 &sprites, u8 &sprites2) const
{
    background = m_bgp;
    sprites = m_obp0;
    sprites2 = m_obp1;
}

void GPU::windowRegisters(u8 &windowX, u8 &windowY) const
{
    windowX = m_wx;
    windowY = m_wy;
}

u8 GPU::dmaRegister() const
{
    return m_dma;
}

const u8 *GPU::backgroundPaletteRam() const
{
    return m_bgPaletteRam.data();
}

const u8 *GPU::spritePaletteRam() const
{
    return m_objPaletteRam.data();
}

int GPU::spritesOnLine() const
{
    return m_spritesOnLine;
}

std::uint64_t GPU::disabledFrames() const
{
    return m_disabledFrames;
}

std::vector<u8> GPU::snapshot() const
{
    return m_frameBuffer;
}

GPU::GpuState GPU::gpuState() const
{
    GpuState state;
    state.lcdc = m_lcdc;
    state.stat = m_stat;
    state.scy = m_scy;
    state.scx = m_scx;
    state.ly = m_ly;
    state.lyc = m_lyc;
    state.bgp = m_bgp;
    state.obp0 = m_obp0;
    state.obp1 = m_obp1;
    state.wy = m_wy;
    state.wx = m_wx;
    state.dma = m_dma;
    state.bgpi = m_bgpi;
    state.obpi = m_obpi;
    state.dot = m_dot;
    state.mode = m_mode;
    state.windowLine = m_windowLine;
    state.spritesOnLine = m_spritesOnLine;
    state.frameCount = m_frameCount;
    state.disabledFrames = m_disabledFrames;
    state.statLine = m_statLine;
    state.bgPaletteRam = m_bgPaletteRam;
    state.objPaletteRam = m_objPaletteRam;
    state.frameBuffer = m_frameBuffer;
    return state;
}

void GPU::restoreGpuState(const GPU::GpuState &state)
{
    m_lcdc = state.lcdc;
    m_stat = state.stat;
    m_scy = state.scy;
    m_scx = state.scx;
    m_ly = state.ly;
    m_lyc = state.lyc;
    m_bgp = state.bgp;
    m_obp0 = state.obp0;
    m_obp1 = state.obp1;
    m_wy = state.wy;
    m_wx = state.wx;
    m_dma = state.dma;
    m_bgpi = state.bgpi;
    m_obpi = state.obpi;
    m_dot = state.dot;
    m_mode = state.mode;
    m_windowLine = state.windowLine;
    m_spritesOnLine = state.spritesOnLine;
    m_frameCount = state.frameCount;
    m_disabledFrames = state.disabledFrames;
    m_statLine = state.statLine;
    m_bgPaletteRam = state.bgPaletteRam;
    m_objPaletteRam = state.objPaletteRam;
    m_frameBuffer = state.frameBuffer;
}

void GPU::renderLine()
{
    m_lineColors.fill(0);
    m_linePriority.fill(false);
    renderBackground(m_ly);
    renderSprites(m_ly);
}

void GPU::renderBackground(int line)
{
    const bool windowEnabled = isBitSet(m_lcdc, 5);
    const bool windowVisible = windowEnabled && m_wy <= 143 && m_wx <= 166 && line >= m_wy;
    const int windowX = m_wx >= 7 ? m_wx - 7 : 0;

    for (int x = 0; x < kScreenWidth; ++x) {
        if (windowVisible && x >= windowX) {
            const u16 mapBase = isBitSet(m_lcdc, 6) ? 0x9C00 : 0x9800;
            drawBackgroundPixel(line, x, x - windowX, m_windowLine, mapBase);
        } else {
            const u16 mapBase = isBitSet(m_lcdc, 3) ? 0x9C00 : 0x9800;
            const int mapX = (x + m_scx) & 0xFF;
            const int mapY = (line + m_scy) & 0xFF;
            drawBackgroundPixel(line, x, mapX, mapY, mapBase);
        }
    }
    if (windowVisible) {
        ++m_windowLine;
    }
}

void GPU::drawBackgroundPixel(int line, int x, int mapX, int mapY, u16 mapBase)
{
    if (m_memory == nullptr) {
        putPixel(x, line, shadeColor(0, m_bgp));
        return;
    }
    const std::size_t mapIndex = static_cast<std::size_t>((mapY >> 3) * 32) + (mapX >> 3);
    const u8 tileId = m_vram[(mapBase + mapIndex) & 0x1FFF];
    u16 tileAddress;
    if (isBitSet(m_lcdc, 4)) {
        tileAddress = static_cast<u16>(0x8000 + tileId * 16);
    } else {
        tileAddress = static_cast<u16>(0x9000 + static_cast<s8>(tileId) * 16);
    }
    const u8 shade = tilePixel(tileAddress, mapX & 0x07, mapY & 0x07);
    m_lineColors[x] = shade;
    m_linePriority[x] = shade != 0;
    putPixel(x, line, shadeColor(shade, m_bgp));
}

void GPU::renderSprites(int line)
{
    m_spritesOnLine = 0;
    if (m_memory == nullptr || !isBitSet(m_lcdc, 1)) {
        return;
    }
    const bool tallSprites = isBitSet(m_lcdc, 2);
    const int height = tallSprites ? 16 : 8;
    const int spriteY = line; // Scanline currently being rendered.

    struct SpriteCandidate {
        int x = 0;       /**< X position on screen. */
        int y = 0;       /**< Y position on screen. */
        u8 tile = 0;     /**< Tile index. */
        u8 attr = 0;     /**< Attribute byte. */
        int oamIndex = 0;/**< Index inside the OAM, used for tie breaking. */
    };
    std::array<SpriteCandidate, kMaxSpritesPerLine> candidates{};
    int count = 0;

    for (int i = 0; i < kSpriteCount && count < kMaxSpritesPerLine; ++i) {
        const u8 y = m_memory->read(static_cast<u16>(0xFE00 + i * 4));
        const u8 x = m_memory->read(static_cast<u16>(0xFE00 + i * 4 + 1));
        if (y > 160 || x >= 168) {
            continue;
        }
        const int top = static_cast<int>(y) - 16;
        const int bottom = top + height;
        if (spriteY < top || spriteY >= bottom) {
            continue;
        }
        candidates[count].x = x;
        candidates[count].y = y;
        candidates[count].tile = m_memory->read(static_cast<u16>(0xFE00 + i * 4 + 2));
        candidates[count].attr = m_memory->read(static_cast<u16>(0xFE00 + i * 4 + 3));
        candidates[count].oamIndex = i;
        ++count;
    }

    // Sort by X ascending (OAM order breaks ties), then draw from the far
    // right so that lower X sprites are drawn on top.
    std::sort(candidates.begin(), candidates.begin() + count,
              [](const SpriteCandidate &a, const SpriteCandidate &b) {
                  if (a.x != b.x) {
                      return a.x < b.x;
                  }
                  return a.oamIndex < b.oamIndex;
              });

    for (int i = count - 1; i >= 0; --i) {
        const SpriteCandidate &sprite = candidates[i];
        int row = spriteY - (static_cast<int>(sprite.y) - 16);
        const bool flipY = isBitSet(sprite.attr, 6);
        if (flipY) {
            row = height - 1 - row;
        }
        u8 tileIndex = sprite.tile;
        if (tallSprites) {
            tileIndex = static_cast<u8>(tileIndex & 0xFE);
            if (row >= 8) {
                tileIndex = static_cast<u8>(tileIndex + 1);
            }
        }
        const u16 tileAddress = static_cast<u16>(0x8000 + tileIndex * 16);
        const int rowInTile = row & 0x07;
        const bool flipX = isBitSet(sprite.attr, 5);
        const u8 palette = isBitSet(sprite.attr, 4) ? m_obp1 : m_obp0;

        for (int px = 0; px < 8; ++px) {
            const int screenX = sprite.x - 8 + px;
            if (screenX < 0 || screenX >= kScreenWidth) {
                continue;
            }
            const int col = flipX ? 7 - px : px;
            const u8 shade = tilePixel(tileAddress, col, rowInTile);
            if (shade == 0) {
                continue;
            }
            // Priority flag: the sprite is hidden behind non-transparent
            // background pixels.
            if (isBitSet(sprite.attr, 7) && m_linePriority[screenX]) {
                continue;
            }
            putPixel(screenX, line, shadeColor(shade, palette));
            ++m_spritesOnLine;
        }
    }
}

u8 GPU::tilePixel(u16 tileAddress, int x, int y) const
{
    if (m_memory == nullptr || m_vram == nullptr) {
        return 0;
    }
    const u8 low = m_vram[(tileAddress + y * 2) & 0x1FFF];
    const u8 high = m_vram[(tileAddress + y * 2 + 1) & 0x1FFF];
    const int bit = 7 - x;
    return static_cast<u8>(((high >> bit) & 0x01u) << 1 | ((low >> bit) & 0x01u));
}

void GPU::setDisplayPalette(DisplayPalette palette)
{
    m_displayPalette = palette;
}

void GPU::setCustomPaletteColors(const std::array<u32, 4> &colors)
{
    m_customPalette = colors;
}

std::array<u32, 4> GPU::activePaletteColors() const
{
    switch (m_displayPalette) {
    case DisplayPalette::Gbc:
        // Authentic palette of the Game Boy Color used to colourise
        // monochrome games: a yellow-green ramp instead of a single hue.
        return {0xE0F8D0FF, 0x88C070FF, 0x346856FF, 0x081820FF};
    case DisplayPalette::Custom:
        return m_customPalette;
    case DisplayPalette::Dmg:
    default:
        // Famous green shades of the original 1989 "Dot Matrix" LCD.
        return {0x9BBC0FFF, 0x8BAC0FFF, 0x306230FF, 0x0F380FFF};
    }
}

u32 GPU::shadeColor(u8 shade, u8 palette) const
{
    const int index = (palette >> (shade * 2)) & 0x03;
    return activePaletteColors()[static_cast<std::size_t>(index)];
}

void GPU::putPixel(int x, int y, u32 color)
{
    if (x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight) {
        return;
    }
    const std::size_t offset = (static_cast<std::size_t>(y) * kScreenWidth + x) * 4;
    m_frameBuffer[offset] = static_cast<u8>((color >> 24) & 0xFF);
    m_frameBuffer[offset + 1] = static_cast<u8>((color >> 16) & 0xFF);
    m_frameBuffer[offset + 2] = static_cast<u8>((color >> 8) & 0xFF);
    m_frameBuffer[offset + 3] = static_cast<u8>(color & 0xFF);
}

void GPU::updateStatInterrupt()
{
    const bool coincidence = (m_ly == m_lyc);
    const bool condition = (isBitSet(m_stat, 6) && coincidence)
                           || (isBitSet(m_stat, 5) && m_mode == 2)
                           || (isBitSet(m_stat, 4) && m_mode == 3)
                           || (isBitSet(m_stat, 3) && m_mode == 0);
    if (condition && !m_statLine && m_interrupts != nullptr) {
        m_interrupts->request(InterruptType::LCDStat);
    }
    m_statLine = condition;
}

void GPU::updateCoincidence()
{
    m_stat = setBit(m_stat, 2, m_ly == m_lyc);
}

void GPU::clearFrameBuffer()
{
    std::fill(m_frameBuffer.begin(), m_frameBuffer.end(), static_cast<u8>(0xFF));
}

u8 GPU::readPaletteRam(u16 address) const
{
    if (address == 0xFF69) {
        return m_bgPaletteRam[m_bgpi & 0x3F];
    }
    if (address == 0xFF6B) {
        return m_objPaletteRam[m_obpi & 0x3F];
    }
    return 0xFF;
}

void GPU::writePaletteRam(u16 address, u8 value)
{
    if (address == 0xFF69) {
        m_bgPaletteRam[m_bgpi & 0x3F] = value;
        stepPaletteIndex(m_bgpi);
    } else if (address == 0xFF6B) {
        m_objPaletteRam[m_obpi & 0x3F] = value;
        stepPaletteIndex(m_obpi);
    }
}

} // namespace gb
