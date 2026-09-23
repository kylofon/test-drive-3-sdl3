// game.h -- what a game folder holds (the car and course slots of PLAYDISK.DAT with the names from
// their .LST files), where td3port is, and starting it.
#pragma once

#include <wx/string.h>

#include <vector>

// A car or course slot of PLAYDISK.DAT whose .LST is in the folder: its base name (the code the
// game takes, e.g. "CCNSX", "SCENE02") and the name its .LST starts with ("Acura NSX",
// "Cape Cod - Niagara").
struct Slot {
    wxString code;
    wxString name;
};

struct Catalogue {
    std::vector<Slot> cars;
    std::vector<Slot> courses;
    int lastCar = -1;     // PLAYDISK.DAT's selected slots (index into the lists above), -1 unknown
    int lastCourse = -1;
    int lastSkill = 0;    // 1..9 as the game shows it, 0 unknown
};

// The cars and courses of a game folder; empty lists if it has no PLAYDISK.DAT.
Catalogue ReadCatalogue(const wxString& gameDir);

// The folder the launcher runs from.
wxString LauncherDir();

// Where things are by default: "Game" and td3port(.exe) beside the launcher.
wxString DefaultGameDir();
wxString DefaultProgram();

// Whether `dir` holds the file `name` (as written, or in upper or lower case).
bool FilePresent(const wxString& dir, const wxString& name);

// The first of the game's own files missing from `dir`, or empty if they are all there.
wxString MissingGameFile(const wxString& dir);

struct GameOptions {
    wxString program;       // td3port(.exe)
    wxString gameDir;       // --game-dir
    wxString car;           // --car: empty = the game's own last choice
    wxString course;        // --course: empty = the game's own last choice
    int skill = 0;          // --skill 1..9: 0 = the game's own last choice
    int frameTicks = 23;    // --frame-ticks: the game's speed
    bool speaker = false;   // --sound speaker (else adlib)
    int scale = 3;          // --scale: the window is 320x240 times this
    bool fullscreen = false;  // --fullscreen
};

// Starts the game. On failure returns false and says why in `error`.
bool LaunchGame(const GameOptions& options, wxString& error);
