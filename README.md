# Test Drive III: The Passion — SDL3 port

A faithful C reimplementation of Accolade's *Test Drive III: The Passion* (1990), running natively on SDL3,
in the same way as the [Test Drive (1987)](https://github.com/kylofon/test-drive-sdl3) and
[Test Drive II](https://github.com/kylofon/test-drive-2-sdl3) ports. It is not an emulator: every function of
the original program was reverse-engineered, documented and rewritten in C, and the port reads the original
game's files at run time. The original data is not redistributed, and you need to get it yourself.

## How to play (Windows)

You need the files of the original DOS *Test Drive III* (`TDIII.EXE`, `DATAA.DAT` … and the car and scene
files). They are not included.

1. Open the [latest release](https://github.com/kylofon/test-drive-3-sdl3/releases/latest) and download
   `td3port-…-win64.zip`.
2. Put your original game files in a folder named `Game`.
3. Open the zip. Copy all of its files into the folder that holds `Game`, so that `Test Drive III.exe` sits
   next to `Game`:

   ```text
   Test Drive III\
   ├── Game\                 <- your original game files (TDIII.EXE, DATAA.DAT, PLAYDISK.DAT, ...)
   ├── Test Drive III.exe    <- the launcher
   ├── td3port.exe           <- the game
   ├── SDL3.dll
   └── (the other files from the zip)
   ```

4. Double-click `Test Drive III.exe`, choose your car, course and options, and press **Play**.
   (`td3port.exe` also starts on its own with the defaults.)

Keep the folder somewhere you can save files, such as Documents or the Desktop, not Program Files: the game
saves its high scores (`SCENE0x.HI`) and your last choices (`PLAYDISK.DAT`) in `Game`. If Windows says
"Windows protected your PC", click **More info**, then **Run anyway**. Press Alt+Enter for full screen. The
keys are listed under [Controls](#controls-from-the-original) and in the launcher.

## Status

* The VGA version (320×200, 256 colours) with AdLib / Sound Blaster music and effects is ported and
  playable: the title and credits, all menus, the five cars, both courses (Pacific – Yosemite, Cape Cod –
  Niagara) with all their legs and routes, traffic, police, the replay, results and high scores.
  Please report any difference from the original in the issues.
* The EGA and Tandy graphics and the MT-32, Tandy and Game Blaster sound are not ported (see `PLAN.md`).

## The launcher

`Test Drive III.exe` starts the game with the options chosen in its window (`td3port/launcher/README.md`):

* **Game files**: the game folder and `td3port.exe` (by default both beside the launcher).
* **Start**: the car, course and skill level the game starts with, as if chosen in its own menus.
* **Game speed**: timer ticks per frame while driving (default 23, see below).
* **Sound**: AdLib / Sound Blaster or the PC speaker. **Window size** and **full screen**.

Settings are remembered in `%APPDATA%\Test Drive III\settings.ini`.

### Game speed

Test Drive III moves the cars, the traffic and its race clock once per drawn frame, and only waits for at
least 5 timer ticks (1/29 s) between frames. On the PCs of 1990 the 3D view took much longer to draw, so the
game ran at a handful of frames a second and was tuned for that; on anything faster (or in DOSBox at high
cycles) the whole game runs several times too fast. The port keeps the original rule but waits for **23**
ticks by default (about 6 frames a second), which plays like the game was meant to; 29 runs the race clock in
real time and 5 is the original program's limit. Choose it in the launcher or with `--frame-ticks`.

## Requirements

* Your game files in a folder (by default `Game` beside the program). The port needs `TDIII.EXE` (packed or
  unpacked), `PLAYDISK.DAT`, `DATAA.DAT`, `DATAB.DAT`, `DATAC.DAT`, `INSTR.DAT` and the car
  (`C*.DAT/.LST/.POB`) and scene (`SCENE*.DAT/.LST/.HI`) files. `TD3.CFG` is optional (VGA + AdLib without it).
* The folder must be writable (high scores, last choices).
* To build: CMake 3.24+, a C11 compiler and SDL 3; for the launcher a C++17 compiler and wxWidgets 3.2.

## Supported game version

The port supports exactly one release of the game, and refuses any other with "… is a different release of Test
Drive III (load image … bytes; this port needs 166544)" or "… its program is not the TDIII.EXE this port needs".

* Game: *Test Drive III: The Passion* (Accolade, 1990), the DOS program `TDIII.EXE` built with Microsoft C 5.1.
  The port's notes do not record a version number.
* The program as reverse-engineered: `TDIII.EXE`, 125 251 bytes, compressed with EXEPACK; unpacked, its load
  image is **166 544 bytes** (0x28A90). The port accepts the file packed or unpacked: the load image is what it
  checks.
* `TDIII.EXE` with SHA-1 `a09b0d620b91247976a7d172fd433e8c6af25926` works (reported by a user).
* Some copies name the program `TD3.EXE`; the ones reported so far are a different release (load image
  179 568 bytes) that this port cannot load. This port only looks for `TDIII.EXE`.

To see a file's hash on Windows, open a command prompt in the game folder and type
`certutil -hashfile TDIII.EXE SHA1`; on Linux or macOS, `sha1sum TDIII.EXE`. If you report a problem, give the
file's size and SHA-1.

`td3port --check --game-dir Game` says whether the program in `Game` loads, without opening a window.

The car and course lists (in the game and the launcher) come from `PLAYDISK.DAT` only: to add the cars and
courses of a bonus or scenery disk, use the game's own installer or merge tool, so that it writes them into
`PLAYDISK.DAT`; copying the files alone does not list them.

## Build

From the repository root, in Git Bash or an MSYS2 MinGW64 shell:

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S td3port -B td3port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DTD3_LAUNCHER=ON
cmake --build td3port/build
```

The build copies `SDL3.dll` and the launcher's wxWidgets DLLs next to the programs. The MSYS2 `SDL3.dll` also
needs `libiconv-2.dll` from `C:\msys64\mingw64\bin`; to run outside the MSYS2 shell, copy it there too.
Leave out `-DTD3_LAUNCHER=ON` to build only the game.

## Run

```bash
./td3port/build/td3port.exe --game-dir Game
```

| Option | Meaning |
|---|---|
| `--game-dir DIR` | Folder with the original game files (default `Game`) |
| `--scale N` | Initial window size as a multiple of 320×240 (default 3) |
| `--fullscreen` | Start in full screen |
| `--frame-ticks N` | Game speed: timer ticks per frame while driving (default 23; 5 = the original's limit) |
| `--sound adlib\|speaker` | Sound device (default: `TD3.CFG`'s, AdLib without it) |
| `--car CODE`, `--course CODE` | Start with this car / course, e.g. `CCNSX`, `SCENE02` |
| `--skill N` | Skill level 1–9 (1–3 automatic gearbox) |
| `--check` | Verify that `TDIII.EXE` loads, then exit without opening a window |

Alt+Enter toggles full screen. The window keeps the 4:3 aspect of the original monitor.

## Controls (from the original)

* Arrow keys / numeric keypad: steer, accelerate, brake. A / Z shift up / down (Enter + Up/Down: the gear
  gates); skill levels 1–3 shift automatically.
* R mirror, H headlights, W wipers, C wheel centring, M radio station.
* F1 window size, F2 detail, F3 steering sensitivity, F5 chase car view, F6 return to the road, F7 mouse
  steering, F10 instant replay, F9 pause the replay.
* Esc: leave the race or the game. Ctrl-P pause, Ctrl-S sound, Ctrl-Q music, Ctrl-E engine sound.
* A connected gamepad acts as the joystick: Ctrl-J joystick, Ctrl-K keyboard, Ctrl-A / Ctrl-D analog /
  digital. The mouse can steer (F7).

## Changes from original

* **Removed:** the copy protection (the game runs as after a correct answer) and all disk handling: Play Disk
  prompts and drive letters. Files are found in the game folder whatever their case.
* **Game speed:** frames are paced at 23 timer ticks instead of the original's minimum of 5 (see above).
  Loops the original ran at CPU speed (the main menu's turning car, the water roll) are paced to the timer.
* **Graphics:** VGA only; an EGA or Tandy `TD3.CFG` starts in VGA.
* **Sound:** AdLib through the Nuked-OPL3 emulator, or the PC speaker. An MT-32, Tandy or Game Blaster
  `TD3.CFG` falls back to AdLib (with its sound effects on).
* **Joystick and mouse:** a gamepad replaces the analogue PC joystick; the mouse driver is emulated.
* **Robustness:** a few spots where the original could read past its data or trap on a division overflow are
  guarded (the compass picture's run count, some 3D divisions). A Shift key released outside the window no
  longer sticks.
* Everything else, including the original's quirks and bugs, is kept on purpose.

## Layout

* `Game/` — your game files (not in the repository).
* `tools/` — reverse-engineering tools and data decoders (pictures, 3D objects, maps, music), see `FORMATS.md`.
* `port/` — function index, module assignment, specs (`port/spec/`), format documents (`port/formats/`)
  and the guide for writing them (`port/RE_GUIDE.md`).
* `td3port/` — the SDL3 port (`td3port/PORTING.md`: architecture and rules) and its launcher
  (`td3port/launcher/`).
* `FORMATS.md` — decoded file formats. `PLAN.md` — the plan, decisions and what is parked.

## License

The port, launcher and tools are MIT licensed (see `LICENSE`). Nuked-OPL3
(`td3port/third_party/nuked-opl3`) is LGPL 2.1 or later. *Test Drive III* is © Accolade. Its files are not
part of this repository or the releases.

## Support

https://buymeacoffee.com/krzysztofkania
