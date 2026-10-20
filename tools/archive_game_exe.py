"""Keep a copy of the game executable before Steam overwrites it.

    python tools\\archive_game_exe.py
    python tools\\archive_game_exe.py --exe "D:\\...\\EliteDangerous64.exe"
    python tools\\archive_game_exe.py --self-test

A game update replaces EliteDangerous64.exe in place. Re-deriving EDVR's
addresses (tools\\build_diff.py map, docs\\game-binary-map.md) needs the old
build next to the new one, and once Steam has finished there is no old build
left on disk: the 332841 -> 334473 remap of 2026-10-20 only worked because a
REA session still held its own copy. Run this after every flight-ready build
and before letting Steam update.

It copies the installed exe to analysis\\EliteDangerous64-<build>.exe, the
name build_diff.py's examples use. analysis\\ is git-ignored: game files are
never committed. The build is the last field of VersionInfo.txt's "Version"
beside the exe (2026.10.13.334473 -> 334473). The copy is written to a temp
name, hashed, and renamed into place, so a partial copy never carries the
final name.

  * the destination already holds the same bytes -> "already archived", 0;
  * the destination holds different bytes under the same build -> refused,
    1, nothing written (a mislabelled or modified copy needs a person);
  * no VersionInfo.txt or no build in it -> refused, 1, unless --build.

Without --exe it searches every Steam library (the registry's SteamPath and
libraryfolders.vdf) and the Frontier launcher folder for Odyssey's
Products\\*\\EliteDangerous64.exe, and refuses when it finds none or several.
Nothing is launched or modified; only the archive file is written.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tempfile

GAME_EXE = "EliteDangerous64.exe"
ODYSSEY_PRODUCT = "elite-dangerous-odyssey-64"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DEST = os.path.join(REPO, "analysis")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pe_identity(path):
    """(TimeDateStamp, SizeOfImage): the pair EDVR's build gates check."""
    with open(path, "rb") as f:
        head = f.read(4096)
    if head[:2] != b"MZ" or len(head) < 0x40:
        raise ValueError("%s: not a PE file (no MZ header)" % path)
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    if pe + 24 + 60 > len(head) or head[pe:pe + 4] != b"PE\0\0":
        raise ValueError("%s: not a PE file (no PE signature)" % path)
    stamp = struct.unpack_from("<I", head, pe + 8)[0]
    size_of_image = struct.unpack_from("<I", head, pe + 24 + 56)[0]
    return stamp, size_of_image


def build_of(exe):
    """The build number from VersionInfo.txt beside the exe, or None."""
    info = os.path.join(os.path.dirname(exe), "VersionInfo.txt")
    try:
        with open(info, "r", encoding="utf-8-sig") as f:
            version = json.load(f).get("Version", "")
    except (OSError, ValueError):
        return None
    last = str(version).rsplit(".", 1)[-1]
    return last if last.isdigit() else None


def steam_libraries():
    roots = []
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
            roots.append(winreg.QueryValueEx(k, "SteamPath")[0])
    except (ImportError, OSError):
        pass
    libraries = []
    for root in roots:
        libraries.append(root)
        vdf = os.path.join(root, "steamapps", "libraryfolders.vdf")
        try:
            with open(vdf, "r", encoding="utf-8", errors="replace") as f:
                libraries.extend(library_paths(f.read()))
        except OSError:
            pass
    seen, unique = set(), []
    for lib in libraries:
        key = os.path.normcase(os.path.normpath(lib))
        if key not in seen:
            seen.add(key)
            unique.append(os.path.normpath(lib))
    return unique


def library_paths(vdf_text):
    """The "path" values of a libraryfolders.vdf, unescaped."""
    return [m.replace("\\\\", "\\") for m in re.findall(r'"path"\s+"((?:[^"\\]|\\.)*)"', vdf_text)]


def find_installed_exes():
    roots = [os.path.join(lib, "steamapps", "common", "Elite Dangerous") for lib in steam_libraries()]
    local = os.environ.get("LOCALAPPDATA", "")
    if local:
        roots.append(os.path.join(local, "Frontier_Developments"))
    found = []
    for root in roots:
        exe = os.path.join(root, "Products", ODYSSEY_PRODUCT, GAME_EXE)
        if os.path.isfile(exe):
            found.append(os.path.normpath(exe))
    return found


