# Test Drive III (the launcher)

`Test Drive III.exe` starts `td3port`, the SDL3 port of Test Drive III: The Passion, with the options chosen
in its window. It is built with [wxWidgets](https://www.wxwidgets.org/) 3.2 from the platform's own controls,
like the Test Drive II Enhanced launcher it is modelled on.

## The window

* **Game files**
  * **Folder**: the folder with the original game's files (default `Game` beside the launcher).
  * **Program**: `td3port.exe` (default: beside the launcher).
  * The line below says how many cars and courses the folder has, or what is missing (`TDIII.EXE`,
    `PLAYDISK.DAT`, `DATAA/B/C.DAT`, `INSTR.DAT`). **Play** stays greyed out until the folder and the program
    are there.
* **Start**, as if chosen in the game's menus (which can still change them; the game remembers them in
  `PLAYDISK.DAT` like its own choices):
  * **Car** (`--car`) and **Course** (`--course`): the slots of `PLAYDISK.DAT` whose `.LST` is in the folder,
    named as their `.LST` names them. *Default* is the game's own last choice.
  * **Skill level** (`--skill`): 1-9 as the game shows it; 1-3 have an automatic gearbox.
* **Options**
  * **Game speed** (`--frame-ticks`, default 23): timer ticks (145.6 a second) per frame while driving. The
    game moves the cars and its race clock once per frame, so fewer ticks make everything faster: 5 is the
    original program's limit (what a fast PC or DOSBox at high cycles gives, far too fast), 29 runs the race
    clock in real time, 23 is what felt right in play-testing.
  * **Sound** (`--sound`): AdLib / Sound Blaster (through Nuked-OPL3) or the PC speaker. `TD3.CFG` is not
    changed.
  * **Window size** (`--scale`) and **Start in full screen** (`--fullscreen`; Alt+Enter switches).
* **Keys in the game**: a reminder of the game's keys.
* **Play** starts the game; the launcher stays open. **About**: version, author and links.

Everything is remembered in `%APPDATA%\Test Drive III\settings.ini` (`~/.config/test-drive-iii` on Linux). A
folder or program left at its default is stored empty, so it follows the launcher if the whole folder moves.
Delete the file to go back to the defaults.

## Building

Needs CMake 3.24, a C++17 compiler and wxWidgets 3.2 (MSYS2 `mingw64`: `mingw-w64-x86_64-wxwidgets3.2-msw`;
Debian and Ubuntu: `libwxgtk3.2-dev`). To build it beside `td3port.exe`, add `-DTD3_LAUNCHER=ON` when
configuring the port:

```bash
cmake -S td3port -B td3port/build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release -DTD3_LAUNCHER=ON
cmake --build td3port/build
```

It also builds on its own (`cmake -S launcher -B launcher/build -G Ninja`); then choose the program in the
window or copy the launcher next to it.

On Windows the build copies every DLL the launcher needs beside it (`copy_dlls.cmake`): the two wxWidgets DLLs
and the MSYS2 libraries they load. Keep them with the `.exe` in a release. The C++ runtime of the launcher
itself is linked in.

## Files

* `app.cpp`: the wxWidgets application.
* `launcher.h`, `launcher.cpp`: the window and the About box.
* `game.h`, `game.cpp`: reading `PLAYDISK.DAT` and the `.LST` names, the file checks and starting the game.
* `settings.h`, `settings.cpp`: `settings.ini`.
* `icon.h`, `icon.cpp`: the app icon, a banded road running to the horizon drawn in code. `make_icon.py`
  (Pillow) writes the same drawing to `app.ico` for Explorer.
* `app.rc`, `app.manifest`, `app.ico`, `version.h`: icon, visual styles, DPI awareness, version info.
* `copy_dlls.cmake`: the post-build DLL copy.
* `CMakeLists.txt`: the build, standalone or from the main project.
