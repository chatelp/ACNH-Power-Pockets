# Third-party notices

ACNH Power Pockets is released under the MIT license (see `LICENSE`). A few files that it
ships are not under that license. They are separate files that the app reads at runtime;
each keeps its own license, reproduced in `licenses/`.

| File | Origin | License |
|---|---|---|
| `assets/items.txt` (shipped as `items.txt`) | English item names table `NHSE.Core/Resources/text/en/text_item_en.txt` of [NHSE](https://github.com/kwsch/NHSE) by kwsch and contributors, unchanged. Inherited from upstream ACNH-Save-Editor. | GNU GPL v3.0 — `licenses/GPL-3.0.txt`. Its source form is the file itself. |
| `assets/font.ttf` (shipped as `font.ttf`) | **Varela Round** 3.010. Copyright 2023 The Varela Round Project Authors (https://github.com/alefalefalef/Varela-Round-Hebrew/), with Reserved Font Names "Varela" and "Varela Round". | SIL Open Font License 1.1 — `licenses/OFL-1.1-VarelaRound.txt` |
| `source/stb_image.h` | [stb](https://github.com/nothings/stb) by Sean Barrett | Public domain or MIT, at your choice (notice at the end of the file) |
| `source/main.cpp` (save engine) | [ACNH-Save-Editor](https://github.com/cbreezy210/ACNH-Save-Editor) by cbreezy210 | MIT, copyright notice kept in `LICENSE` |

Item names are text from Animal Crossing: New Horizons, a game by Nintendo. This project is
not affiliated with or endorsed by Nintendo.

The logo, icons and 98 item pictures were drawn for this project (`tools/make_logo.py`,
`tools/make_icons.py`, `tools/make_item_glyphs.py`) and are under the project's MIT license.
