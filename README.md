# ntremu
Nintendo DS Emulator. Can play many games, but some may
still be buggy. Has generally complete 2d/3d graphics, audio, and most of the hardware. Has some cool features like debugger, free camera, and DLDI
support.

<img src=images/firmware.png width="300">
<img src=images/pokemon.png width="300">
<img src=images/mariokart.png width="300">
<img src=images/linux.png width="300">

## Building

This project requires SDL2 as a dependency to build and run.
To build use `make` or `make release` to build the release version
or `make debug` for debugging symbols. I have tested on both Ubuntu and MacOS.

## Usage

You need 3 files from the DS to run the emulator: arm7 bios (bios7.bin),
arm9 bios (bios9.bin), and the firmware (firmware.bin). You can use
the `-p` argument to pass a path where these files are located, or
it will use current directory by default. You can pass the `-b` option
to boot from the firmware rather than booting a game directly.

To run a game just run the executable with the path to the ROM (.nds file) as the last command line argument, or pass `-h` to see other command line options.

The keyboard controls are as follows:

| NDS | Key |
| --- | --- |
| `A` | `Z` |
| `B` | `X` |
| `X` | `A` |
| `Y` | `S` |
| `L` | `Q` |
| `R` | `W` |
| `Dpad` | `Arrow keys` |
| `Start` | `Return` |
| `Select` | `RShift` |
| `Lid` | `Backspace` |

You can also connect a controller prior to starting the emulator.

Hotkeys are as follows:

| Control | Key |
| ------- | --- |
| Pause/Unpause | `P` |
| Mute/Unmute | `M` |
| Reset | `R` |
| Toggle speedup | `Tab` |
| Toggle wireframe | `O` |
| Toggle freecam  | `C` |

When freecam is enabled, normal keyboard input won't work
and instead you can control the freecam with
W,A,S,D,Q,E,Up,Down,Left,Right.

DLDI allows homebrew software to access files on an SD card.
On Linux you can create a FAT filesystem image with `mkfs.fat`.

## Credits

- [GBATEK](https://www.problemkaputt.de/gbatek.htm)
- [melonDS](https://melonds.kuribo64.net/)
- [EmuDev Discord Server](https://discord.gg/dkmJAes)
