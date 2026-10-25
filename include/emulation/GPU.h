/**
 * @file GPU.h
 * @brief Pixel processing unit (PPU) of the Game Boy: LCD registers, scanline
 *        timing, background/window rendering, sprites and the RGBA framebuffer.
 */

#ifndef GB_GPU_H
#define GB_GPU_H

#include "emulation/Types.h"

#include <array>
#include <cstddef>
#include <vector>

namespace gb {

class Memory;
class InterruptController;

/**
 * @brief The LCD controller of the Game Boy.
 *
 * The GPU is driven by the cycle counter of the emulator: every call to
 * tick() advances the dot clock and, when the 160 pixels of a scanline have
 * been produced, renders that line into an internal 160x144 RGBA framebuffer.
 */
class GPU {
public:
    /**
     * @brief Constructs a powered off GPU with default register values.
     */
    GPU();

    /**
     * @brief Attaches the memory bus used to fetch tiles, maps and OAM.
     * @param memory Pointer to the memory bus.
     */
    void setMemory(Memory *memory);

    /**
     * @brief Attaches the interrupt controller used to raise STAT interrupts.
     * @param interrupts Pointer to the interrupt controller.
     */
    void setInterrupts(InterruptController *interrupts);

    /**
     * @brief Attaches the video RAM owned by the memory bus.
     * @param vram Pointer to the 8 KiB video RAM array.
     */
    void setVideoRam(u8 *vram);

    /**
     * @brief Resets the GPU to its post-boot state.
     */
    void reset();

    /**
     * @brief Advances the LCD by a number of CPU cycles.
     * @param cycles Number of cycles elapsed.
     */
    void tick(int cycles);

    /**
     * @brief Reads one of the LCD registers.
     * @param address Address inside 0xFF40-0xFF4B.
     * @return The register value.
     */
    u8 readRegister(u16 address) const;

    /**
     * @brief Writes one of the LCD registers.
     * @param address Address inside 0xFF40-0xFF4B.
     * @param value The value to write.
     */
    void writeRegister(u16 address, u8 value);

    /**
     * @brief Returns the RGBA8888 framebuffer of the last completed frame.
     * @return Pointer to 160*144*4 bytes.
     */
    const u8 *frameBuffer() const;

    /**
     * @brief Returns the number of frames rendered so far.
     * @return The frame counter.
     */
    std::uint64_t frameCount() const;

    /**
     * @brief Returns the current scanline (LY register).
     * @return The line index (0..153).
     */
    int currentLine() const;

    /**
     * @brief Returns the current mode of the LCD.
     * @return 0 = HBlank, 1 = VBlank, 2 = OAM search, 3 = pixel transfer.
     */
    int mode() const;

    /**
     * @brief Tells whether the LCD is switched on.
     * @return True when bit 7 of LCDC is set.
     */
    bool isLcdEnabled() const;

    /**
     * @brief Returns the current LCD control register.
     * @return The value of LCDC.
     */
    u8 lcdControl() const;

    /**
     * @brief Returns the current STAT register value.
     * @return The value of STAT including the mode bits.
     */
    u8 statRegister() const;

    /**
     * @brief Returns the scroll registers for the debugger.
     * @param scrollX Receives SCX.
     * @param scrollY Receives SCY.
     */
    void scrollRegisters(u8 &scrollX, u8 &scrollY) const;

    /**
     * @brief Returns the palette registers for the debugger.
     * @param background Receives BGP.
     * @param sprites Receives OBP0.
     * @param sprites2 Receives OBP1.
     */
    void paletteRegisters(u8 &background, u8 &sprites, u8 &sprites2) const;

    /**
     * @brief Returns the window position registers.
     * @param windowX Receives WX.
     * @param windowY Receives WY.
     */
    void windowRegisters(u8 &windowX, u8 &windowY) const;

    /**
     * @brief Returns the DMA transfer register.
     * @return The value of DMA (0xFF46).
     */
    u8 dmaRegister() const;

    /**
     * @brief Returns the background palette RAM.
     * @return Pointer to 32 bytes of palette data.
     */
    const u8 *backgroundPaletteRam() const;

    /**
     * @brief Returns the sprite palette RAM.
     * @return Pointer to 32 bytes of palette data.
     */
    const u8 *spritePaletteRam() const;

    /**
     * @brief Returns the number of sprites drawn on the current line.
     * @return The count of sprites of the last rendered line.
     */
    int spritesOnLine() const;

    /**
     * @brief Returns the number of frames skipped because the LCD was off.
     * @return The counter of disabled frames.
     */
    std::uint64_t disabledFrames() const;

    /**
     * @brief Returns the framebuffer as a vector copy (thread safe snapshot).
     * @return 160*144*4 RGBA bytes.
     */
    std::vector<u8> snapshot() const;

    /**
     * @brief Selects the colour source of the four LCD shades.
     * @param palette The palette to use.
     */
    void setDisplayPalette(DisplayPalette palette);

    /**
     * @brief Returns the colour source of the four LCD shades.
     * @return The active palette.
     */
    DisplayPalette displayPalette() const { return m_displayPalette; }

    /**
     * @brief Replaces the four custom shades (RRGGBBAA).
     * @param colors The four colours, index 0 = lightest.
     */
    void setCustomPaletteColors(const std::array<u32, 4> &colors);

