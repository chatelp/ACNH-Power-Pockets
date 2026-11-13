#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ACNH Power Pockets: UI model + panel UI. The save engine below is the one of upstream
// ACNH-Save-Editor, unchanged apart from the SD folder name in its path strings
// (checked by tools/check_engine_untouched.py); see docs/engine/AUDIT_ECRITURE.md.
#include "pp_model.h"
#include "pp_ui.h"

// ================= v1.2 DUAL-FILE SAVE ENGINE =================
struct XorShift128 {
    uint32_t a, b, c, d;
    XorShift128(uint32_t seed) {
        uint32_t Mersenne = 0x6C078965;
        a = (Mersenne * (seed ^ (seed >> 30))) + 1;
        b = (Mersenne * (a   ^ (a   >> 30))) + 2;
        c = (Mersenne * (b   ^ (b   >> 30))) + 3;
        d = (Mersenne * (c   ^ (c   >> 30))) + 4;
    }
    uint32_t Next() { uint32_t t = a; a = b; b = c; c = d; t ^= t << 11; t ^= t >> 8; return d = t ^ d ^ (d >> 19); }
    uint64_t Next64() { return ((uint64_t)Next() << 32) | Next(); }
};

void GetParam(const uint32_t* data, int index, uint8_t* result, int len) {
    uint32_t seed = data[data[index] & 0x7F];
    XorShift128 rand(seed);
    uint32_t prms = data[data[index + 1] & 0x7F] & 0x7F;
    int rndRollCount = (prms & 0xF) + 1;
    for (int i = 0; i < rndRollCount; i++) rand.Next64();
    for (int i = 0; i < len; i++) result[i] = (uint8_t)(rand.Next() >> 24);
}

static const uint8_t sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
static uint8_t xtime(uint8_t x) { return (x << 1) ^ (((x >> 7) & 1) * 0x1b); }
static void KeyExpansion(const uint8_t* key, uint8_t* rk) {
    for (int i = 0; i < 16; i++) rk[i] = key[i];
    for (int i = 4; i < 44; i++) {
        uint8_t t[4];
        for (int j = 0; j < 4; j++) t[j] = rk[(i-1)*4+j];
        if (i % 4 == 0) { uint8_t q = t[0]; t[0] = sbox[t[1]] ^ rcon[i/4]; t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[q]; }
        for (int j = 0; j < 4; j++) rk[i*4+j] = rk[(i-4)*4+j] ^ t[j];
    }
}
static void AesEncryptBlock(const uint8_t* rk, const uint8_t* in, uint8_t* out) {
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i];
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];
    for (int r = 1; r < 10; r++) {
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
        uint8_t t;
        t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
        t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
        t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
        for (int c = 0; c < 4; c++) {
            uint8_t a=s[c*4], b=s[c*4+1], d=s[c*4+2], e=s[c*4+3], x=a^b^d^e;
            s[c*4]   ^= x ^ xtime(a^b); s[c*4+1] ^= x ^ xtime(b^d);
            s[c*4+2] ^= x ^ xtime(d^e); s[c*4+3] ^= x ^ xtime(e^a);
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[r*16+i];
    }
    for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
    { uint8_t t; t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
      t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
      t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t; }
    for (int i = 0; i < 16; i++) out[i] = s[i] ^ rk[160+i];
}

void CryptACNH(const uint8_t* header, uint8_t* data, size_t dataLen) {
    const uint32_t* imp = (const uint32_t*)(header + 0x100);
    uint8_t key[16], ctr[16];
    GetParam(imp, 0, key, 16); GetParam(imp, 2, ctr, 16);
    uint8_t rk[176]; KeyExpansion(key, rk);
    uint8_t ek[16];
    for (size_t off = 0; off < dataLen; off += 16) {
        AesEncryptBlock(rk, ctr, ek);
        size_t bl = 16; if (off + bl > dataLen) bl = dataLen - off;
        for (size_t i = 0; i < bl; i++) data[off+i] ^= ek[i];
        for (int i = 15; i >= 0; i--) { if (++ctr[i] != 0) break; }
    }
}

static uint32_t Scramble(uint32_t v) { v = (v * 0x16A88000) | ((v * 0xCC9E2D51) >> 17); v *= 0x1B873593; return v; }
static uint32_t Advance(uint32_t c, uint32_t v) { c ^= Scramble(v); c = (c >> 19) | (c << 13); c = (c * 5) + 0xE6546B64; return c; }
static uint32_t Finalize(uint32_t c, uint32_t len) { c ^= len; c ^= c >> 16; c *= 0x85EBCA6B; c ^= c >> 13; c *= 0xC2B2AE35; c ^= c >> 16; return c; }
static uint32_t Murmur3Hash(const uint8_t* data, size_t len) {
    uint32_t c = 0; const uint32_t* u32 = (const uint32_t*)data;
    for (size_t i = 0; i < len / 4; i++) c = Advance(c, u32[i]);
    return Finalize(c, (uint32_t)len);
}
static int GetLength(const uint8_t* data, size_t maxLen, uint32_t expect) {
    uint32_t c = 0; const uint32_t* u32 = (const uint32_t*)data;
    for (size_t i = 0; i < maxLen / 4; i++) {
        c = Advance(c, u32[i]);
        uint32_t l = (uint32_t)((i + 1) * 4);
        if (Finalize(c, l) == expect) return (int)l;
    }
    return -1;
}

static uint32_t LE32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static void WriteLE32(uint8_t* p, uint32_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF; }
static uint16_t LE16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static void WriteLE16(uint8_t* p, uint16_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; }
static uint8_t CalcChecksum(uint32_t v) { uint32_t s = v + (v>>16) + (v>>24) + (v>>8); return (uint8_t)(s - 0x2D); }
static uint32_t rotr32(uint32_t x, int r) { r &= 31; return (x >> r) | (x << ((32 - r) & 31)); }
static uint32_t rotl32(uint32_t x, int r) { r &= 31; return (x << r) | (x >> ((32 - r) & 31)); }
static uint32_t DecInt(uint32_t enc, uint8_t shift, uint16_t adjust) { return rotr32(enc, shift + 3) + 0x80E32B11u - adjust; }
static uint32_t EncInt(uint32_t value, uint8_t shift, uint16_t adjust) { return rotl32(value + adjust - 0x80E32B11u, shift + 3); }

