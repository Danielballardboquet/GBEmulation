# Game Boy Emulator

A Game Boy (DMG/CGB) emulator written in C++ with a Qt 6 interface.

## Features

- Full Game Boy and Game Boy Color emulation
- Customizable controls and display palettes
- Save/load battery RAM
- Screenshot capture
- Debug panel for development
- Multiple theme support
- ROM library management

## Building

### Requirements

- CMake 3.16 or later
- Qt 6.0 or later
- C++17 compatible compiler

### Build Instructions

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Usage

1. Launch the emulator
2. Select a ROM from the library or open a ROM file
3. Use the configured controls to play
4. Press F11 for fullscreen mode
5. Press F12 to take a screenshot

## Controls

Default keyboard bindings:
- Arrow keys / WASD: D-pad
- Z: A button
- X: B button
- Enter: Start
- Backspace: Select
- P: Pause/Resume
- F11: Fullscreen
- Esc: Exit

Controls can be customized in Settings.

## License

MIT License - see LICENSE.txt for details
