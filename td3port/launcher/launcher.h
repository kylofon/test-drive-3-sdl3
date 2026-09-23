// launcher.h -- the launcher window: choose the game folder, the car, course and skill the game
// starts with, the game's speed and the display and sound options, then Play.
#pragma once

#include <wx/dialog.h>

#include "game.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherDialog : public wxDialog {
public:
    LauncherDialog();

private:
    // Reads the folder's cars and courses again and refills their lists, keeping the chosen ones
    // where they still exist.
    void Reload();
    // Checks the folder and the program and enables Play and the controls to match.
    void UpdateState();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void Save();

    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxChoice* car_ = nullptr;     // "Default", then the cars
    wxChoice* course_ = nullptr;  // "Default", then the courses
    wxChoice* skill_ = nullptr;   // "Default", then 1..9
    wxSpinCtrl* speed_ = nullptr;
    wxChoice* sound_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxButton* play_ = nullptr;

    Catalogue catalogue_;
    // The choices, kept while the lists are refilled; empty codes / 0 are "Default".
    wxString carCode_;
    wxString courseCode_;
    bool loading_ = true;  // no edits are recorded while the window is built
};
