#!/usr/bin/env python3
"""Builds assets/item_collections.txt: themed collections of items for the Catalog
("Browse collections") and the catalog type of each item (the ZL / ZR filter).

Source: the community ACNH data spreadsheet ("Data Spreadsheet for Animal Crossing
New Horizons"), as JSON in Norviah/animal-crossing (https://github.com/Norviah/animal-crossing,
MIT). One file is read: json/combined/Items.min.json at the pinned commit below.

Usage:
    python3 tools/make_item_collections.py <Items.min.json>            writes assets/item_collections.txt
    python3 tools/make_item_collections.py <Items.min.json> --check    compares, writes nothing
    python3 tools/make_item_collections.py <Items.min.json> --list "Japanese, traditional"
                                                                       prints the items of one collection

Download:
    curl -fsSL -o Items.min.json \\
      https://raw.githubusercontent.com/Norviah/animal-crossing/<NORVIAH_COMMIT>/json/combined/Items.min.json

Only ids whose name in items.txt matches the spreadsheet (base name, without the
" (variant)" part) are kept, so a collection never shows an unrelated item.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEMS = os.path.join(ROOT, "assets", "items.txt")
OUT = os.path.join(ROOT, "assets", "item_collections.txt")
NORVIAH_COMMIT = "8bec58729a10879d575ba18d50a1dda74804a374"   # v8.2.0, 2022-01-07
SHEET_ID = "1mo7myqHry5r_TKvakvIhHbcEAEQpSiNoNQoIS8sMpvM"     # the spreadsheet Norviah converted

# Catalog type of each spreadsheet sheet. Sheets not listed are "other".
SHEET_TYPE = {
    "Housewares": "furniture", "Miscellaneous": "furniture", "Wall-mounted": "furniture",
    "Ceiling Decor": "furniture", "Photos": "furniture", "Posters": "furniture",
    "Gyroids": "furniture", "Artwork": "furniture",
    "Wallpaper": "surfaces", "Floors": "surfaces", "Rugs": "surfaces",
    "Tops": "clothing", "Bottoms": "clothing", "Dress-Up": "clothing", "Headwear": "clothing",
    "Accessories": "clothing", "Socks": "clothing", "Shoes": "clothing", "Bags": "clothing",
    "Umbrellas": "clothing", "Clothing Other": "clothing",
}
TYPES = ("furniture", "surfaces", "clothing")
CLOTHING = {s for s, t in SHEET_TYPE.items() if t == "clothing"}
# Message cards carry ids of other items in this version of the data: never used.
SKIP_SHEETS = {"Message Cards"}

# "Japanese, traditional": hand-made from the spreadsheet, because no single field
# covers it. Furniture: the "Japanese Style" tag and three series. Clothing has no
# style tag, so it is picked by name (whole words).
JP_SERIES = {"imperial", "bamboo", "cherry blossoms"}
JP_CLOTHING = re.compile(
    r"\b(kimono|yukata|hakama|jinbei|happi|zori|geta|tabi|samurai|ninja|noh|kabuki|"
    r"junihitoe|bamboo hat|sushi chef|zen|old commoner|cherry-blossom (umbrella|pochette))\b", re.I)
JP_EXCLUDE = re.compile(r"Kerokerokeroppi|Hello Kitty|Lunar|Chinese|zodiac|Moroccan", re.I)


def read(path):
    with open(path, "r", encoding="utf-8-sig") as f:
        return f.read()


def base(name):
    return re.sub(r"\s*\([^()]*\)$", "", name).strip().lower()


def cap(s):
    return s[:1].upper() + s[1:]


def as_list(v):
    if isinstance(v, list):
        return v
    return [v] if v else []


def item_ids(it, names, dropped):
    """Ids of an item and of its variants that items.txt names the same way."""
    out = []
    for v in (it.get("variations") or [it]):
        i = v.get("internalId", it.get("internalId"))
        if i is None or i in out:
            continue
        if 0 <= i < len(names) and names[i] and base(names[i]) == base(it["name"]):
            out.append(i)
        else:
            dropped.add(i)
    return out


def build(data, names):
    groups = [("Styles", {}), ("Furniture series", {}), ("HHA themes", {}),
              ("Clothing styles", {}), ("Clothing themes", {})]
    g = dict(groups)
    jp = g["Styles"].setdefault("Japanese, traditional", set())
    types = {}
    dropped = set()
    for it in data:
        sheet = it.get("sourceSheet")
        if sheet in SKIP_SHEETS:
            continue
        ids = item_ids(it, names, dropped)
        if not ids:
            continue
        t = SHEET_TYPE.get(sheet)
        if t:
            for i in ids:
                types.setdefault(i, t)
        for s in as_list(it.get("series")):
            g["Furniture series"].setdefault(cap(s), set()).update(ids)
        for c in as_list(it.get("concepts")):
            g["HHA themes"].setdefault(cap(c), set()).update(ids)
        if sheet in CLOTHING:
            for s in as_list(it.get("styles")):
                g["Clothing styles"].setdefault(cap(s), set()).update(ids)
            for th in as_list(it.get("themes")):
                g["Clothing themes"].setdefault(cap(th), set()).update(ids)
            if JP_CLOTHING.search(it["name"]):
                jp.update(ids)
        elif (it.get("tag") == "Japanese Style" or set(as_list(it.get("series"))) & JP_SERIES) \
                and not JP_EXCLUDE.search(it["name"]):
            jp.update(ids)
    return groups, types, dropped


def ranges(ids):
    ids = sorted(ids)
    out, i = [], 0
    while i < len(ids):
        j = i
        while j + 1 < len(ids) and ids[j + 1] == ids[j] + 1:
            j += 1
        out.append(("%04X" % ids[i]) if i == j else ("%04X-%04X" % (ids[i], ids[j])))
        i = j + 1
    return out


def wrap(tokens, width=96):
    lines, cur = [], ""
    for t in tokens:
        if cur and len(cur) + 1 + len(t) > width:
            lines.append(cur)
            cur = t
        else:
            cur = (cur + " " + t) if cur else t
    if cur:
        lines.append(cur)
    return lines


def render(groups, types, dropped):
    n_coll = sum(len(c) for _, c in groups)
    lines = [
        "# ACNH Power Pockets - item collections (Catalog > Browse collections) and the",
        "# catalog type of each item (the ZL / ZR filter of the Catalog).",
        "# Copy this file to sdmc:/switch/acnh_power_pockets/ next to items.txt.",
        "#",
        "# Generated by tools/make_item_collections.py from the community \"Animal Crossing: New",
        "# Horizons\" data spreadsheet (docs.google.com/spreadsheets/d/" + SHEET_ID + "),",
        "# as JSON in Norviah/animal-crossing (github.com/Norviah/animal-crossing, MIT),",
        "# commit %s." % NORVIAH_COMMIT,
        "# %d collections; ids whose name in items.txt differs from the spreadsheet are left out (%d)."
        % (n_coll, len(dropped)),
        "#",
        "# Lines:",
        "#   group <name>         a heading of the collection list",
        "#   collection <name>    a collection, in the last group; the id lines below belong to it",
        "#   type <name>          furniture, surfaces or clothing; the id lines below belong to it",
        "#                        (ids of no type are \"other\")",
        "#   <id> <id>-<id> ...   ids in hex, as in items.txt, single or inclusive ranges",
    ]
    for gname, colls in groups:
        lines.append("")
        lines.append("group " + gname)
        for cname in sorted(colls, key=str.lower):
            lines.append("collection " + cname)
            lines.extend("  " + ln for ln in wrap(ranges(colls[cname])))
    for t in TYPES:
        lines.append("")
        lines.append("type " + t)
        lines.extend("  " + ln for ln in wrap(ranges(i for i, v in types.items() if v == t)))
    return "\n".join(lines) + "\n"


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        sys.exit(__doc__)
    with open(args[0], "r", encoding="utf-8") as f:
        data = json.load(f)
    names = read(ITEMS).split("\n")
    groups, types, dropped = build(data, names)
    if "--list" in args:
        want = args[args.index("--list") + 1]
        for _, colls in groups:
            if want in colls:
                for i in sorted(colls[want], key=lambda i: names[i].lower()):
                    print("%04X %s" % (i, names[i]))
        return
    text = render(groups, types, dropped)
    if "--check" in args:
        same = os.path.exists(OUT) and read(OUT) == text
        print("item_collections.txt " + ("up to date" if same else "DIFFERS"))
        sys.exit(0 if same else 1)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", OUT, {g: len(c) for g, c in groups},
          {t: sum(1 for v in types.values() if v == t) for t in TYPES}, "dropped", len(dropped))


if __name__ == "__main__":
    main()