struct Region { uint32_t off; uint32_t len; };
const uint32_t WALLET_OFF = 0x37D60;
const uint32_t BANK_OFF   = 0x6527C;
const uint32_t MILES_OFF  = 0x12718;
const uint32_t LOAN_OFF   = 0x33CBF0;
const uint32_t POCKETS_BASE = 0x37CA8;

// ================= Built-in favorites (resolved by name at boot, A-Z) =================
// Power Pockets keeps this list as the base and overlays user_favorites.txt on top
// (see pp::Favorites). Unresolved names are hidden instead of showing ID FFFE.
static const char* FAV_NAMES[] = {
    "Nook Miles Ticket",
    "Bell bag",
    "Bell voucher",
    "99,000 Bells",
    "gold roses",
    "cherry-blossom petal",
    "star fragment",
    "large star fragment",
    "star wand",
    "gold nugget",
    "iron nugget",
    "pearl",
    "gold bars",
    "customization kit",
    "golden axe",
    "golden net",
    "golden rod",
    "golden shovel",
    "golden slingshot",
    "golden watering can",
    "crown (No Variations)",
    "royal crown (No Variations)",
    // Fruits
    "apple",
    "cherry",
    "orange",
    "peach",
    "pear",
    "coconut",
    // Vegetables
    "potato",
    "carrot",
    "wheat",
    "sugarcane",
    "tomato",
    "orange pumpkin",
    // Common crafting materials (iron and gold nuggets are above)
    "wood",
    "softwood",
    "hardwood",
    "stone",
    "clay",
    "tree branch",
    "bamboo piece",
    // Useful items and tools
    "fish bait",
    "medicine",
    "pitfall seed",
    "white wrapping paper",
    "sapling",
    "ladder",
    "vaulting pole",
    "fossil",
    // Seasonal materials
    "young spring bamboo",
    "pine cone",
    "acorn",
    "maple leaf",
    "summer shell",
    "snowflake",
    "large snowflake",
    "red ornament",
    "heart crystal",
    // Rare and fun
    "golden toilet",
    "golden seat",
    "golden casket",
    "robot hero",
    "Nintendo Switch",
    "wobbling Zipper toy",
    "coelacanth",
    "stringfish",
    "golden trout",
    "barreleye",
    "golden stag"
};
static const int FAV_MAX = (int)(sizeof(FAV_NAMES) / sizeof(FAV_NAMES[0]));

static const char* LOADOUT_NAMES[] = {"Mining", "Fishing", "Terraforming", "Custom 1", "Custom 2"};
static const int LOADOUT_COUNT = 5;

static void applyEdit(uint8_t* p, uint32_t off, uint32_t value) {
    uint16_t adjust = (uint16_t)(p[off+4] | (p[off+5] << 8));
    uint8_t shift = p[off+6];
    uint32_t enc = EncInt(value, shift, adjust);
    WriteLE32(p + off, enc);
    p[off+7] = CalcChecksum(enc);
}

static uint8_t* readSaveFile(FsFileSystem* fs, const char* path, size_t& outSize) {
    FsFile f; if (R_FAILED(fsFsOpenFile(fs, path, FsOpenMode_Read, &f))) return nullptr;
    s64 sz = 0; fsFileGetSize(&f, &sz);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (buf) { u64 rd = 0; fsFileRead(&f, 0, buf, (u64)sz, FsReadOption_None, &rd); }
    fsFileClose(&f); outSize = (size_t)sz; return buf;
}

static char bakMsg[128];
static bool backupToSd(const uint8_t* pers, size_t pSz, const uint8_t* main, size_t mSz) {
    FsFileSystem sd; Result rc = fsOpenSdCardFileSystem(&sd);
    if (R_FAILED(rc)) { snprintf(bakMsg, sizeof(bakMsg), "BAK sd open 0x%X", rc); return false; }
    auto writeOne = [&](const char* p, const uint8_t* d, size_t s) -> bool {
        fsFsDeleteFile(&sd, p);
        if (R_FAILED(fsFsCreateFile(&sd, p, s, 0))) return false;
        FsFile f; if (R_FAILED(fsFsOpenFile(&sd, p, FsOpenMode_Write, &f))) return false;
        Result wr = fsFileWrite(&f, 0, d, s, FsWriteOption_Flush);
        fsFileClose(&f); return R_SUCCEEDED(wr);
    };
    bool ok = writeOne("/switch/acnh_power_pockets/backup_personal.dat", pers, pSz) &&
              writeOne("/switch/acnh_power_pockets/backup_main.dat", main, mSz);
    fsFsClose(&sd);
    if (!ok) snprintf(bakMsg, sizeof(bakMsg), "BAK write failed!");
    return ok;
}

// ================= Item database (UI) =================
using pp::Item;
static std::vector<Item> g_allItems;

// Engine status buffer: doSave / restore / Doctor report their outcome here.
// The UI parses it (pp::parse*Status) instead of displaying it raw.
static char g_status_buf[128] = "Ready. ZL restores backup. (+) Search, (A) Save/Quit";

// ================= NEW: Native Switch Keyboard Wrapper =================
static std::string showKeyboard(const char* guideText, const char* initialText) {
    SwkbdConfig kbd;
    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, guideText);
        if (initialText && initialText[0] != '\0') {
            swkbdConfigSetInitialText(&kbd, initialText);
        }
        char outStr[256] = {0};
        if (R_SUCCEEDED(swkbdShow(&kbd, outStr, sizeof(outStr)))) {
            swkbdClose(&kbd);
            return std::string(outStr);
        }
        swkbdClose(&kbd);
    }
    return initialText ? std::string(initialText) : "";
}

// ================= Engine state shared with the UI =================
static SDL_Renderer* g_ren = nullptr;
// g_vals: working money values. g_slot/g_newId/g_newCount: written by doSave()
// into one slot; pp::syncSaveSlotVars() makes that write a no-op (invariant I2).
static uint32_t g_vals[4] = { 0, 0, 0, 0 };
static int g_slot = 1;
static int32_t g_newId = 0, g_newCount = 1;

