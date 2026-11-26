// ACNH Power Pockets - every on-screen string in one place (English UI).
// Tone (docs/design/CHARTE.md section 9): short sentences, no exclamation marks,
// say what happened and what to do next.
#pragma once

namespace pp {
namespace S {

static const char* const APP_TITLE        = "ACNH Power Pockets";

// Panels (tabs)
static const char* const TAB[7] = { "Pockets", "Favorites", "Catalog", "Bells & Miles", "Turnips", "Loadouts", "System" };

// Header
static const char* const HOLDING_NONE     = "Holding: \xE2\x80\x94";           // "Holding: —"
static const char* const HOLDING_FMT      = "Holding: %s";
static const char* const CHIP_UPTODATE    = "Up to date";
static const char* const CHIP_CHANGES_FMT = "%d change%s";

// Help bar
static const char* const H_ACTIONS = "Actions";
static const char* const H_BACK    = "Back";
static const char* const H_CLOSE   = "Close";
static const char* const H_CHOOSE  = "Choose";
static const char* const H_CONFIRM = "Confirm";
static const char* const H_CANCEL  = "Cancel";
static const char* const H_PANEL   = "Panel";
static const char* const H_REVIEW  = "Review";
static const char* const H_DISCARD = "Discard";
static const char* const H_QUIT    = "Quit";
static const char* const H_PLACE   = "Place + next";
static const char* const H_TAKE    = "Take";
static const char* const H_TYPE    = "Type";
static const char* const H_SEARCH  = "Search";
static const char* const H_OPEN    = "Open";
static const char* const H_FILTER  = "Type";
static const char* const H_MODIFY  = "Modify";
static const char* const H_LOAD    = "Load";
static const char* const H_HOLD_A  = "Hold to confirm";

// Generic words
static const char* const EMPTY        = "Empty";
static const char* const BADGE_SINGLE = "1/slot";
static const char* const BADGE_FAV    = "FAV";
static const char* const BADGE_WRITES = "WRITES NOW";
static const char* const DOESNT_STACK = "doesn't stack";
static const char* const BAG_SLOT     = "pocket upgrade slot";
static const char* const STACKS_TO_FMT = "stacks to %d";
static const char* const OVER_MAX_FMT  = "more than the game allows (%d)";
static const char* const VARIANT_FMT   = "variant value %d";
static const char* const ARROW        = " \xC2\xBB ";                          // " » "
static const char* const DOT          = " \xC2\xB7 ";                          // " · "
static const char* const ELLIPSIS     = "\xE2\x80\xA6";                       // "…"
static const char* const TIMES        = "\xC3\x97";                           // "×"

// Pockets
static const char* const M_PLACE_NEXT     = "Place in next empty slot";
static const char* const M_FILL_EMPTY_FMT = "Fill empty slots (%d)";
static const char* const M_TAKE           = "Take in hand";
static const char* const M_QTY            = "Quantity\xE2\x80\xA6";
static const char* const M_HELD_QTY       = "Held quantity\xE2\x80\xA6";
static const char* const M_ENTER_ID       = "Enter item ID (hex)\xE2\x80\xA6";
static const char* const M_ADD_FAV        = "Add to favorites";
static const char* const M_REMOVE_FAV     = "Remove from favorites";
static const char* const M_HIDE_FAV       = "Hide this built-in favorite";
static const char* const M_REVERT         = "Revert to original";
static const char* const M_CLEAR          = "Clear slot";
static const char* const R_NOTHING_HELD   = "nothing in hand";
static const char* const R_NO_EMPTY       = "no empty slot";
static const char* const R_SLOT_EMPTY     = "slot is empty";
static const char* const R_NOT_MODIFIED   = "not modified";
static const char* const R_SINGLE         = "1 (doesn't stack)";
static const char* const R_NO_ITEMS       = "items.txt missing";
static const char* const T_PLACED_FMT     = "Placed %s in slot %d";
static const char* const T_FULL           = "No empty slot left. Clear a slot first (A > Clear slot).";
static const char* const T_NOTHING_HELD   = "Nothing in hand. Take an item in Favorites or Catalog (R).";
static const char* const T_HOLDING_FMT    = "Holding: %s \xC2\xB7 Y to place in the next empty slot";
static const char* const T_QTY_ONE        = "Quantity set to 1 (doesn't stack)";
static const char* const T_QTY_CAP_FMT    = "Quantity set to %d, the most this item stacks to";
static const char* const T_CLEARED_FMT    = "Slot %d cleared (pending)";
static const char* const T_FILLED_FMT     = "%d slot%s filled (pending)";
static const char* const T_REVERTED       = "Reverted to the loaded value";
static const char* const T_BAD_ID         = "Invalid item ID. Use 4 hex digits, 0000 to FFFC.";
static const char* const D_FILL_TITLE     = "Fill the empty slots?";
static const char* const D_FILL_BODY_FMT  = "%d empty slot%s will get %s. Occupied slots are not touched.";
static const char* const KB_ENTER_ID      = "Item ID in hex (e.g. 16DB)";
static const char* const SLOT_DETAIL_FMT  = "Slot %d";

// Favorites / Catalog
static const char* const M_TAKE_FAV       = "Take";
static const char* const M_TAKE_QTY       = "Take with quantity\xE2\x80\xA6";
static const char* const M_RESET_FAVS     = "Restore default favorites";
static const char* const FAVS_EMPTY       = "No favorites yet.";
static const char* const FAVS_EMPTY_HINT  = "Add some from the Catalog: A > Add to favorites.";
static const char* const FAVS_RO_HINT     = "items.txt is missing, favorites can't be resolved.";
static const char* const T_FAV_ADDED      = "Added to favorites";
static const char* const T_FAV_REMOVED    = "Removed from favorites";
static const char* const T_FAV_FULL       = "Favorites are full (100). Remove one first.";
static const char* const T_FAV_IO         = "Couldn't save favorites to the SD card. Nothing changed.";
static const char* const T_FAV_RO         = "Favorites are read-only without items.txt";
static const char* const T_FAV_RESET      = "Default favorites restored (your list was kept as .bak)";
static const char* const T_FAV_IGNORED_FMT = "user_favorites.txt: %d line%s ignored";
static const char* const D_UNFAV_TITLE_FMT = "Remove %s from favorites?";
static const char* const D_HIDE_BODY      = "Built-in favorites come back with \"Restore default favorites\".";
static const char* const FAV_GROUP_YOURS  = "YOURS";
static const char* const FAV_GROUP_BUILTIN = "BUILT-IN";
static const char* const FAV_GROUP_CONT   = " (cont.)";
static const char* const D_RESET_TITLE    = "Restore the default favorites?";
static const char* const D_RESET_BODY     = "Your additions and removals are kept in user_favorites.txt.bak.";
static const char* const SEARCH_LABEL     = "Search";
static const char* const SEARCH_HINT      = "A, Y or X to type";   // cursor on the search field
static const char* const SEARCH_HINT_X    = "X to search";         // cursor on a result
static const char* const SEARCH_EMPTY     = "Type part of a name (e.g. \"star\") or a hex ID.";
static const char* const SEARCH_NONE_FMT  = "No item contains \"%s\".";
static const char* const KB_SEARCH        = "Search items (e.g. star, or 16DB)";
// Catalog collections (item_collections.txt) and the ZL / ZR type filter
static const char* const COLL_BROWSE      = "Browse collections";
static const char* const COLL_LABEL       = "Collection";
static const char* const COLL_MISSING     = "No collections";
static const char* const COLL_CTX_FMT     = "%d collections \xC2\xB7 A opens one \xC2\xB7 B goes back";
static const char* const COLL_CREDIT      = "Data: community ACNH spreadsheet";
static const char* const COLL_COUNT_FMT   = "%d item%s";
static const char* const COLL_RESULTS_FMT = "%s \xC2\xB7 %d item%s";
static const char* const COLL_GROUP_CONT  = " (cont.)";

// Lists from catalogue.ac
static const char* const GRP_YOUR_LISTS   = "Your lists";
static const char* const LISTS_CREDIT     = "Lists from catalogue.ac";
static const char* const RX_ENTRY         = "Receive a list from catalogue.ac";
static const char* const RX_ENTRY_RIGHT   = "Wi-Fi";
static const char* const LIST_BAD_FMT     = "%s \xC2\xB7 can't be read";
static const char* const LIST_BAD_RIGHT   = "A: delete";
static const char* const M_OPEN_LIST      = "Open";
static const char* const M_FILL_LIST      = "Fill pockets with this list";
static const char* const M_DELETE_FILE_FMT = "Delete %s";
static const char* const D_LFILL_TITLE_FMT = "Fill pockets with \"%s\"?";
static const char* const D_LFILL_BODY_FMT = "%d empty slot%s will get the items of this list, with their quantities. Occupied slots are not touched.";
static const char* const D_LFILL_SHORT_FMT = "%d item%s of the list won't fit.";
static const char* const D_LFILL_VARIANT  = "Furniture comes in its base color or pattern.";
static const char* const T_LFILLED_FMT    = "%d slot%s filled from the list (pending)";
static const char* const D_LDEL_TITLE_FMT = "Delete %s?";
static const char* const D_LDEL_BODY_FMT  = "It holds %d list%s. This only removes the file from the SD card; it can be sent again.";
static const char* const D_LDEL_BAD       = "This file can't be read as a catalogue.ac export or list link. This only removes it from the SD card.";
static const char* const T_LDELETED       = "List file deleted";
static const char* const RX_TITLE         = "Receive a list";
static const char* const RX_STEP1         = "On your phone or computer, on the same Wi-Fi, open:";
static const char* const RX_STEP2         = "Code:";
static const char* const RX_HINT          = "Send a catalogue.ac export or a list's share link. B stops receiving.";
static const char* const RX_WAITING       = "Waiting for a list\xE2\x80\xA6";
static const char* const RX_GOT_FMT       = "Received \"%s\": %d item%s";
static const char* const RX_MORE_FMT      = " (+%d more list%s)";
static const char* const RX_LOCKED        = "Too many wrong codes: receiving stopped. Open this screen again for a new code.";
static const char* const RX_ERR_TITLE     = "Can't receive";
static const char* const RX_E_OFFLINE     = "The console isn't connected to a network. Connect it in the System Settings, on the same Wi-Fi as your phone, or copy the export into switch/acnh_power_pockets/lists/ on the SD card.";
static const char* const RX_E_START       = "The receiving server couldn't start. Copy the export into switch/acnh_power_pockets/lists/ on the SD card instead.";
static const char* const RX_OK_FMT        = "\"%s\" is on the console: %d item%s. Open it in Catalog > Browse collections.";
static const char* const RX_SKIPPED_FMT   = " %d entr%s skipped (recipes, villagers or unknown items).";
static const char* const RX_NOT_LIST      = "This isn't a catalogue.ac export or a list's share link.";
static const char* const RX_EMPTY         = "Nothing in this list can go in pockets.";
static const char* const RX_SD            = "The console couldn't save it on the SD card.";
static const char* const H_STOP           = "Stop";
static const char* const FILTER_FMT       = "Type: %s";
static const char* const FILTER_OF_FMT    = " \xC2\xB7 %d shown";
static const char* const FILTER_NAME[]    = { "All", "Furniture", "Walls, floors & rugs", "Clothing", "Other" };
static const char* const FILTER_NONE_FMT  = "Nothing of type \"%s\" here.";
static const char* const FILTER_NONE_HINT = "ZL / ZR changes the type filter.";
static const char* const T_COLL_MISSING   = "item_collections.txt is missing: copy it next to items.txt";
static const char* const CTX_RESULTS_FMT  = "%d result%s for \"%s\"";

// Bells & Miles
static const char* const VAL_NAME[4]   = { "Wallet", "Bank", "Nook Miles", "House loan" };
static const char* const M_MODIFY      = "Modify\xE2\x80\xA6";
static const char* const M_TYPE_VALUE  = "Type a value\xE2\x80\xA6";
static const char* const M_SET_MAX     = "Set to max";
static const char* const M_SET_ZERO    = "Set to 0";
static const char* const M_PAY_LOAN    = "Pay off loan (0)";
static const char* const WALLET_WARN   = "Above the game's wallet max (99,999)";
static const char* const T_BAD_VALUE   = "Invalid value. Digits only.";
static const char* const STEP_MONEY    = "Left/right \xC2\xB1" "1,000 \xC2\xB7 up/down \xC2\xB1" "100,000";
static const char* const STEP_ACCEL    = "Hold a direction 2 s: steps \xC3\x97" "10";
static const char* const STEP_QTY      = "Left/right \xC2\xB1" "1 \xC2\xB7 up/down \xC2\xB1" "10";

// Loadouts
static const char* const M_LOAD_LO     = "Load into pockets";
static const char* const M_SAVE_LO     = "Save current pockets here";
static const char* const M_DELETE_LO   = "Delete loadout";
static const char* const LO_EMPTY      = "Empty";
static const char* const LO_SAVED_FMT  = "Saved \xC2\xB7 %d items in %d slots";
static const char* const LO_TOO_BIG_FMT = "Uses slot %d, but your pockets have %d slots: it can't be loaded.";
static const char* const T_LO_LOADED_FMT = "Loadout \"%s\" loaded (pending). + to review.";
static const char* const T_LO_SAVED_FMT  = "Loadout \"%s\" saved to the SD card";
static const char* const T_LO_DELETED    = "Loadout deleted";
static const char* const T_LO_BAD        = "Loadout file is damaged. Nothing was loaded.";
static const char* const LO_BAD          = "Damaged file";
static const char* const T_LO_WRITE      = "Couldn't write the loadout to the SD card";
static const char* const D_LO_REPLACE_FMT = "Replace \"%s\"?";
static const char* const D_LO_REPLACE_BODY = "The pockets as shown, including changes not applied yet, will be saved.";
static const char* const D_LO_LOAD_FMT  = "Load \"%s\"?";
static const char* const D_LO_LOAD_BODY = "It replaces all your slots, including your pending pocket changes.";
static const char* const D_LO_DEL_FMT   = "Delete \"%s\"?";

// System
static const char* const SYS_BANNER    = "Red actions write to the console save directly, without the Review step.";
static const char* const SYS_RESTORE   = "Restore SD backup to console";
static const char* const SYS_RESTORE_SUB = "State before your last Apply";
static const char* const SYS_SESSION   = "Restore pre-edit backup";
static const char* const SYS_SESSION_SUB = "State before this app's first write since the game last saved";
static const char* const SYS_DOCTOR    = "Save Doctor (repair save)";
static const char* const SYS_DOCTOR_SUB = "Only if the game refuses to load the save";
static const char* const SYS_RELOAD    = "Reload favorites and 1/slot rules";
static const char* const SYS_RELOAD_SUB = "Re-read user_favorites.txt and nonstackable.txt";
static const char* const SYS_ABOUT     = "About and updates";
static const char* const SYS_ABOUT_SUB = "Version, author and credits";
static const char* const SYS_QUIT      = "Quit";
static const char* const SYS_QUIT_SUB  = "Asks first if changes are pending";
static const char* const BLOCKED_FMT   = "Blocked: apply or discard the %d pending change%s first";
static const char* const BLOCKED_SESSION = "Blocked: no pre-edit backup for this save state yet";
static const char* const BLOCKED_TRUST = "Blocked: the last SD backup was not written completely";
static const char* const BLOCKED_TAINT = "Blocked: quit and relaunch the app first";
static const char* const BLOCKED_RO    = "Blocked: unsupported save version";
static const char* const T_RELOADED    = "Favorites and 1/slot rules reloaded";

// Dialogs: review / apply / discard / quit
static const char* const REVIEW_BANNER_FMT = "Review \xC2\xB7 %d change%s \xC2\xB7 nothing is written until you apply";
static const char* const REVIEW_NONE   = "No pending changes";
static const char* const REVIEW_GROUP_POCKETS = "POCKETS";
static const char* const REVIEW_GROUP_MONEY   = "BELLS & MILES";
static const char* const REVIEW_NOTE1  = "Apply writes personal.dat and main.dat to the console save.";
static const char* const REVIEW_NOTE2  = "The SD backup is replaced first by the current console state.";
static const char* const B_APPLY       = "Apply";
static const char* const B_APPLY_QUIT  = "Apply & quit";
static const char* const B_DISCARD_ALL = "Discard all";
static const char* const B_QUIT_NOAPPLY = "Quit without applying";
static const char* const B_BACK        = "Back";
static const char* const B_QUIT        = "Quit";
static const char* const B_STAY        = "Stay";
static const char* const B_NO          = "No";
static const char* const B_YES_DISCARD = "Yes, discard all";
static const char* const B_CANCEL      = "Cancel";
static const char* const B_OK          = "OK";
static const char* const B_FILL        = "Fill";
static const char* const B_REMOVE      = "Remove";
static const char* const B_RESTORE_DEF = "Restore defaults";
static const char* const B_REPLACE     = "Replace";
static const char* const B_LOAD        = "Load";
static const char* const B_DELETE      = "Delete";
static const char* const B_APPLY_ANYWAY = "Apply anyway";
static const char* const B_RESTORE_ANYWAY = "Restore anyway";
static const char* const B_REPAIR_ANYWAY = "Repair anyway";
static const char* const B_HOLD_RESTORE = "Hold A to restore";
static const char* const B_HOLD_REPAIR = "Hold A to repair";
static const char* const M_REVERT_CHANGE = "Revert this change";
static const char* const M_GO_TO       = "Go to item";

static const char* const D_DISCARD_TITLE_FMT = "Discard %d change%s?";
static const char* const D_DISCARD_BODY = "Pockets and money go back to the loaded state. Nothing is written.";
static const char* const T_DISCARDED   = "Changes discarded";
static const char* const T_NO_CHANGES  = "No changes to discard";
static const char* const D_QUIT_TITLE_FMT = "Quit with %d change%s not applied?";
static const char* const D_QUIT_BODY   = "Quitting without applying writes nothing.";
static const char* const WAIT_TITLE    = "Writing\xE2\x80\xA6";
static const char* const WAIT_BODY     = "Don't turn off the console.";
static const char* const T_APPLIED     = "Changes applied \xC2\xB7 SD backup updated with the previous state";

static const char* const D_SESSION_FAIL_TITLE = "Pre-edit backup failed";
static const char* const D_SESSION_FAIL_BODY  = "The state before your edits couldn't be copied to the SD card. The regular SD backup is still made before writing.";
static const char* const D_SESSION_FAIL_DIRECT = "The state before your edits couldn't be copied to the SD card. If you continue, the current console state is not kept anywhere.";
static const char* const D_ERR_TITLE   = "Nothing was written";
static const char* const E_BACKUP      = "The SD backup failed (card full, locked or missing?). Nothing was written to the save. Your changes are still pending.";
static const char* const E_NAND        = "Writing the save failed and was not committed. The console save was not changed. Your changes are still pending: try Apply again, or discard them.";
static const char* const E_HASH        = "The integrity hashes couldn't be made consistent, so nothing was written. Your changes are still pending. Please report this.";
static const char* const E_MALLOC      = "Not enough memory. Nothing was written. Relaunch the app through a game (hold R).";
static const char* const E_OTHER       = "The engine reported an unexpected status. Check the save in System > Info before doing anything else.";
static const char* const E_RO          = "This save version isn't supported (unexpected file sizes). The app is read-only.";
static const char* const E_TAINTED     = "A repair failed earlier in this session. Quit the app completely and relaunch it.";

// Danger dialogs
static const char* const DANGER_TITLE  = "Direct NAND write";
static const char* const D_RESTORE_SUB = "Restore the SD backup?";
static const char* const D_RESTORE_B1  = "The console save is replaced right away by the SD backup made before your last Apply.";
static const char* const D_RESTORE_B2  = "Everything applied since that backup is lost. The pre-edit backup stays on the SD card.";
static const char* const D_SESSION_SUB = "Restore the pre-edit backup?";
static const char* const D_SESSION_B1  = "The console save goes back to its state before this app first wrote to it, since the game last saved.";
static const char* const D_SESSION_B2  = "Everything applied since is lost. The regular SD backup is replaced by the pre-edit backup first.";
static const char* const D_DOCTOR_SUB  = "Run the Save Doctor?";
static const char* const D_DOCTOR_B1   = "Only use it if the game refuses to load the save after an edit.";
static const char* const D_DOCTOR_B2   = "It recomputes the integrity hashes, copies the previous state to damaged_*.dat, then rewrites the save right away.";
static const char* const D_BLOCKED_TITLE = "Not available right now";
static const char* const D_BLOCKED_PENDING_FMT = "You have %d pending change%s. Apply them (+) or discard them (-) first.";
static const char* const BK_MISSING    = "There is no backup on the SD card yet.";
static const char* const BK_SIZE       = "The backup doesn't match this save (different size).";
static const char* const BK_READ       = "The backup couldn't be read from the SD card.";
static const char* const BK_CORRUPT    = "This backup doesn't belong to the current save: it is damaged, or the game has saved since it was made. It can't be restored.";
static const char* const BK_NOMEM      = "Not enough memory to check the backup.";
static const char* const BK_STALE_HASH = "This backup has a stale integrity hash: the game would refuse it. It can't be restored.";
static const char* const D_DOCTOR_FAILED_TITLE = "Repair failed";
static const char* const E_DOCTOR_FAILED = "The repair couldn't be written and was not committed. Quit the app completely and relaunch it before doing anything else.";
static const char* const E_SESSION_TO_BACKUP = "The pre-edit backup couldn't be copied over the regular backup. Nothing was restored. The regular backup is now incomplete: Apply once to make a new one.";
static const char* const T_RESTORED    = "Save restored from the SD backup";
static const char* const D_REPAIRED_TITLE = "Repair finished";
static const char* const D_REPAIRED_BODY  = "Quit the app completely, then start the game.";
static const char* const D_REPAIRED_NOFOR = "Repair finished, but the forensic copy (damaged_*.dat) couldn't be written.";
static const char* const E_RESTORE_FAILED = "Restore failed. The console save was not changed (no commit).";

// Fatal / degraded
static const char* const FATAL_TITLE   = "The save can't be opened";
static const char* const FATAL_HINT1   = "Fully close Animal Crossing (HOME > X > Close), then relaunch this app";
static const char* const FATAL_HINT2   = "through the game: hold R while starting Animal Crossing.";
static const char* const NO_ITEMS_BANNER = "items.txt is missing: no item names (IDs shown) and no 1/slot protection.";
static const char* const RO_BANNER     = "Unsupported save version: read-only.";

// Turnips (Stalk Market, read-only)
static const char* const TN_BANNER       = "Stalk Market \xC2\xB7 read from your save, nothing is written";
static const char* const TN_WEEK_FMT     = "Week of %s";
static const char* const TN_PAST_FMT     = "These prices are from the week of %s. Play once this week to get this week's.";
static const char* const TN_FUTURE       = "The save is dated after the console clock: these may not be this week's prices.";
static const char* const TN_NO_CLOCK     = "Console clock unavailable";
static const char* const TN_UNSUPPORTED  = "Turnip prices can only be read from a 3.0.x save.";
static const char* const TN_DAISY        = "Daisy Mae (Sunday)";
static const char* const TN_PATTERN      = "Pattern";
static const char* const TN_BEST         = "Best price";
static const char* const TN_UNKNOWN_PAT  = "Unknown (%u)";
static const char* const TN_UNUSUAL      = "Unusual values in the save, shown as they are";
static const char* const TN_AM           = "AM";
static const char* const TN_PM           = "PM";
static const char* const TN_DAYS[7]      = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char* const TN_DAYS3[7]     = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* const TN_STATS_FMT    = "Min %u \xC2\xB7 Max %u \xC2\xB7 Avg %.1f \xC2\xB7 dashed line: Daisy Mae's price";
static const char* const TN_BUY_FMT      = "Buy %u";
static const char* const TN_HOLD_FMT     = "%s turnips in your pockets";
static const char* const TN_SPOILED_FMT  = " \xC2\xB7 %d slot%s spoiled";
static const char* const TN_NONE         = "No turnips in your pockets";
static const char* const TN_NONE_HINT    = "Daisy Mae sells them on Sunday mornings.";
static const char* const TN_NOW_FMT      = "Now (%s, %u): %s Bells";
static const char* const TN_SUNDAY       = "Sunday: Nook's Cranny doesn't buy turnips today.";
static const char* const TN_BEST_FMT     = "Best still to come (%s, %u): %s Bells";
static const char* const TN_BEST_WEEK_FMT = "Best this week (%s, %u): %s Bells";
static const char* const TN_NO_BEST      = "No better price left this week.";
static const char* const TN_STALE_VALUE  = "Their value needs this week's prices.";
static const char* const TN_DETAIL_FMT   = "Turnips \xC2\xB7 read-only \xC2\xB7 Daisy Mae %u \xC2\xB7 %s";
static const char* const TN_DETAIL_NOW_FMT = " \xC2\xB7 now %s: %u";
static const char* const MONTHS3[12]     = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char* const STALE_BANNER  = "Stale integrity hash: the game would refuse this save. Fix it with System > Save Doctor.";

static const char* const SPLASH_TITLE  = "Reading the save\xE2\x80\xA6";
static const char* const SPLASH_HINT1  = "This takes a few seconds.";
static const char* const SPLASH_HINT2  = "On a save with a stale integrity hash it can take several minutes. Please wait.";

// About
static const char* const AB_TITLE      = "About";
static const char* const AB_AUTHOR     = "chatelp";
static const char* const AB_URL        = "github.com/chatelp/ACNH-Power-Pockets";
static const char* const AB_CREDITS[5][2] = {
    { "Save engine", "ACNH Save Editor by cbreezy210 (MIT)" },
    { "Item data", "NHSE by kwsch (GPL-3.0)" },
    { "Collections", "ACNH data spreadsheet, via Norviah (MIT)" },
    { "Turnips panel", "inspired by Turnips by averne" },
    { "Font", "Varela Round (OFL 1.1)" },
};
static const char* const AB_NOTE       = "Not affiliated with Nintendo. Use at your own risk, offline.";
static const char* const B_CHECK       = "Check for updates";
static const char* const B_CLOSE       = "Close";

// Updates (network only when asked; nothing here touches the save)
static const char* const U_ASK_TITLE   = "Connect to GitHub?";
static const char* const U_ASK_B1      = "The app will look for a newer version on github.com. It sends nothing about you or your save.";
static const char* const U_ASK_B2      = "It never turns Wi-Fi on. On a console with custom firmware, going online is at your own risk.";
static const char* const B_CONNECT     = "Connect";
static const char* const U_ERR_TITLE   = "No update";
static const char* const U_CHECKING    = "Checking for updates\xE2\x80\xA6";
static const char* const U_DOWNLOADING = "Downloading the update\xE2\x80\xA6";
static const char* const U_PREPARING   = "Checking the package\xE2\x80\xA6";
static const char* const U_PROGRESS_HINT = "Nothing is installed yet. B cancels.";
static const char* const U_UPTODATE_TITLE = "Up to date";
static const char* const U_UPTODATE_FMT = "You have the latest version (%s).";
static const char* const U_AVAIL_FMT   = "Version %s is available";
static const char* const U_AVAIL_SIZE_FMT = "You have %s. Download: %.1f MB.";
static const char* const U_BULLET      = "\xE2\x80\xA2 ";
static const char* const B_NOT_NOW     = "Not now";
static const char* const B_DOWNLOAD    = "Download";
static const char* const U_PENDING     = "Apply or discard your pending changes first, then update.";
static const char* const U_PLACE       = "This copy of the app isn't switch/acnh_power_pockets/acnh_power_pockets.nro, so it can't update itself. Download the new version from GitHub.";
static const char* const U_READY_TITLE_FMT = "Install version %s?";
static const char* const U_READY_B1    = "The app will close, replace its own files and start again.";
static const char* const U_READY_B2    = "Your save, backups, loadouts and favorites are not touched. Don't turn off the console meanwhile.";
static const char* const B_INSTALL     = "Install and restart";
static const char* const U_CANCELLED   = "Update cancelled, nothing was installed";
static const char* const U_E_OFFLINE   = "The console isn't connected to the internet. Connect it in the System Settings, or download the new version from GitHub on another device.";
static const char* const U_E_TLS       = "The secure connection to GitHub couldn't be verified. Check the console's date and time, then try again.";
static const char* const U_E_HTTP      = "GitHub didn't answer as expected. Try again later.";
static const char* const U_E_LIMIT     = "GitHub is limiting requests from your connection. Try again in an hour.";
static const char* const U_E_NETWORK   = "The connection was lost. Nothing was installed.";
static const char* const U_E_RELEASE   = "The latest release can't be installed from the app. Download it from GitHub.";
static const char* const U_E_CHECKSUM  = "The download is incomplete or damaged (wrong checksum). Nothing was installed.";
static const char* const U_E_PACKAGE   = "The package isn't what this app expects. Nothing was installed. Download it from GitHub.";
static const char* const U_E_SD        = "The SD card couldn't be written (full or damaged?). Nothing was installed.";
static const char* const U_N_UPDATED_FMT = "Updated to version %s";
static const char* const U_N_ROLLEDBACK = "The update didn't finish: the previous version was put back";

// Info pane
static const char* const INFO_TITLE    = "Info";

} // namespace S
} // namespace pp