def archive(exe, dest_dir, build=None, out=print):
    """Copy exe to dest_dir/EliteDangerous64-<build>.exe. Returns an exit code."""
    build = build or build_of(exe)
    if not build:
        out("[archive] refused: no build number in VersionInfo.txt beside %s; pass --build." % exe)
        return 1
    stamp, size_of_image = pe_identity(exe)
    source_hash = sha256_of(exe)
    target = os.path.join(dest_dir, "EliteDangerous64-%s.exe" % build)
    identity = "build %s, sha256 %s, PE stamp 0x%08X, SizeOfImage 0x%X" % (
        build, source_hash, stamp, size_of_image)
    if os.path.exists(target):
        if sha256_of(target) == source_hash:
            out("[archive] already archived: %s (%s)" % (target, identity))
            return 0
        out("[archive] refused: %s exists with different bytes; the installed exe is %s. "
            "Check which copy is right before replacing it." % (target, identity))
        return 1
    os.makedirs(dest_dir, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=".archive-", suffix=".tmp", dir=dest_dir)
    os.close(fd)
    try:
        shutil.copyfile(exe, temp)
        if sha256_of(temp) != source_hash:
            out("[archive] refused: the copy's hash differs from %s (changed while copying?)" % exe)
            return 1
        os.replace(temp, target)
        temp = None
    finally:
        if temp and os.path.exists(temp):
            os.remove(temp)
    out("[archive] archived %s -> %s (%s)" % (exe, target, identity))
    return 0


def _fake_pe(path, stamp, size_of_image, filler=b""):
    pe = 0x80
    head = bytearray(0x200)
    head[0:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, pe)
    head[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<I", head, pe + 8, stamp)
    struct.pack_into("<I", head, pe + 24 + 56, size_of_image)
    with open(path, "wb") as f:
        f.write(bytes(head) + filler)


def self_test():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as td:
        game = os.path.join(td, "Products", ODYSSEY_PRODUCT)
        os.makedirs(game)
        exe = os.path.join(game, GAME_EXE)
        _fake_pe(exe, 0x6ACD513D, 0x640C000, b"one")
        with open(os.path.join(game, "VersionInfo.txt"), "w", encoding="utf-8") as f:
            f.write('{"Version":"2026.10.13.334473","executable":"EliteDangerous64.exe"}')
        dest = os.path.join(td, "analysis")
        lines = []

        check(build_of(exe) == "334473", "build_of reads the last VersionInfo field")
        check(pe_identity(exe) == (0x6ACD513D, 0x640C000), "pe_identity reads stamp and SizeOfImage")
        check(archive(exe, dest, out=lines.append) == 0, "first archive succeeds")
        target = os.path.join(dest, "EliteDangerous64-334473.exe")
        check(os.path.isfile(target) and sha256_of(target) == sha256_of(exe), "archive copies the exact bytes")
        check(archive(exe, dest, out=lines.append) == 0 and "already archived" in lines[-1],
              "re-archiving the same bytes is a no-op")

        _fake_pe(exe, 0x6ACD513D, 0x640C000, b"two")
        before = sha256_of(target)
        check(archive(exe, dest, out=lines.append) == 1 and "refused" in lines[-1],
              "different bytes under the same build are refused")
        check(sha256_of(target) == before, "a refused archive leaves the existing copy alone")
        check(not [n for n in os.listdir(dest) if n.startswith(".archive-")], "no temp file is left behind")

        os.remove(os.path.join(game, "VersionInfo.txt"))
        check(archive(exe, dest, out=lines.append) == 1, "a missing VersionInfo.txt is refused")
        check(archive(exe, dest, build="999", out=lines.append) == 0
              and os.path.isfile(os.path.join(dest, "EliteDangerous64-999.exe")), "--build overrides VersionInfo")

        not_pe = os.path.join(td, "not_pe.exe")
        with open(not_pe, "wb") as f:
            f.write(b"\0" * 64)
        try:
            pe_identity(not_pe)
            check(False, "a non-PE file is rejected")
        except ValueError:
            pass

    vdf = '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"C:\\\\Program Files (x86)\\\\Steam"\n\t}\n' \
          '\t"1"\n\t{\n\t\t"path"\t\t"D:\\\\SteamLibrary"\n\t}\n}\n'
    check(library_paths(vdf) == [r"C:\Program Files (x86)\Steam", r"D:\SteamLibrary"],
          "libraryfolders.vdf paths are unescaped")

    for f in failures:
        print("[archive] self-test FAIL: %s" % f)
    print("[archive] self-test %s" % ("FAILED" if failures else "passed"))
    return 1 if failures else 0


def main(argv=None):
    p = argparse.ArgumentParser(description="Copy the game exe to analysis\\ before an update replaces it.")
    p.add_argument("--exe", help="the EliteDangerous64.exe to archive (default: search the installs)")
    p.add_argument("--dest", default=DEFAULT_DEST, help="archive folder (default: analysis\\ in this checkout)")
    p.add_argument("--build", help="build number when VersionInfo.txt is missing")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args(argv)
    if args.self_test:
        return self_test()
    exe = args.exe
    if not exe:
        found = find_installed_exes()
        if len(found) != 1:
            print("[archive] refused: found %d Odyssey installs%s; pass --exe."
                  % (len(found), (":\n  " + "\n  ".join(found)) if found else ""))
            return 1
        exe = found[0]
    if not os.path.isfile(exe):
        print("[archive] refused: no such file: %s" % exe)
        return 1
    return archive(os.path.abspath(exe), os.path.abspath(args.dest), args.build)


if __name__ == "__main__":
    sys.exit(main())
