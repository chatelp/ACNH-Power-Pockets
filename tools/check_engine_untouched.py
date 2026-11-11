#!/usr/bin/env python3
"""Proves that the save engine inherited from ACNH-Save-Editor is unchanged.

Each engine zone (upstream line ranges, upstream commit 731c228) must appear
byte-for-byte, as one contiguous block, in the current source/main.cpp, with
ONE allowed difference: the name of the app's SD folder inside the path strings
of the engine's own files (backup_*.dat, damaged_*.dat, layout_*.dat). Power
Pockets keeps its files in sdmc:/switch/acnh_power_pockets/ so that nothing is
shared with an upstream install. The script applies that exact rename to the
upstream text before comparing, and fails if the folder name appears anywhere
else than in such a path string, or a different number of times than expected.
The restore block and the Save Doctor repair pass are allowed to have MOVED
(into lambdas) but not to have changed. It also checks that no new code
writes to the save: the number of NAND write / commit call sites is the same
as upstream, doSave() is called from exactly one place, and the pp_* UI files
never touch the engine.

Usage: python3 tools/check_engine_untouched.py [upstream-ref]   (default upstream/main)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = sys.argv[1] if len(sys.argv) > 1 else "upstream/main"

ZONES = [
    ((16, 139), "XorShift/AES CryptACNH, Murmur3, LE helpers, EncInt/DecInt, offsets"),
    ((177, 183), "applyEdit"),
    ((185, 191), "readSaveFile"),
    ((193, 209), "bakMsg + backupToSd"),
    ((509, 591), "mount, read, decrypt, regions, layout cache, decode g_vals"),
    ((620, 663), "doSave"),
    ((721, 768), "restore block (moved verbatim)"),
    ((779, 812), "Save Doctor repair pass (moved verbatim)"),
]

up = subprocess.run(["git", "-C", str(ROOT), "show", f"{REF}:source/main.cpp"],
                    check=True, capture_output=True, text=True).stdout
cur = (ROOT / "source" / "main.cpp").read_text(encoding="utf-8")
up_lines = up.split("\n")

# The only allowed edit inside the engine zones: the SD folder name in path strings.
UP_DIR, OUR_DIR = "switch/acnh_editor/", "switch/acnh_power_pockets/"
PATH_LITERAL = re.compile(r'"(?:sdmc:)?/switch/acnh_editor/(?:backup|damaged|layout)_(?:personal|main)\.dat"')
EXPECTED_PATHS = 10     # 2 backup (write) + 4 layout + 2 backup (restore) + 2 damaged

failed = False
renamed = 0
for (a, b), name in ZONES:
    block = "\n".join(up_lines[a - 1:b])
    paths = len(PATH_LITERAL.findall(block))
    if block.count(UP_DIR) != paths:          # the folder name outside a known path string
        failed = True
        print(f"[FAIL] lines {a:>3}-{b:<3} {name}: unexpected use of the SD folder name")
        continue
    renamed += paths
    n = cur.count(block.replace(UP_DIR, OUR_DIR))
    status = "OK " if n == 1 else "FAIL"
    if n != 1:
        failed = True
    note = f", SD folder renamed in {paths} path string{'s' if paths != 1 else ''}" if paths else ""
    print(f"[{status}] lines {a:>3}-{b:<3} {name}  (found {n}x{note})")

ok = renamed == EXPECTED_PATHS and UP_DIR not in cur
failed |= not ok
print(f"[{'OK ' if ok else 'FAIL'}] SD folder: {renamed} engine path strings renamed (expected {EXPECTED_PATHS}), "
      f"old folder name {'absent from' if UP_DIR not in cur else 'STILL IN'} main.cpp")


def count(text, pat):
    return len(re.findall(pat, text))


checks = [
    ("fsFsCommit call sites", r"fsFsCommit\(", None),
    ("fsFileWrite call sites", r"fsFileWrite\(", None),
    ("fsFsOpenFile with FsOpenMode_Write", r"FsOpenMode_Write", None),
    ("CryptACNH call sites (+2 allowed: backup check decrypts 2 malloc'd copies)", r"CryptACNH\(", 2),
    ("applyEdit call sites", r"applyEdit\(", None),
]
for label, pat, extra in checks:
    u, c = count(up, pat), count(cur, pat)
    ok = c == u + (extra or 0)
    failed |= not ok
    print(f"[{'OK ' if ok else 'FAIL'}] {label}: upstream {u}, now {c}")

calls = count(cur, r"\bdoSave\(\);")
ok = calls == 1
failed |= not ok
print(f"[{'OK ' if ok else 'FAIL'}] doSave() called from exactly one place: {calls}")

FORBIDDEN_IN_UI = ("fsFsCommit(", "fsFileWrite(", "FsOpenMode_Write", "fsOpenSaveDataFileSystem",
                   "CryptACNH(", "applyEdit(", "doSave(", "writeNand", "origP", "origM", "mainData")
for f in sorted((ROOT / "source").glob("pp_*")):
    code = re.sub(r"//[^\n]*|/\*.*?\*/", "", f.read_text(encoding="utf-8"), flags=re.S)  # ignore comments
    bad = [w for w in FORBIDDEN_IN_UI if w in code]
    ok = not bad
    failed |= not ok
    print(f"[{'OK ' if ok else 'FAIL'}] {f.name} never touches the engine" + ("" if ok else f": {bad}"))

print()
print("ENGINE UNTOUCHED (apart from the SD folder name in its path strings)" if not failed else "ENGINE CHECK FAILED")
print("Visual check of the moves: git diff -w --color-moved=zebra", REF, "-- source/main.cpp")
sys.exit(1 if failed else 0)