    /**
     * @brief Returns the four custom shades (RRGGBBAA).
     * @return The custom colours, index 0 = lightest.
     */
    const std::array<u32, 4> &customPaletteColors() const { return m_customPalette; }

    /**
     * @brief Returns the four shades used with the active palette.
     * @return The colours, index 0 = lightest, in RRGGBBAA order.
     */
    std::array<u32, 4> activePaletteColors() const;

private:
    /**
     * @brief Renders the scanline selected by LY into the framebuffer.
     */
    void renderLine();

    /**
     * @brief Renders the background and the window for one line.
     * @param line The scanline to render.
     */
    void renderBackground(int line);

    /**
     * @brief Renders the sprites that intersect one line.
     * @param line The scanline to render.
     */
    void renderSprites(int line);

    /**
     * @brief Draws a single background/window pixel.
     * @param line Scanline of the pixel.
     * @param x Column of the pixel.
     * @param mapX Horizontal position inside the tile map.
     * @param mapY Vertical position inside the tile map.
     * @param mapBase Address of the tile map to use.
     */
    void drawBackgroundPixel(int line, int x, int mapX, int mapY, u16 mapBase);

    /**
     * @brief Returns the color index of a pixel of a tile.
     * @param tileAddress Address of the 16 bytes of the tile.
     * @param x Column inside the tile (0..7).
     * @param y Row inside the tile (0..7).
     * @return The 2-bit color index.
     */
    u8 tilePixel(u16 tileAddress, int x, int y) const;

    /**
     * @brief Converts a color index and a palette into an RGBA pixel.
     * @param shade Color index (0..3).
     * @param palette Palette register.
     * @return The 32-bit RGBA color.
     */
    u32 shadeColor(u8 shade, u8 palette) const;

    /**
     * @brief Writes a pixel into the framebuffer.
     * @param x Column of the pixel.
     * @param y Row of the pixel.
     * @param color RGBA color.
     */
    void putPixel(int x, int y, u32 color);

    /**
     * @brief Updates the STAT interrupt line after a mode or coincidence change.
     */
    void updateStatInterrupt();

    /**
     * @brief Compares LY with LYC and sets the coincidence flag.
     */
    void updateCoincidence();

    /**
     * @brief Clears the framebuffer to the lightest shade.
     */
    void clearFrameBuffer();

    /**
     * @brief Reads one of the CGB palette RAM bytes.
     * @param address Address inside 0xFF68-0xFF6B.
     * @return The palette RAM byte.
     */
    u8 readPaletteRam(u16 address) const;

    /**
     * @brief Writes one of the CGB palette RAM bytes.
     * @param address Address inside 0xFF68-0xFF6B.
     * @param value The value to write.
     */
    void writePaletteRam(u16 address, u8 value);

    std::array<u8, kBackgroundPaletteSize> m_bgPaletteRam; /**< Background palette RAM. */
    std::array<u8, kSpritePaletteSize> m_objPaletteRam;    /**< Sprite palette RAM. */
    std::vector<u8> m_frameBuffer;                         /**< RGBA framebuffer. */
    std::array<u8, kScreenWidth> m_lineColors;             /**< Color index of the current line. */
    std::array<bool, kScreenWidth> m_linePriority;         /**< Background priority per pixel. */
    std::array<u32, 4> m_customPalette = {0xFFFFFFFF, 0xAAAAAAFF, 0x555555FF, 0x000000FF}; /**< User shades. */
    DisplayPalette m_displayPalette = DisplayPalette::Dmg; /**< Active colour source. */
    Memory *m_memory = nullptr;                            /**< Attached memory bus. */
    InterruptController *m_interrupts = nullptr;           /**< Attached interrupts. */
    u8 *m_vram = nullptr;                                  /**< Video RAM owned by the bus. */
    u8 m_lcdc = 0x91;   /**< LCD control (0xFF40). */
    u8 m_stat = 0x85;   /**< LCD status (0xFF41). */
    u8 m_scy = 0;       /**< Background scroll Y (0xFF42). */
    u8 m_scx = 0;       /**< Background scroll X (0xFF43). */
    u8 m_ly = 0;        /**< Current scanline (0xFF44). */
    u8 m_lyc = 0;       /**< Scanline comparison (0xFF45). */
    u8 m_bgp = 0xFC;    /**< Background palette (0xFF47). */
    u8 m_obp0 = 0xFF;   /**< Sprite palette 0 (0xFF48). */
    u8 m_obp1 = 0xFF;   /**< Sprite palette 1 (0xFF49). */
    u8 m_wy = 0;        /**< Window Y (0xFF4A). */
    u8 m_wx = 0;        /**< Window X (0xFF4B). */
    u8 m_dma = 0xFF;    /**< DMA register (0xFF46). */
    u8 m_bgpi = 0;      /**< Background palette index (0xFF68). */
    u8 m_obpi = 0;      /**< Sprite palette index (0xFF6A). */
    int m_dot = 0;      /**< Dot counter inside the current line. */
    int m_mode = 2;     /**< Current LCD mode. */
    int m_windowLine = 0; /**< Line of the window currently being drawn. */
    int m_spritesOnLine = 0; /**< Sprites drawn on the last line. */
    std::uint64_t m_frameCount = 0;    /**< Number of rendered frames. */
    std::uint64_t m_disabledFrames = 0; /**< Number of frames skipped with the LCD off. */
    bool m_statLine = false; /**< Previous state of the STAT interrupt line. */
};

} // namespace gb

#endif // GB_GPU_H
