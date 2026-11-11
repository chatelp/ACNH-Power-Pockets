# ACNH Power Pockets

> [!CAUTION]
> **This tool writes directly to your Animal Crossing: New Horizons save. It can corrupt it, and a corrupted island may be impossible to recover.**
>
> - **Back up your save with [JKSV](https://github.com/J-D-K/JKSV) or [Checkpoint](https://github.com/BernardoGiordano/Checkpoint) before every editing session, and copy that backup to a computer.** The app's own SD backups are a safety net, not a replacement.
> - **Version 1.0.0 has not been validated on real hardware yet.** It has been tested on a computer only, against a simulated engine. Do the [first-use test](#first-use-test) on a save you can afford to lose before touching your real island.
> - The save engine inherited from upstream **does not recompute the save's integrity hashes** (see [Known limitations](#known-limitations)). Whether the game tolerates this on your version is not proven.
> - Offline use only. Editing saves can get you into trouble online and may break Nintendo's terms of service. Never trade edited or unreleased items with other players.
> - Provided **as is, with no warranty of any kind** (MIT license). You alone are responsible for what happens to your save, your console and your accounts. Not affiliated with or endorsed by Nintendo.

**ACNH Power Pockets fills your pockets fast and shows you every change before writing a single byte.** It runs on the Switch itself (homebrew, Atmosphère) and edits the 20 pocket slots, Bells, Nook Miles and the house loan of the primary resident.

It is a fork of [ACNH-Save-Editor](https://github.com/cbreezy210/ACNH-Save-Editor) by cbreezy210: same save engine, new interface built around inventory work. See [Where it comes from](#where-it-comes-from).

![Pockets panel after filling 10 slots](docs/screenshots/pockets.png)

---

## Contents

- [What you get](#what-you-get)
- [First-use test](#first-use-test)
- [Install](#install)
- [Using it](#using-it)
- [How your save is protected](#how-your-save-is-protected)
- [Known limitations](#known-limitations)
- [Files on the SD card](#files-on-the-sd-card)
- [Troubleshooting](#troubleshooting)
- [Building and testing](#building-and-testing)
- [Where it comes from](#where-it-comes-from)
- [Contributing](#contributing)
- [License and credits](#license-and-credits)

## What you get

- **Ten slots in ten presses.** Pick an item once (favorite, search, or an item already in your pockets). Each press of **Y** drops it in the next empty slot.
- **Nothing is written until you say so.** Every edit stays in memory. The **Review** screen lists each change as *before » after*. **Apply** writes everything in one go, **Discard** throws it all away.
- **Your items are never overwritten by accident.** Placing only targets empty slots. Replacing an item takes an explicit *Clear slot* first.
- **Favorites you manage yourself.** Star or unstar any item from the app. Your list lives in `user_favorites.txt` on the SD card, on top of 22 built-in favorites.
- **Honest quantities.** Tools and other items that never stack carry a `1/slot` badge. The app doesn't ask for a quantity it can't deliver. You can extend the list in `nonstackable.txt`.
- **Dangerous actions are hard to trigger.** Restoring a backup and the Save Doctor write to the console immediately. They live in the System panel, in red, need **A** held for 1.5 seconds, and are blocked while you have unapplied changes.

| | |
|---|---|
| ![Action menu](docs/screenshots/slot-menu.png) | ![Review screen](docs/screenshots/review.png) |
| ![Catalog search](docs/screenshots/catalog.png) | ![Hold-to-confirm dialog](docs/screenshots/danger-dialog.png) |

## First-use test

Do this once, with a save you can afford to lose. It answers two questions nobody has checked yet on this version: does the game accept a save written by this tool, and how does the game count stacks?

1. Back up the save with JKSV or Checkpoint and copy the backup to a computer.
2. Launch Power Pockets. Put **Nook Miles Ticket** in an **empty** slot with quantity **9** (*Favorites* › *Take with quantity…* › 9, then **Y** in *Pockets*). Press **+**, then **Apply**.
3. Start the game:
   - **The save doesn't load, or the game offers to restore it:** stop. Restore your JKSV/Checkpoint backup and [open an issue](../../issues) with your game version.
   - **You get 10 tickets:** the game stores "quantity − 1". Displayed quantities are off by one until `kCountZeroBased` is set to `true` in `source/pp_model.h`. Please report it.
   - **You get 9 tickets:** quantities are exact.
4. Save in the game, quit, reopen Power Pockets and look at **System › Info › Region hashes**. Report what you see. It tells us whether the game repairs the hashes on its own.

## Install

Requirements: a Switch running Atmosphère, and Animal Crossing: New Horizons **3.0.x** (other save versions open read-only).

1. Copy the `switch/acnh_editor/` folder to the root of the SD card. It contains `acnh_editor.nro`, `font.ttf`, `items.txt`, `icon.png`, `ui_icons.png` and `nonstackable.txt`. Builds come from the [Actions tab](../../actions) (artifact `acnh-power-pockets`) until releases are published.
2. Fully close the game: HOME › highlight Animal Crossing › **X** › Close. A suspended game overwrites your edits on its next autosave.
3. Hold **R** while starting Animal Crossing to open the Homebrew Menu, then launch **ACNH Power Pockets**. Launching from the Album is refused: it doesn't give the app enough access.

The folder name is `acnh_editor`, the same as upstream ACNH-Save-Editor. Power Pockets replaces it and reuses its backups, loadouts and settings. Don't run both apps side by side.

## Using it

### Buttons

| Button | Does |
|---|---|
| **L / R** | Switch panel |
| **D-pad / left stick** | Move (hold to repeat) |
| **A** | Open the action menu for the selected thing |
| **Y** | Its default action (the first line of that menu) |
| **B** | Close a menu, dialog or number editor |
| **+** | Review pending changes, then Apply / Discard / Quit |
| **−** | Discard all pending changes |
| **ZL, ZR, X** | Nothing, on purpose |

### Panels

| Panel | For |
|---|---|
| **Pockets** | The 20 slots. **Y** places the item in hand in the next empty slot (or picks up the item under the cursor when your hand is empty). Menu: *Fill empty slots*, *Take in hand*, *Quantity…*, *Held quantity…*, *Enter item ID (hex)…*, *Add/Remove favorite*, *Revert to original*, *Clear slot*. |
| **Favorites** | Your favorites, A to Z. **Y** takes one and jumps back to Pockets. |
| **Catalog** | Search all ~15,000 items by name or 4-digit hex ID. |
| **Bells & Miles** | Wallet, Bank, Nook Miles, House loan. Step with the D-pad (±1,000 / ±100,000, ×10 after holding 2 s) or type a value. |
| **Loadouts** | Five saved pocket setups on the SD card. Loading one counts as a pending change. |
| **System** | Restore backups, Save Doctor, reload your lists, diagnostics, quit. |

### Recipes

- **Ten stacks of Nook Miles Tickets:** **R** › *Nook Miles Ticket* › **A** › *Take with quantity…* › 10 › **A**, then **Y** ×10, then **+** › **Apply**.
- **Copy something you already carry:** in *Pockets*, select it and press **Y** with an empty hand to pick it up, then **Y** again to place copies.
- **Fill every free slot:** take an item, then **A** › *Fill empty slots*.
- **Undo one slot:** **A** › *Revert to original*. **Undo everything:** **−**.
- **Add a favorite:** find the item in *Catalog* › **A** › *Add to favorites*.

## How your save is protected

**Every Apply:**
1. The first time in a session, copies the console's current save to `session_*.dat` on the SD card. This copy isn't overwritten again during that session.
2. Hands your changes to the unchanged upstream engine. The engine first writes `backup_*.dat` (the state before this Apply), then writes and commits the save.
3. Reloads the "original" state only if every step succeeded. If anything fails, your changes stay pending and the app says what happened.

**Before any restore**, the backup is checked on a decrypted copy: size and value checksums must match this save, otherwise the restore is refused. A backup that wasn't written completely is never offered.

**What this does *not* protect you from:** a game version with a different save layout (the app goes read-only when file sizes are unexpected, but that check is not exhaustive), the integrity hashes noted below, power loss at the wrong moment, or SD card failure. That is why the JKSV/Checkpoint backup at the top of this page matters.

## Known limitations

- **Integrity hashes are not recomputed.** The save is split into regions protected by Murmur3 hashes. The engine finds them but doesn't update them after an edit. This is inherited from upstream and deliberately left alone (this fork doesn't touch the engine). System › Info shows how many hashes match.
- **Quantity convention unconfirmed:** see the [first-use test](#first-use-test).
- **Only the primary resident** (`Villager0`) and **only the 20 pocket slots** (not the bag expansion).
- **Save versions 3.0.x only**; anything else is read-only.
- Item names are English, from `items.txt`.

## Files on the SD card

All in `sdmc:/switch/acnh_editor/`.

| File | Written by | What it is |
|---|---|---|
| `user_favorites.txt` | the app | Your favorites. Plain text, safe to edit. `.bak` keeps your previous version. |
| `nonstackable.txt` | you | Extra `1/slot` rules (optional) |
| `backup_personal.dat`, `backup_main.dat` | the app | Save as it was before the last Apply |
| `session_personal.dat`, `session_main.dat` | the app | Save as it was before the first write of the current session |
| `damaged_*.dat` | Save Doctor | Copy taken before a repair |
| `l_0.dat` … `l_4.dat` | the app | Loadouts |
| `layout_*.dat` | the app | Cache of the save's region layout |

`user_favorites.txt` format:
```text
+16A2 Nook Miles Ticket     a favorite you added (the hex ID counts, the name is a reminder)
-0A4E gold roses            a built-in favorite you removed
```

`nonstackable.txt` format (rules apply top to bottom):
```text
golden axe     exact item name, case-insensitive
0x0A1B         item ID
~wand          every item whose name contains "wand"
-Wandering     undo a match
```
Tools (axes, nets, fishing rods, shovels, watering cans, slingshots, ladder, vaulting pole) are always `1/slot`. The shipped file adds wands, saplings and sprouts.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "The save can't be opened" | The game is still running or suspended. Close it fully (HOME › X › Close), then relaunch through the game holding **R**. |
| Red "missing file" text at launch | `font.ttf` isn't in `sdmc:/switch/acnh_editor/`. |
| Names show as `ID 16A2` | `items.txt` is missing. Editing still works; favorites become read-only. |
| "Read-only: unsupported save version" | Your save isn't the 3.0.x layout this engine knows. Nothing will be written. |
| Changes vanished in the game | The game was suspended while you edited, or you quit without **Apply**. |
| Restore is greyed out | Apply or discard your pending changes first, or the last backup wasn't written completely (System › Info). |

## Building and testing

You need devkitPro with `switch-dev`, `switch-sdl2` and `switch-sdl2_ttf`. Then run:
```sh
make
```
Each push builds the `.nro` in GitHub Actions (`devkitpro/devkita64` container) and uploads the SD-card folder as an artifact.

You can check the project without a Switch:
```sh
git remote add upstream https://github.com/cbreezy210/ACNH-Save-Editor.git && git fetch upstream
python3 tools/check_engine_untouched.py upstream/main   # proves the save engine is byte-identical to upstream
sh tests/host/run.sh                                     # unit tests + compile check against libnx/SDL2 stubs
sh tests/host/ui_shots.sh                                # renders the real UI with a fake engine, checks flows, saves screenshots
```
`ui_shots.sh` needs SDL2, SDL2_ttf (`brew install sdl2 sdl2_ttf`) and Python Pillow.

| Path | Contents |
|---|---|
| `source/main.cpp` | Upstream save engine (unchanged) and the wiring to the UI |
| `source/pp_model.*` | Testable logic: slots, pending changes, favorites, 1/slot rules |
| `source/pp_ui.*`, `source/pp_strings.h` | Panels, dialogs, every on-screen string |
| `docs/design/` | Visual design system and HTML mockups |
| `docs/ux/UX_SPEC.md` | Interaction spec |
| `docs/engine/AUDIT_ECRITURE.md` | Read-only audit of the save-writing code |

The documents in `docs/` are written in French.

## Where it comes from

Power Pockets starts from [ACNH-Save-Editor](https://github.com/cbreezy210/ACNH-Save-Editor) v1.4.1 (commit `731c228`) by cbreezy210, a native Switch save editor for Animal Crossing. Everything that reads, decrypts, encrypts and writes the save is upstream code. A script checks on every push that it is unchanged.

What changed is everything you see and touch:

| ACNH-Save-Editor | ACNH Power Pockets |
|---|---|
| **A** saves to the console immediately | Changes are prepared, reviewed, then applied together |
| One slot per save | Any number of slots per Apply, **Y** fills the next empty slot |
| Favorites compiled into the app | Favorites editable in the app, stored on the SD card |
| Quantity asked for every item | `1/slot` items flagged, no quantity asked |
| **ZL** restores in one press, **ZR ZR** runs the Doctor | Both in the System panel, hold-to-confirm, blocked with pending changes |
| One rolling backup | Rolling backup plus a start-of-session backup, both validated before restoring |

Upstream's own history and plans: [ROADMAP.md](ROADMAP.md) (kept unchanged).

## Contributing

The most useful contribution right now is a **first-use test report**. Open an issue with your game version, your firmware/Atmosphère version, how many tickets you got, and what System › Info showed.

Pull requests are welcome for the interface, tests and docs. Changes to the save engine (`source/main.cpp` engine blocks) need a strong case and a way to test them. `tools/check_engine_untouched.py` fails on purpose when the engine changes.

## License and credits

MIT. See [LICENSE](LICENSE). Copyright cbreezy210 (upstream) and the Power Pockets contributors.

- **cbreezy210** — [ACNH-Save-Editor](https://github.com/cbreezy210/ACNH-Save-Editor), the save engine this project runs on.
- **kwsch and [NHSE](https://github.com/kwsch/NHSE)** — the ACNH save-format research everything here relies on.
- **[devkitPro](https://devkitpro.org) / libnx**, **SDL2**, **SDL_ttf**, **stb_image**.
- **Varela Round** font (SIL Open Font License 1.1).
