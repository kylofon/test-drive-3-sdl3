# Nuked-OPL3

OPL3 (YMF262) emulator by Nuke.YKT, used by td3port for the AdLib / Sound Blaster (OPL2) music and
effects (PLAN.md decision 5). The chip starts in OPL2-compatible mode, which is all the game uses.

* Source: https://github.com/nukeykt/Nuked-OPL3, commit `765ec962e473aeb767e4cba74ffdc8f588ffbfe8`
  (fetched 2026-09-23), files `opl3.c`, `opl3.h`, `LICENSE` unmodified.
* Licence: GNU LGPL 2.1 or later (`LICENSE`). It is built as a separate static library
  (`CMakeLists.txt`, target `nuked_opl3`); the rest of the port is MIT. Releases ship this directory's
  `LICENSE` and a pointer to the source.
