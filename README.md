# ACNH Power Pockets 🍃

**Powerful, safe inventory editing for Animal Crossing: New Horizons, right on your Switch.**

> **ACNH Power Pockets is a fork of [ACNH-Save-Editor](https://github.com/cbreezy210/ACNH-Save-Editor) by cbreezy210** (based on v1.4.1, upstream commit `731c228`).
> The save engine (decryption, encryption, value encoding, SD backups, restore, Save Doctor) is the upstream one, **unchanged**. This fork rebuilds the interface around inventory work. All credit for the engine goes to cbreezy210 and the projects listed in [Credits](#credits).

> **Note:** This is an educational project. Always keep your own backups. Modifying save data always carries a risk. Use offline, at your own risk. Not affiliated with Nintendo.
>
> **Responsible use:** Please don't trade or use unreleased/internal items in online play.

---

## Why this fork

ACNH-Save-Editor made on-console save editing possible. Power Pockets keeps its engine and focuses on one job: **editing your pockets quickly without ever surprising you.**

| Problem in upstream | Power Pockets |
|---|---|
| Putting the same item in 10 slots meant 10 searches or editing `items.txt`. | Take an item once, then press **Y** ten times: the next 10 **empty** slots are filled. |
| Favorites were a fixed list compiled into the app. | Add or remove favorites from the app. They are saved to `user_favorites.txt` on the SD card. |
| The quantity field was shown for items that never stack (tools…). | `1/slot` badge, no quantity prompt, neutral value written. You can edit the list in `nonstackable.txt`. |
| Pressing **A** wrote to the console save immediately, one slot at a time. | Every change is prepared in memory. **Review** lists them before → after, then **Apply** writes them all at once. |
| **ZL** restored the backup in one press. **ZR ZR** ran the Save Doctor. | Both moved to the **System** panel, behind a red hold-to-confirm dialog. Blocked while changes are pending. |
| Eight buttons with a different meaning on each screen. | Six panels (**L**/**R**), **A** opens an action menu, **Y** does the obvious thing, **B** closes. |

![Pockets panel](docs/screenshots/pockets.png)

## Safety first: what is kept, what is added

**Kept from upstream (same code, verified byte-for-byte by `tools/check_engine_untouched.py`):**
- Automatic SD backup of the console state before every write (`backup_personal.dat`, `backup_main.dat`).
- The same encryption and EncryptedInt32 checksum re-encoding for Wallet, Bank, Nook Miles and House loan.
- The same value limits (quantity 1–99, Wallet/Bank ≤ 999,999,999, Miles/Loan ≤ 9,999,999).
- Restore from SD backup and Save Doctor, moved verbatim into the System panel.
- The Album/applet launch guard.

**Added by Power Pockets (UI side only):**
- **Prepare, review, apply.** Nothing reaches the save until **Review > Apply**, which calls the upstream `doSave()` exactly once. **Discard** (−) drops everything in memory and writes nothing.
- **Never overwrites an item by accident.** Placing only targets empty slots. To replace an item, clear it first (explicit).
- **Start-of-session backup.** Before the first write of a session, the console state is also copied to `session_personal.dat` / `session_main.dat`. Those files are never overwritten during that session, even if you apply several times.
- **Backup validation** before any restore. Size and checksums are checked on a decrypted copy, so an empty or truncated backup is refused.
- **Guards.** Restore and Doctor are blocked while changes are pending, after an incomplete SD backup, or after a failed repair. Editing is disabled (read-only) if the save files don't have the expected 3.0.x sizes.
- **Hold to confirm** (1.5 s) for every action that writes the console save directly.

> **Honest note on integrity hashes.** The upstream README mentions automatic "hash healing". The inherited code finds the Murmur3 hash regions but **does not recompute them** when writing ([audit](docs/engine/AUDIT_ECRITURE.md), point h). Power Pockets does not change the engine, so this is unchanged. System > Info shows how many region hashes currently match. Run the first-use test below before editing your real island.

## First use: a 5-minute test (strongly recommended)

1. Make a full backup of your ACNH save with **JKSV** or **Checkpoint** and copy it to a PC.
2. In Power Pockets, put one stackable item (e.g. Nook Miles Ticket ×9) in an **empty** slot, then **+ > Apply**.
3. Start the game. Does the save load? How many tickets do you get: 9 or 10?
   - 10 means the game stores "quantity − 1". Tell us (or set `kCountZeroBased = true` in `source/pp_model.h`) so quantities display exactly.
4. Save in game, quit, reopen Power Pockets: System > Info shows the region hashes.
5. If the save doesn't load: restore your JKSV/Checkpoint backup and open an issue.

## Panels and controls

| Button | Everywhere |
|---|---|
| **L / R** | Previous / next panel (no wrap: System is always at the far right) |
| **D-pad / left stick** | Move. Long press repeats. |
| **A** | Open the action menu of the selected item (the first entry is what **Y** does) |
| **Y** | Quick action: place (Pockets), take (Favorites/Catalog), modify (Bells & Miles), load (Loadouts) |
| **B** | Close the menu / dialog / stepper |
| **+** | **Review**: every pending change before → after, then **Apply**, **Apply & quit**, **Discard all**, **Quit without applying** |
| **−** | Discard all pending changes (asks first) |
| **ZL / ZR / X** | Nothing. ZL no longer restores and ZR no longer runs the Doctor. |
| **HOME** | Leaves without writing anything that wasn't applied |

| Panel | What you do there |
|---|---|
| **Pockets** | 20 slots (1–10 left, 11–20 right). **Y** places the item in hand into the next empty slot. **A** gives: Fill empty slots, Take in hand, Quantity…, Held quantity…, Enter item ID (hex)…, Add/Remove favorite, Revert to original, Clear slot. |
| **Favorites** | Built-in favorites + yours, A–Z. **Y** takes the item and returns to Pockets. **A**: Take with quantity…, Remove from favorites, Restore default favorites. |
| **Catalog** | Search the item database by name or by 4-digit hex ID. ★ marks favorites, `1/slot` marks items that don't stack. |
| **Bells & Miles** | Wallet, Bank, Nook Miles, House loan, with a stepper (±1,000 / ±100,000, ×10 after holding 2 s), typed values, max, 0, revert. A warning appears above the game's wallet max (99,999). |
| **Loadouts** | 5 pocket setups on the SD card, with a preview. Loading one is a pending change like any other. |
| **System** | Restore SD backup, Restore start-of-session backup, Save Doctor (red, hold A), reload favorites and 1/slot rules, Info, Quit. |

**Example: 10 Nook Miles Tickets ×10.** **R** (Favorites) > select *Nook Miles Ticket* > **A** > *Take with quantity…* > set 10 > **A**. You're back in Pockets: press **Y** ten times, then **+ > Apply**.

## SD card files

Everything lives in `sdmc:/switch/acnh_editor/` (same folder as upstream, so backups and loadouts are shared).

| File | Who writes it | Purpose |
|---|---|---|
| `acnh_editor.nro`, `font.ttf`, `items.txt`, `icon.png` | you (install) | App, font, item names (line number = item ID) |
| `ui_icons.png` | you (install) | Star / check / warning / lock icons. Optional: text fallbacks are used without it. |
| `nonstackable.txt` | you | Optional rules for `1/slot` items (format below) |
| `user_favorites.txt` | the app | Your favorites. `.tmp` during a rewrite, `.bak` = your previous file. |
| `backup_personal.dat`, `backup_main.dat` | the app (upstream engine) | Console state before the last Apply |
| `session_personal.dat`, `session_main.dat` | the app | Console state before the first write of the current session |
| `damaged_*.dat` | the app (Save Doctor) | Copy made before a repair |
| `l_0.dat` … `l_4.dat`, `layout_*.dat` | the app (upstream) | Loadouts, save region layout cache |

**`user_favorites.txt`** (managed by the app, safe to edit by hand):
```text
+16A2 Nook Miles Ticket     added favorite (the hex ID is authoritative, the name is a hint)
-0A4E gold roses            built-in favorite you hid
```
Bad lines are ignored and counted (System > Info). If `items.txt` is missing, the file is never rewritten. Limit: 64 favorites.

**`nonstackable.txt`** (one rule per line, applied top to bottom):
```text
golden axe     exact item name (case-insensitive)
0x0A1B         item ID
~wand          every name containing "wand"
-Wandering     remove what a rule matched
```
Tools (axes, nets, rods, shovels, watering cans, slingshots, ladder, vaulting pole) are always `1/slot`. The shipped file adds wands, saplings and sprouts.

## Installation

1. Get the `switch/acnh_editor/` folder (release zip or CI artifact) and copy it to the root of your SD card, replacing the upstream `acnh_editor.nro` if you have it.
2. Fully close Animal Crossing (HOME > X > Close).
3. Hold **R** while starting Animal Crossing to open the Homebrew Menu, then launch **ACNH Power Pockets**. Never launch it from the Album.

## Building

Requires devkitPro with `switch-dev`, `switch-sdl2` and `switch-sdl2_ttf`:
```text
make
```
Every push also builds the `.nro` in CI (`.github/workflows/build.yml`, `devkitpro/devkita64` container) and uploads a ready-to-copy SD layout.

Checks that run without a Switch:
```text
python3 tools/check_engine_untouched.py      # engine identical to upstream (needs the upstream remote)
sh tests/host/run.sh                         # model unit tests + syntax check against libnx/SDL2 stubs
sh tests/host/ui_shots.sh                    # renders the real UI with a fake engine, checks flows, saves screenshots
```
`ui_shots.sh` needs SDL2 + SDL2_ttf on the host (`brew install sdl2 sdl2_ttf`) and Pillow.

Code map: the engine stays in `source/main.cpp`. `source/pp_model.*` holds the testable logic (slots, pending changes, favorites, 1/slot rules). `source/pp_ui.*` draws the panels. `source/pp_strings.h` has every on-screen string. The design and UX specs are in `docs/design/CHARTE.md` and `docs/ux/UX_SPEC.md`. The engine audit is in `docs/engine/AUDIT_ECRITURE.md` (in French).

## Changelog

### 1.0.0 (fork of ACNH-Save-Editor 1.4.1)
- New panel interface (Pockets, Favorites, Catalog, Bells & Miles, Loadouts, System) navigated with L/R, with action menus, steppers, dialogs and toasts.
- Prepare-then-apply model: Review screen (before → after), Apply in one write, Discard without writing, confirmation when quitting with pending changes.
- "Item in hand": take an item from Favorites, Catalog, a slot (pipette) or a typed hex ID. **Y** places it in the next empty slot. *Fill empty slots* fills them all.
- Placing never overwrites an occupied slot, and writes a clean 8-byte record. Clearing writes the standard empty record.
- Favorites managed in the app (`user_favorites.txt` overlay over the 22 built-ins, atomic rewrite, corrupted lines ignored, 64 max). Unresolved built-ins are hidden instead of showing ID FFFE.
- `1/slot` items: badge, no quantity prompt, neutral value 0 written; `nonstackable.txt` rules; odd quantities already in the save are flagged, never changed.
- Restore and Save Doctor moved to the System panel: hold-to-confirm, blocked while changes are pending; backup validated before restoring; start-of-session backup; read-only mode for unknown save sizes.
- System > Info diagnostics (read-only): save layout, matching region hashes, backups present, favorites and 1/slot counts.
- ZL / ZR no longer trigger anything. Missing `font.ttf` now shows a clear message instead of a blank screen.
- Fixed upstream issues: crash risk with an empty favorites list (modulo by zero); a truncated loadout file no longer overwrites part of the pockets.

## Upstream roadmap
The upstream project's history and plans are in [ROADMAP.md](ROADMAP.md) (kept as-is from ACNH-Save-Editor).

## Credits
* **cbreezy210**, author of [ACNH-Save-Editor](https://github.com/cbreezy210/ACNH-Save-Editor): the save engine and everything this fork builds on.
* **kwsch and the NHSE project**: ACNH save-structure research — https://github.com/kwsch/NHSE
* **devkitPro and libnx communities**: Switch homebrew tooling — https://devkitpro.org
* **SDL2, SDL_ttf and stb_image authors**; **Varela Round** font (SIL OFL 1.1).
* **micaturtle** and the ACNH homebrew community, for testing and feedback on the upstream project.

## License
MIT, like upstream. See [LICENSE](LICENSE).
