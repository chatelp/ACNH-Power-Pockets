// ============================================================================
// ACNH Power Pockets - panel UI (SDL2 + SDL_ttf), see docs/ux/UX_SPEC.md and
// docs/design/CHARTE.md.
//
// The UI never calls the save engine directly. main.cpp hands it an
// EngineHooks struct: pointers to the working data it may edit (the 160-byte
// pocket buffer and g_vals) and lambdas wrapping the untouched engine
// (doSave, restore block, Doctor block). Every NAND write goes through those
// three hooks only (invariant I13).
// ============================================================================
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "pp_model.h"

namespace pp {

// Result of the read-only check of a pair of backup files (invariant I8).
enum BackupCheck { BC_OK, BC_MISSING, BC_SIZE, BC_READ, BC_CORRUPT, BC_NOMEM };

struct InfoData {
    // Save
    bool   saveSizeOk = false;
    long   personalSize = 0, mainSize = 0;
    int    pRegOk = 0, pRegCount = 0, mRegOk = 0, mRegCount = 0;
    bool   poisonAllZero = false;
    // SD files
    long   backupPersonal = -1, backupMain = -1;
    long   sessionPersonal = -1, sessionMain = -1;
};

struct EngineHooks {
    // ---- Working data the UI may edit (invariant I1) ----
    uint8_t*  pockets = nullptr;          // personal + POCKETS_BASE (decrypted)
    uint32_t* vals = nullptr;             // g_vals[4]

    // ---- Context ----
    const std::vector<Item>* items = nullptr;
    Favorites*  favorites = nullptr;
    StackRules* stack = nullptr;
    bool saveReady = false;               // the 4 save files were read and decrypted
    bool editSupported = false;           // invariant I9 (known save layout)
    bool sessionBackupKept = false;       // a pre-edit backup of this very save state is already on the SD card
    const char* fatalError = nullptr;     // shown instead of the panels when !saveReady
    const char* const* loadoutNames = nullptr;
    int loadoutCount = 0;
    std::string sdDir;                    // "sdmc:/switch/acnh_editor/"
    std::string version;

    // ---- Engine wrappers (defined in main.cpp, engine code untouched) ----
    std::function<EngineResult()> apply;               // sync (I2) + doSave()
    std::function<EngineResult()> restore;             // verbatim upstream restore block
    std::function<EngineResult()> doctor;              // verbatim upstream Doctor block
    std::function<BackupCheck(bool session)> checkBackup;   // read-only validation
    std::function<bool()> writeSessionBackup;          // SD only: pre-edit backup, origP/origM -> session_*.dat (kept if already there)
    std::function<bool()> sessionToBackup;             // SD only: session_* -> backup_*
    std::function<void(InfoData&)> info;               // read-only diagnostics
    std::function<std::string(const char* guide, const char* initial)> keyboard;
    std::function<void()> reloadLists;                 // favorites + nonstackable.txt
};

class Ui {
public:
    // Returns false when font.ttf can't be loaded (nothing could be drawn).
    bool init(SDL_Renderer* ren, const std::string& sdDir);
    void shutdown();
    // Draws and presents one "Reading the save" frame; call before the blocking load.
    void splash();
    // Call once after the save is loaded: takes the Base snapshot.
    void start(EngineHooks& e);
    // One frame: input, logic, render. Returns false when the app must quit.
    bool frame(uint64_t down, uint64_t held);

    struct Impl;
private:
    Impl* d_ = nullptr;
};

} // namespace pp