int main(int argc, char** argv) {
    // --- APPLET MODE GUARD ---
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState guard_pad;
    padInitializeDefault(&guard_pad);

    if (appletGetAppletType() != AppletType_Application) {
        consoleClear();
        printf("\x1b[31m\n\n  !! ACNH POWER POCKETS - LAUNCH ERROR !!\x1b[0m\n\n");
        printf("  You launched from the Album (Applet Mode).\n");
        printf("  This app needs full RAM + SD access to work safely.\n\n");
        printf("  HOW TO FIX:\n");
        printf("  1. Return to the Home Menu.\n");
        printf("  2. Highlight Animal Crossing: New Horizons.\n");
        printf("  3. HOLD [R] while pressing [A] to launch it.\n");
        printf("  4. The Homebrew Menu will open. Launch the editor from there.\n\n");
        printf("  Press [A] to exit.\n");
        consoleUpdate(NULL);
        
        while (appletMainLoop()) {
            padUpdate(&guard_pad);
            if (padGetButtonsDown(&guard_pad) & HidNpadButton_A) break;
            consoleUpdate(NULL);
            svcSleepThread(16000000);
        }
        consoleExit(NULL);
        return 0;
    }

    // --- Power Pockets: missing font guard (upstream showed a blank green screen) ---
    if (!pp::fileExists("sdmc:/switch/acnh_power_pockets/font.ttf")) {
        consoleClear();
        printf("\x1b[31m\n\n  !! ACNH POWER POCKETS - MISSING FILE !!\x1b[0m\n\n");
        printf("  font.ttf was not found in sdmc:/switch/acnh_power_pockets/\n");
        printf("  Copy font.ttf (and items.txt) from the release zip into that folder.\n\n");
        printf("  Nothing was read or written. Press [A] to exit.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&guard_pad);
            if (padGetButtonsDown(&guard_pad) & HidNpadButton_A) break;
            consoleUpdate(NULL);
            svcSleepThread(16000000);
        }
        consoleExit(NULL);
        return 0;
    }
    consoleExit(NULL);
    // -------------------------

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("ACNH Power Pockets", 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN);
    g_ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    TTF_Init();
    const std::string sdDir = "sdmc:/switch/acnh_power_pockets/";
    pp::Ui ui;
    if (!ui.init(g_ren, sdDir)) {
        // font.ttf is there but can't be loaded: every screen would be blank while the
        // buttons still work. Stop here, before the save is opened.
        ui.shutdown();
        TTF_Quit();
        SDL_DestroyRenderer(g_ren); SDL_DestroyWindow(win); SDL_Quit();
        consoleInit(NULL);
        printf("\x1b[31m\n\n  !! ACNH POWER POCKETS - UNREADABLE FILE !!\x1b[0m\n\n");
        printf("  font.ttf in sdmc:/switch/acnh_power_pockets/ can't be loaded (damaged copy?).\n");
        printf("  Copy font.ttf again from the release zip.\n\n");
        printf("  Nothing was read or written. Press [A] to exit.\n");
        consoleUpdate(NULL);
        while (appletMainLoop()) {
            padUpdate(&guard_pad);
            if (padGetButtonsDown(&guard_pad) & HidNpadButton_A) break;
            consoleUpdate(NULL);
            svcSleepThread(16000000);
        }
        consoleExit(NULL);
        return 0;
    }

    // Everything below runs before the first frame: the item and picture rules, then the
    // engine reading, decrypting and scanning both save files. That takes seconds, or
    // minutes when a region hash is stale (after an edit, until the game saves again):
    // show something instead of a black screen.
    ui.splash();

    // items.txt: one name per line, the item ID is the line index (unchanged format).
    FILE* it = fopen("sdmc:/switch/acnh_power_pockets/items.txt", "rb");
    if (it) {
        char line[256];
        uint16_t id = 0;
        while (fgets(line, sizeof(line), it)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[len-1] = '\0';
                len--;
            }
            g_allItems.push_back({id, std::string(line)});
            id++;
        }
        fclose(it);
    }
    pp::Favorites favorites;
    pp::StackRules stackRules;
    pp::GlyphMap glyphMap;                // item pictures, and which kinds of items never stack
    pp::StackLimits stackLimits;          // largest stack the game allows, per item
    auto reloadLists = [&]() {
        favorites.load(g_allItems, FAV_NAMES, FAV_MAX, sdDir + "user_favorites.txt");
        glyphMap.build(g_allItems, (sdDir + "item_glyphs.txt").c_str());       // before the 1/slot rules: they use its item kinds
        stackLimits.load((sdDir + "item_stacks.txt").c_str(), g_allItems.size());
        stackRules.build(g_allItems, (sdDir + "nonstackable.txt").c_str(), &glyphMap, &stackLimits);
    };
    reloadLists();

    FsFileSystem fs; FsSaveDataAttribute attr; memset(&attr, 0, sizeof(attr));
    attr.application_id = 0x01006F8002326000ull; attr.save_data_type = 3;
    Result rc = fsOpenSaveDataFileSystem(&fs, FsSaveDataSpaceId_User, &attr);

    size_t pSz=0, pHdrSz=0, mSz=0, mHdrSz=0;
    uint8_t *personal=nullptr, *pHdr=nullptr, *origP=nullptr;
    uint8_t *mainData=nullptr, *mHdr=nullptr, *origM=nullptr;

    if (R_SUCCEEDED(rc)) {
        personal = readSaveFile(&fs, "/Villager0/personal.dat", pSz);
        pHdr = readSaveFile(&fs, "/Villager0/personalHeader.dat", pHdrSz);
        mainData = readSaveFile(&fs, "/main.dat", mSz);
        mHdr = readSaveFile(&fs, "/mainHeader.dat", mHdrSz);
        if (personal && mainData) {
            origP = (uint8_t*)malloc(pSz); if(origP) memcpy(origP, personal, pSz);
            origM = (uint8_t*)malloc(mSz); if(origM) memcpy(origM, mainData, mSz);
        }
    } else { snprintf(g_status_buf, sizeof(g_status_buf), "Mount failed! Close ACNH fully first."); }

    Region pReg[16], mReg[32]; int pRegC=0, mRegC=0;

    if (personal && pHdr && mainData && mHdr) {
        CryptACNH(pHdr, personal, pSz); CryptACNH(mHdr, mainData, mSz);

        auto findRegions = [&](uint8_t* buf, size_t sz, Region* arr, int& cnt, int maxR) {
            size_t off = 0x100;
            while (off + 4 < sz && cnt < maxR) {
                uint32_t hash = LE32(buf + off);
                int len = GetLength(buf + off + 4, sz - off - 4, hash);
                if (len == -1) { off += 0x10; continue; }
                arr[cnt].off = (uint32_t)off; arr[cnt].len = (uint32_t)len; cnt++; off += 4 + len;
            }
        };
        findRegions(personal, pSz, pReg, pRegC, 16);
        findRegions(mainData, mSz, mReg, mRegC, 32);

        // ---- SAVE DOCTOR v1.4.2: region layout memory ----
        auto saveLayout = [&](const char* path, Region* arr, int cnt) {
            FILE* f = fopen(path, "wb"); if (!f) return;
            fwrite(&cnt, 4, 1, f);
            for (int i = 0; i < cnt; i++) { fwrite(&arr[i].off, 4, 1, f); fwrite(&arr[i].len, 4, 1, f); }
            fclose(f);
        };
        auto loadLayout = [&](const char* path, Region* arr, int& cnt, int maxR, size_t sz) {
            FILE* f = fopen(path, "rb"); if (!f) return;
            int n = 0; if (fread(&n, 4, 1, f) != 1) n = 0;
            if (n < 1 || n > maxR) { fclose(f); return; }
            Region tmp[32]; bool ok = true;
            for (int i = 0; i < n; i++) {
                if (fread(&tmp[i].off, 4, 1, f) != 1 || fread(&tmp[i].len, 4, 1, f) != 1) { ok = false; break; }
                if (tmp[i].off + 4 + tmp[i].len > sz) ok = false;
            }
            if (ok) { for (int i = 0; i < n; i++) arr[i] = tmp[i]; cnt = n; }
            fclose(f);
        };
        { // ---- v1.4.3: pick the BEST layout, never pollute the cache ----
            static Region cacheP[16]; static Region cacheM[32];
            int cachePC = 0, cacheMC = 0;
            loadLayout("sdmc:/switch/acnh_power_pockets/layout_personal.dat", cacheP, cachePC, 16, pSz);
            loadLayout("sdmc:/switch/acnh_power_pockets/layout_main.dat", cacheM, cacheMC, 32, mSz);
            if (pRegC > 0 && pRegC >= cachePC) {
                saveLayout("sdmc:/switch/acnh_power_pockets/layout_personal.dat", pReg, pRegC);
            } else if (cachePC > 0) {
                for (int i = 0; i < cachePC; i++) pReg[i] = cacheP[i];
                pRegC = cachePC;
            }
            if (mRegC > 0 && mRegC >= cacheMC) {
                saveLayout("sdmc:/switch/acnh_power_pockets/layout_main.dat", mReg, mRegC);
            } else if (cacheMC > 0) {
                for (int i = 0; i < cacheMC; i++) mReg[i] = cacheM[i];
                mRegC = cacheMC;
            }
        }
        // ---- end layout memory ----

        g_vals[0] = DecInt(LE32(personal + WALLET_OFF), personal[WALLET_OFF+6], LE16(personal + WALLET_OFF + 4));
        g_vals[1] = DecInt(LE32(personal + BANK_OFF), personal[BANK_OFF+6], LE16(personal + BANK_OFF + 4));
        g_vals[2] = DecInt(LE32(personal + MILES_OFF), personal[MILES_OFF+6], LE16(personal + MILES_OFF + 4));
        g_vals[3] = DecInt(LE32(mainData + LOAN_OFF), mainData[LOAN_OFF+6], LE16(mainData + LOAN_OFF + 4));

        const uint8_t* rec = personal + POCKETS_BASE;
        g_newId = LE16(rec); g_newCount = LE16(rec + 4);
    } else if (R_SUCCEEDED(rc)) { snprintf(g_status_buf, sizeof(g_status_buf), "FAILED to read NAND files!"); }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);

    auto doSave = [&]() {
        if (!backupToSd(origP, pSz, origM, mSz)) { snprintf(g_status_buf, sizeof(g_status_buf), "%s", bakMsg); }
        else {
            uint8_t* outP = (uint8_t*)malloc(pSz);
            uint8_t* outM = (uint8_t*)malloc(mSz);
            if (outP && outM) {
                memcpy(outP, personal, pSz); memcpy(outM, mainData, mSz);
                applyEdit(outP, WALLET_OFF, g_vals[0]);
                applyEdit(outP, BANK_OFF, g_vals[1]);
                applyEdit(outP, MILES_OFF, g_vals[2]);
                applyEdit(outM, LOAN_OFF, g_vals[3]);

                // --- HEAL PRE-EXISTING CORRUPTION ---
                // The old buggy app poisoned 8 per-villager fields in main.dat.
                // We must explicitly zero them out to heal the save.
                for (int i = 0; i < 8; i++) {
                    uint32_t poison_off = 0x5B3D50 + (i * 0x74A40);
                    WriteLE32(outM + poison_off, 0x00000000);
                }

                uint8_t* r = outP + POCKETS_BASE + (g_slot - 1) * 8;
                WriteLE16(r, (uint16_t)g_newId); WriteLE16(r + 4, (uint16_t)g_newCount);

                CryptACNH(pHdr, outP, pSz); CryptACNH(mHdr, outM, mSz);

                auto writeNand = [&](const char* path, uint8_t* buf, size_t sz) -> bool {
                    FsFile f; if (R_FAILED(fsFsOpenFile(&fs, path, FsOpenMode_Write, &f))) return false;
                    Result w2 = fsFileWrite(&f, 0, buf, sz, FsWriteOption_Flush); fsFileClose(&f);
                    return R_SUCCEEDED(w2);
                };

                if (writeNand("/Villager0/personal.dat", outP, pSz) && writeNand("/main.dat", outM, mSz)) {
                    fsFsCommit(&fs);
                    memcpy(origP, outP, pSz); memcpy(personal, outP, pSz); CryptACNH(pHdr, personal, pSz);
                    memcpy(origM, outM, mSz); memcpy(mainData, outM, mSz); CryptACNH(mHdr, mainData, mSz);
                    uint8_t* pr = personal + POCKETS_BASE + (g_slot - 1) * 8;
                    WriteLE16(pr, (uint16_t)g_newId); WriteLE16(pr + 4, (uint16_t)g_newCount);
                    snprintf(g_status_buf, sizeof(g_status_buf), "SAVED personal + main to NAND!");
                } else { snprintf(g_status_buf, sizeof(g_status_buf), "NAND write failed!"); }
            } else { snprintf(g_status_buf, sizeof(g_status_buf), "malloc failed!"); }
            if (outP) free(outP);
            if (outM) free(outM);
        }
    };

    // ======================================================================
    // Power Pockets: the two blocks below are MOVED VERBATIM from the upstream
    // input loop (ZL handler, and the second ZR press of the Save Doctor).
    // Not a single instruction was changed; the original indentation is kept
    // on purpose so `git diff --color-moved` shows a pure move. Only the trigger
    // changed: System panel + danger dialog instead of a raw trigger press.
    // ======================================================================
    auto doRestore = [&]() {
                    FILE* fp = fopen("sdmc:/switch/acnh_power_pockets/backup_personal.dat", "rb");
                    FILE* fm = fopen("sdmc:/switch/acnh_power_pockets/backup_main.dat", "rb");
                    if (!fp || !fm) {
                        snprintf(g_status_buf, sizeof(g_status_buf), "Restore failed: backup files missing!");
                    } else {
                        fseek(fp, 0, SEEK_END); long psz2 = ftell(fp); fseek(fp, 0, SEEK_SET);
                        fseek(fm, 0, SEEK_END); long msz2 = ftell(fm); fseek(fm, 0, SEEK_SET);
                        if ((size_t)psz2 != pSz || (size_t)msz2 != mSz) {
                            snprintf(g_status_buf, sizeof(g_status_buf), "Restore failed: backup size mismatch!");
                        } else {
                            uint8_t* bp = (uint8_t*)malloc(pSz);
                            uint8_t* bm = (uint8_t*)malloc(mSz);
                            bool okRead = bp && bm &&
                                fread(bp, 1, pSz, fp) == pSz &&
                                fread(bm, 1, mSz, fm) == mSz;
                            if (!okRead) {
                                snprintf(g_status_buf, sizeof(g_status_buf), "Restore failed: backup read error!");
                            } else {
                                auto writeNand2 = [&](const char* path, uint8_t* buf, size_t sz) -> bool {
                                    FsFile f; if (R_FAILED(fsFsOpenFile(&fs, path, FsOpenMode_Write, &f))) return false;
                                    Result w2 = fsFileWrite(&f, 0, buf, sz, FsWriteOption_Flush); fsFileClose(&f);
                                    return R_SUCCEEDED(w2);
                                };
                                if (writeNand2("/Villager0/personal.dat", bp, pSz) && writeNand2("/main.dat", bm, mSz)) {
                                    fsFsCommit(&fs);
                                    memcpy(origP, bp, pSz);
                                    memcpy(origM, bm, mSz);
                                    memcpy(personal, bp, pSz);
                                    memcpy(mainData, bm, mSz);
                                    CryptACNH(pHdr, personal, pSz);
                                    CryptACNH(mHdr, mainData, mSz);
                                    g_vals[0] = DecInt(LE32(personal + WALLET_OFF), personal[WALLET_OFF+6], LE16(personal + WALLET_OFF + 4));
                                    g_vals[1] = DecInt(LE32(personal + BANK_OFF), personal[BANK_OFF+6], LE16(personal + BANK_OFF + 4));
                                    g_vals[2] = DecInt(LE32(personal + MILES_OFF), personal[MILES_OFF+6], LE16(personal + MILES_OFF + 4));
                                    g_vals[3] = DecInt(LE32(mainData + LOAN_OFF), mainData[LOAN_OFF+6], LE16(mainData + LOAN_OFF + 4));
                                    const uint8_t* rec2 = personal + POCKETS_BASE + (g_slot - 1) * 8;
                                    g_newId = LE16(rec2); g_newCount = LE16(rec2 + 4);
                                    snprintf(g_status_buf, sizeof(g_status_buf), "RESTORED from SD backup!");
                                } else {
                                    snprintf(g_status_buf, sizeof(g_status_buf), "Restore failed: NAND write failed!");
                                }
                            }
                            if (bp) free(bp);
                            if (bm) free(bm);
                        }
                    }
                    if (fp) fclose(fp);
                    if (fm) fclose(fm);
    };

    auto doDoctor = [&]() {
                        // 1) FORENSIC COPY of damaged encrypted state (never touches good backups)
                        FsFileSystem sd2; bool forensics = false;
                        if (R_SUCCEEDED(fsOpenSdCardFileSystem(&sd2))) {
                            auto wrFile = [&](const char* p, const uint8_t* d, size_t s) -> bool {
                                fsFsDeleteFile(&sd2, p);
                                if (R_FAILED(fsFsCreateFile(&sd2, p, s, 0))) return false;
                                FsFile f; if (R_FAILED(fsFsOpenFile(&sd2, p, FsOpenMode_Write, &f))) return false;
                                Result w = fsFileWrite(&f, 0, d, s, FsWriteOption_Flush);
                                fsFileClose(&f); return R_SUCCEEDED(w);
                            };
                            forensics = wrFile("/switch/acnh_power_pockets/damaged_personal.dat", origP, pSz) &&
                                        wrFile("/switch/acnh_power_pockets/damaged_main.dat", origM, mSz);
                            fsFsClose(&sd2);
                        }
                        // 2) Re-heal all detected regions
                        
                        // 4) Re-apply EncryptedInt32 checksums on known fields
                        applyEdit(personal, WALLET_OFF, g_vals[0]);
                        applyEdit(personal, BANK_OFF, g_vals[1]);
                        applyEdit(personal, MILES_OFF, g_vals[2]);
                        applyEdit(mainData, LOAN_OFF, g_vals[3]);
                        // 5) Encrypt -> snapshot -> write NAND -> decrypt for session consistency
                        CryptACNH(pHdr, personal, pSz); CryptACNH(mHdr, mainData, mSz);
                        memcpy(origP, personal, pSz); memcpy(origM, mainData, mSz);
                        auto writeNandR = [&](const char* path, uint8_t* buf, size_t sz) -> bool {
                            FsFile f; if (R_FAILED(fsFsOpenFile(&fs, path, FsOpenMode_Write, &f))) return false;
                            Result w = fsFileWrite(&f, 0, buf, sz, FsWriteOption_Flush); fsFileClose(&f);
                            return R_SUCCEEDED(w);
                        };
                        bool ok = writeNandR("/Villager0/personal.dat", personal, pSz) && writeNandR("/main.dat", mainData, mSz);
                        if (ok) fsFsCommit(&fs);
                        CryptACNH(pHdr, personal, pSz); CryptACNH(mHdr, mainData, mSz);
                        if (ok) snprintf(g_status_buf, sizeof(g_status_buf), forensics ? "REPAIRED! Quit fully, then try the game." : "REPAIRED (no forensic copy) - quit & try game");
                        else snprintf(g_status_buf, sizeof(g_status_buf), "Repair NAND write failed!");
    };

    // ======================================================================
    // UI hooks. They only call the engine blocks above, unchanged.
    // ======================================================================
    const bool filesRead = personal && pHdr && mainData && mHdr;
    // The UI reads the 20 pocket records even in read-only mode: they must be inside the file.
    const bool saveReady = filesRead && origP && origM && pSz >= POCKETS_BASE + pp::POCKETS_BYTES;
    // Invariant I9: offsets above are those of save revision 3.0.x (NHSE
    // PersonalOffsets30 / MainSaveOffsets30). Other sizes => read-only.
    const bool editSupported = saveReady && pSz == 0x74A40 && mSz == 0x9B0E90;

    // ---- Integrity hashes (Power Pockets) --------------------------------------------
    // The game stores a Murmur3 hash in front of each region of personal.dat / main.dat and
    // rejects the whole save ("the save data is corrupted") when one of them is stale.
    // Upstream's last release rewrote them for regions it guessed by scanning the files;
    // its unreleased HEAD, which this fork started from, dropped the step altogether.
    // Seen on a real 3.0.3 save: an Apply that changed nothing but pocket records left one
    // stale hash, and the game refused the save.
    //
    // Fixed region table of the 3.0.x layout: hash at `off`, computed over the next `len`
    // bytes. It is only used when both files have the 3.0.x sizes (editSupported), and it
    // was checked against a real game-written save (21 of 21 hashes match).
    static const pp::RegionRef HASH_P[] = { { 0x00110, 0x37ACC }, { 0x37BE0, 0x3CE5C } };
    static const pp::RegionRef HASH_M[] = {
        { 0x000110, 0x1E339C }, { 0x1E34B0, 0x3D089C }, { 0x5B3E60, 0x037ACC }, { 0x5EB930, 0x03CE5C },
        { 0x6288A0, 0x037ACC }, { 0x660370, 0x03CE5C }, { 0x69D2E0, 0x037ACC }, { 0x6D4DB0, 0x03CE5C },
        { 0x711D20, 0x037ACC }, { 0x7497F0, 0x03CE5C }, { 0x786760, 0x037ACC }, { 0x7BE230, 0x03CE5C },
        { 0x7FB1A0, 0x037ACC }, { 0x832C70, 0x03CE5C }, { 0x86FBE0, 0x037ACC }, { 0x8A76B0, 0x03CE5C },
        { 0x8E4620, 0x037ACC }, { 0x91C0F0, 0x03CE5C }, { 0x958F50, 0x057F3C } };
    const int HASH_PN = (int)(sizeof(HASH_P) / sizeof(HASH_P[0])), HASH_MN = (int)(sizeof(HASH_M) / sizeof(HASH_M[0]));
    auto hashesValid = [&](const uint8_t* p, const uint8_t* m) -> bool {
        return editSupported && pp::regionsHashOk(p, pSz, HASH_P, HASH_PN, Murmur3Hash) == HASH_PN &&
               pp::regionsHashOk(m, mSz, HASH_M, HASH_MN, Murmur3Hash) == HASH_MN;
    };
    // Hash status of the save ON THE CONSOLE, for the UI. Taken only at moments when the
    // working buffers equal the console state (after loading, after a successful write):
    // while pocket edits are pending the buffers legitimately disagree with their hashes.
    int conPOk = 0, conPN = 0, conMOk = 0, conMN = 0;
    auto noteConsoleHashes = [&]() {
        if (!saveReady) return;
        if (editSupported) {                                  // known layout: the fixed table is the reference
            conPN = HASH_PN; conMN = HASH_MN;
            conPOk = pp::regionsHashOk(personal, pSz, HASH_P, HASH_PN, Murmur3Hash);
            conMOk = pp::regionsHashOk(mainData, mSz, HASH_M, HASH_MN, Murmur3Hash);
        } else {                                              // unknown layout: regions found by the engine's scan
            std::vector<pp::RegionRef> r;
            for (int i = 0; i < pRegC; i++) r.push_back({ pReg[i].off, pReg[i].len });
            conPN = pRegC; conPOk = pp::regionsHashOk(personal, pSz, r.data(), (int)r.size(), Murmur3Hash);
            r.clear();
            for (int i = 0; i < mRegC; i++) r.push_back({ mReg[i].off, mReg[i].len });
            conMN = mRegC; conMOk = pp::regionsHashOk(mainData, mSz, r.data(), (int)r.size(), Murmur3Hash);
        }
    };
    noteConsoleHashes();
    // Brings the working buffers to exactly the bytes the engine is about to write, then makes
    // every region hash match them. The engine applies the same four values, zeroes the same
    // eight fields and (after syncSaveSlotVars) writes the same slot record on its own copies:
    // all of that rewrites identical bytes, so the hashes computed here are the hashes of what
    // reaches the console. RAM only; nothing is written by this function.
    auto finalizeBuffers = [&](bool zeroFieldsLikeDoSave) {
        applyEdit(personal, WALLET_OFF, g_vals[0]);
        applyEdit(personal, BANK_OFF, g_vals[1]);
        applyEdit(personal, MILES_OFF, g_vals[2]);
        applyEdit(mainData, LOAN_OFF, g_vals[3]);
        // doSave() zeroes these 8 fields on every write; the repair pass leaves them alone.
        if (zeroFieldsLikeDoSave) for (int i = 0; i < 8; i++) WriteLE32(mainData + 0x5B3D50 + (i * 0x74A40), 0x00000000);
        for (int i = 0; i < HASH_PN; i++) WriteLE32(personal + HASH_P[i].off, Murmur3Hash(personal + HASH_P[i].off + 4, HASH_P[i].len));
        for (int i = 0; i < HASH_MN; i++) WriteLE32(mainData + HASH_M[i].off, Murmur3Hash(mainData + HASH_M[i].off + 4, HASH_M[i].len));
    };
    static char fatalBuf[128];
    snprintf(fatalBuf, sizeof fatalBuf, "%s", g_status_buf);
    // The engine only reports a failed mount or a failed read; name the other causes.
    if (filesRead && !(origP && origM)) snprintf(fatalBuf, sizeof fatalBuf, "Not enough memory to load the save.");
    else if (filesRead && !saveReady) snprintf(fatalBuf, sizeof fatalBuf, "Unexpected save file size.");

    // Pre-edit backup (session_*.dat, SD only): the console state before this app's first
    // write since the game last saved. The mark file holds the signature of the state the
    // app wrote last. Same state at launch => the game has not saved in between => the pair
    // on the SD card is still that pre-edit state and must NOT be overwritten.
    const std::string sessP = sdDir + "session_personal.dat", sessM = sdDir + "session_main.dat";
    const std::string markPath = sdDir + "session_mark.txt";
    auto currentSig = [&]() { return pp::stateSignature(origP, pSz, origM, mSz); };   // origP/origM = validated console state
    bool sessionBackupDone = editSupported && pp::fileSize(sessP.c_str()) == (long)pSz && pp::fileSize(sessM.c_str()) == (long)mSz &&
                             pp::stateMarkMatches(markPath.c_str(), currentSig());
    // Called after every successful console write (SD only, best effort: without the mark
    // the next launch simply takes a fresh pre-edit backup).
    auto markWritten = [&]() { pp::writeStateMark(markPath.c_str(), currentSig(), sessionBackupDone); };

    pp::EngineHooks hooks;
    // Slots 1..20 always; the bag (slots 21..40) only on a known 3.0.x layout, as far as
    // the game's own counter says it is unlocked. The counter is read, never written.
    static_assert(POCKETS_BASE == pp::POCKETS_OFF_30, "pocket block offset");
    const int pocketSlots = editSupported ? pp::slotsFromBagCount(LE32(personal + pp::BAG_COUNT_OFF_30)) : pp::SLOT_COUNT;
    hooks.pockets = !saveReady ? pp::Pockets()
                  : pocketSlots > pp::SLOT_COUNT ? pp::Pockets(personal + POCKETS_BASE, personal + pp::BAG_OFF_30, pocketSlots)
                  : pp::Pockets(personal + POCKETS_BASE);
    hooks.vals = g_vals;
    hooks.items = &g_allItems;
    hooks.favorites = &favorites;
    hooks.stack = &stackRules;
    hooks.limits = &stackLimits;
    hooks.glyphs = &glyphMap;
    hooks.saveReady = saveReady;
    hooks.editSupported = editSupported;
    hooks.sessionBackupKept = sessionBackupDone;
    hooks.fatalError = saveReady ? nullptr : fatalBuf;
    hooks.loadoutNames = LOADOUT_NAMES;
    hooks.loadoutCount = LOADOUT_COUNT;
    hooks.sdDir = sdDir;
    hooks.version = APP_VERSION_STR;

    // Apply = exactly one doSave() call (invariant I3), preceded by the slot sync (I2).
    hooks.apply = [&]() -> pp::EngineResult {
        g_status_buf[0] = '\0';
        pp::syncSaveSlotVars(personal + POCKETS_BASE, g_slot, g_newId, g_newCount);
        // Never write a save the game would refuse: recompute the hashes, then check all 21.
        if (!editSupported) { snprintf(g_status_buf, sizeof(g_status_buf), "Unsupported save layout: nothing written"); return pp::ER_OTHER; }
        finalizeBuffers(true);
        if (!hashesValid(personal, mainData)) { snprintf(g_status_buf, sizeof(g_status_buf), "HASH self-check failed: nothing written"); return pp::ER_HASH; }
        doSave();
        pp::EngineResult r = pp::parseSaveStatus(g_status_buf);
        if (r == pp::ER_OK) { markWritten(); noteConsoleHashes(); }
        return r;
    };
    hooks.restore = [&]() -> pp::EngineResult {
        g_status_buf[0] = '\0';
        doRestore();
        pp::EngineResult r = pp::parseRestoreStatus(g_status_buf);
        if (r == pp::ER_OK) { markWritten(); noteConsoleHashes(); }
        return r;
    };
    hooks.doctor = [&]() -> pp::EngineResult {
        g_status_buf[0] = '\0';
        // The repair pass rewrites the working buffers as they are: give them valid hashes
        // first. This is what makes the Doctor able to fix a save the game refuses.
        if (!editSupported) { snprintf(g_status_buf, sizeof(g_status_buf), "Unsupported save layout: nothing written"); return pp::ER_OTHER; }
        finalizeBuffers(false);
        if (!hashesValid(personal, mainData)) { snprintf(g_status_buf, sizeof(g_status_buf), "HASH self-check failed: nothing written"); return pp::ER_HASH; }
        doDoctor();
        pp::EngineResult r = pp::parseDoctorStatus(g_status_buf);
        if (r == pp::ER_OK || r == pp::ER_OK_NO_FORENSIC) { markWritten(); noteConsoleHashes(); }
        return r;
    };

    // Read-only validation of a backup pair before any restore (invariant I8), on
    // decrypted COPIES: sizes must match this save, the 4 EncryptedInt32 checksums must
    // be valid, and the backup may only differ from the loaded save where this app
    // writes (pockets, the 4 values, the 8 heal fields). Catches empty, truncated,
    // damaged and foreign backups, and backups made before the game last saved.
    hooks.checkBackup = [&](bool session) -> pp::BackupCheck {
        std::string bp = sdDir + (session ? "session_personal.dat" : "backup_personal.dat");
        std::string bm = sdDir + (session ? "session_main.dat" : "backup_main.dat");
        long s1 = pp::fileSize(bp.c_str()), s2 = pp::fileSize(bm.c_str());
        if (s1 < 0 || s2 < 0) return pp::BC_MISSING;
        if ((size_t)s1 != pSz || (size_t)s2 != mSz) return pp::BC_SIZE;
        uint8_t* cp = (uint8_t*)malloc(pSz);
        uint8_t* cm = (uint8_t*)malloc(mSz);
        pp::BackupCheck res = pp::BC_OK;
        if (!cp || !cm) res = pp::BC_NOMEM;
        else {
            FILE* f1 = fopen(bp.c_str(), "rb"); FILE* f2 = fopen(bm.c_str(), "rb");
            bool okRead = f1 && f2 && fread(cp, 1, pSz, f1) == pSz && fread(cm, 1, mSz, f2) == mSz;
            if (f1) fclose(f1);
            if (f2) fclose(f2);
            if (!okRead) res = pp::BC_READ;
            else {
                CryptACNH(pHdr, cp, pSz); CryptACNH(mHdr, cm, mSz);   // copies only
                auto fieldOk = [](const uint8_t* b, uint32_t off) { return b[off + 7] == CalcChecksum(LE32(b + off)); };
                if (!fieldOk(cp, WALLET_OFF) || !fieldOk(cp, BANK_OFF) || !fieldOk(cp, MILES_OFF) || !fieldOk(cm, LOAN_OFF))
                    res = pp::BC_CORRUPT;
                std::vector<pp::ByteRange> okP = { { POCKETS_BASE, (uint32_t)pp::POCKETS_BYTES }, { pp::BAG_OFF_30, (uint32_t)pp::POCKETS_BYTES },
                                                   { WALLET_OFF, 8 }, { BANK_OFF, 8 }, { MILES_OFF, 8 } };
                std::vector<pp::ByteRange> okM = { { LOAN_OFF, 8 } };
                for (int i = 0; i < 8; i++) okM.push_back({ (uint32_t)(0x5B3D50 + (i * 0x74A40)), 4 });
                if (editSupported) {                          // the hash words follow the data they protect
                    for (int i = 0; i < HASH_PN; i++) okP.push_back({ HASH_P[i].off, 4 });
                    for (int i = 0; i < HASH_MN; i++) okM.push_back({ HASH_M[i].off, 4 });
                }
                if (pp::diffOutside(personal, cp, pSz, okP.data(), (int)okP.size()) != 0 || pp::diffOutside(mainData, cm, mSz, okM.data(), (int)okM.size()) != 0)
                    res = pp::BC_CORRUPT;
                // A backup with a stale hash would be refused by the game: don't restore it.
                else if (res == pp::BC_OK && editSupported && !hashesValid(cp, cm)) res = pp::BC_STALE_HASH;
            }
        }
        if (cp) free(cp);
        if (cm) free(cm);
        return res;
    };

    // Pre-edit backup (SD only), taken before the first console write. Kept as long as
    // the console state is the one this app wrote last (see sessionBackupDone above).
    hooks.writeSessionBackup = [&]() -> bool {
        if (sessionBackupDone) return true;
        remove(markPath.c_str());                              // the old mark describes the pair being replaced
        bool ok = pp::writeFileAll(sessP.c_str(), origP, pSz) && pp::writeFileAll(sessM.c_str(), origM, mSz);
        if (!ok) { remove(sessP.c_str()); remove(sessM.c_str()); }   // no half-written pair, no wasted space
        sessionBackupDone = ok;
        return ok;
    };
    // SD only: makes the pre-edit backup the source of the regular restore block.
    hooks.sessionToBackup = [&]() -> bool {
        return pp::copyFile(sessP.c_str(), (sdDir + "backup_personal.dat").c_str()) &&
               pp::copyFile(sessM.c_str(), (sdDir + "backup_main.dat").c_str());
    };

    // Read-only: the Stalk Market as the game stored it in main.dat (never written).
    hooks.turnips = [&](pp::TurnipWeek& w) -> bool {
        return saveReady && editSupported && mainData && pp::readTurnipWeek(mainData, mSz, w);
    };
    // Console clock (the one the game uses), weekday 0 = Sunday.
    hooks.clock = [](pp::GameDate& g, int& weekday) -> bool {
        u64 ts = 0;
        TimeCalendarTime ct; TimeCalendarAdditionalInfo ci;
        if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &ts))) return false;
        if (R_FAILED(timeToCalendarTimeWithMyRule(ts, &ct, &ci))) return false;
        g.year = ct.year; g.month = ct.month; g.day = ct.day; g.hour = ct.hour; g.minute = ct.minute;
        g.valid = true; weekday = (int)ci.wday;
        return true;
    };

    // Read-only diagnostics for System > Info.
    hooks.info = [&](pp::InfoData& d) {
        d.saveSizeOk = editSupported;
        d.personalSize = (long)pSz; d.mainSize = (long)mSz;
        d.pRegCount = conPN; d.mRegCount = conMN;               // console state, see noteConsoleHashes
        d.pRegOk = conPOk; d.mRegOk = conMOk;
        if (saveReady) {
            bool allZero = true;
            for (int i = 0; i < 8; i++) {
                uint32_t off = 0x5B3D50 + (i * 0x74A40);
                if (off + 4 <= mSz && LE32(mainData + off) != 0) allZero = false;
            }
            d.poisonAllZero = allZero;
        }
        d.backupPersonal = pp::fileSize((sdDir + "backup_personal.dat").c_str());
        d.backupMain = pp::fileSize((sdDir + "backup_main.dat").c_str());
        d.sessionPersonal = pp::fileSize((sdDir + "session_personal.dat").c_str());
        d.sessionMain = pp::fileSize((sdDir + "session_main.dat").c_str());
    };
    hooks.keyboard = [](const char* guide, const char* initial) { return showKeyboard(guide, initial); };
    hooks.reloadLists = reloadLists;

    ui.start(hooks);
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad); u64 held = padGetButtons(&pad);
        if (!ui.frame(k, held)) break;
    }

    ui.shutdown();
    TTF_Quit();
    if (personal) free(personal);
    if (pHdr) free(pHdr);
    if (origP) free(origP);
    if (mainData) free(mainData);
    if (mHdr) free(mHdr);
    if (origM) free(origM);
    if (R_SUCCEEDED(rc)) fsFsClose(&fs);
    SDL_DestroyRenderer(g_ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
