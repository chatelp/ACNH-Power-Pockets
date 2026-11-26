// ============================================================================
// ACNH Power Pockets - lists from catalogue.ac (The ULTIMATE Catalogue).
//
// Reads what the site gives: its export file (menu > Export, JSON) or the link
// of a shared list (https://catalogue.ac/?l=...). Each list becomes a collection
// of the Catalog ("Your lists"). Reading a list never changes the save: taking
// or filling from it only stages pending changes, like the rest of the app.
//
// Item ids: catalogue.ac uses the game's own item id, in decimal ("79" is
// 0x004F), except for clothing ("c652", one game id per color, from
// catalogue_ac.txt). Furniture variants ("79-3") are taken in their base
// variant, recipes and non-items are skipped; every skip is counted.
// Pure logic, tested on the host (tests/host/test_lists.cpp).
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "pp_model.h"

namespace pp {

static const size_t LIST_TEXT_MAX  = 2 * 1024 * 1024;    // one export or link
static const int    LIST_FILES_MAX = 64;
static const int    LIST_ITEMS_MAX = 2000;               // per list

struct ListItem { uint16_t id; int qty; };
struct ImportedList { std::string name; std::vector<ListItem> items; };
struct ListStats {
    int variants = 0;    // furniture variants taken in their base variant
    int recipes = 0;     // DIY recipes (not imported yet)
    int notItems = 0;    // villagers, reactions...: not objects
    int unknown = 0;     // ids this app doesn't know
    int skipped() const { return recipes + notItems + unknown; }
};

// catalogue_ac.txt: "c652 2542 14A9" = clothing id, then the game id of each color.
class ClothingIds {
public:
    bool load(const char* path);                          // false if missing
    bool applyLine(const std::string& line);              // exposed for tests
    bool lookup(int cid, int color, uint16_t& id) const;
    size_t size() const { return map_.size(); }
private:
    std::map<int, std::vector<uint16_t> > map_;
};

enum ListResult { LR_OK, LR_NOT_LIST, LR_BAD, LR_EMPTY, LR_TOO_BIG, LR_SD };   // LR_SD: add() couldn't write the file
// Reads an export, a single list, or text holding a share link. LR_EMPTY: valid, but no
// list with at least one item this app can place.
ListResult readListText(const std::string& text, const std::vector<Item>& items, const ClothingIds& clothing,
                        std::vector<ImportedList>& out, ListStats& stats);

struct ListFile {
    std::string file;                                    // name in the lists folder
    std::vector<ImportedList> lists;                     // without the lists a newer file replaces
    ListStats stats;
    ListResult result = LR_OK;
    long mtime = 0;
};

// What the list still asks for once the pockets are counted (pending changes included):
// each item minus what the pockets already hold of it, any variant. `isSingle` tells a
// 1/slot item (one per slot, its quantity field is a variant) from a stack. `already` =
// pieces of the list already in the pockets.
std::vector<ListItem> stillNeeded(const ImportedList& list, const Pockets& pockets,
                                  const std::function<bool(uint16_t)>& isSingle, int& already);

// The folder sdmc:/switch/acnh_power_pockets/lists/. A list is known by its name: receiving
// a list with the name of one already there replaces it, so sending a newer export again
// simply updates the lists. Received lists are stored one per file, "<name>.json" (the
// single-list format, game ids); files copied by hand (exports, links) are read as they are,
// and when two files hold a list of the same name, the newer file wins.
class ImportedLists {
public:
    void load(const std::string& dir, const std::vector<Item>& items, const ClothingIds& clothing);
    const std::vector<ListFile>& files() const { return files_; }
    int listCount() const;
    // Checks `text`, then stores each of its lists and reloads. `got` = what it holds,
    // `replaced` = how many of them replaced a list of the same name.
    ListResult add(const std::string& dir, const std::string& text, const std::vector<Item>& items,
                   const ClothingIds& clothing, std::vector<ImportedList>& got, ListStats& stats, int& replaced);
    bool remove(const std::string& dir, const std::string& file, const std::vector<Item>& items, const ClothingIds& clothing);
private:
    std::vector<ListFile> files_;
};

} // namespace pp
