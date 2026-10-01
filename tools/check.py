"""Everything that can be checked without the board, in one go.

    python tools/check.py [--quick] [--no-device]
    python tools/check.py --compare out/play out/dev_play

  1. The puzzles (tools/puzzles/build_pack.py --check): every source sound,
     and the packed data in src/game/PuzzleData.cpp built from them.
  2. The host tests (tools/tests): the decoder against the Python reference,
     the rules and the score, saving, packs read from FAT card images.
  3. Every script in tools/scripts runs in the simulator twice: the two runs
     must draw identical frames (the game is deterministic), with no drawing
     into a frame still being sent (the simulator's BUG lines). Scripts named
     card_*.txt run with a card in the slot (out/card.img, made here).
  4. The release build compiles for the device and fits (tools/check_size.py).

Screenshots, contact sheets and GIFs are left in out/<script>/ to look at.

--compare A B: the images two runs of a script left (say the simulator's and
the board's, from tools/device.py run): the same frames, pixel for pixel?
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "out"


def run(cmd, **kw):
    return subprocess.run([sys.executable, *map(str, cmd)], capture_output=True, text=True, cwd=ROOT, **kw)


def frames_hash(path):
    """A hash of every frame's pixels (not of the file's bytes)."""
    h = hashlib.sha256()
    im = Image.open(path)
    try:
        while True:
            h.update(im.convert("RGB").tobytes())
            im.seek(im.tell() + 1)
    except EOFError:
        pass
    return h.hexdigest()


def make_card():
    """The card the card_* scripts play with: two packs made of built-in puzzles."""
    sys.path.insert(0, str(HERE / "puzzles"))
    sys.path.insert(0, str(HERE / "tests"))
    import cwformat as cw
    import mkcard
    from run_tests import builtin_pack
    _, blobs = cw.read_pack(builtin_pack())
    OUT.mkdir(exist_ok=True)
    mkcard.make(OUT / "card.img", {"BONUS.CWD": cw.make_pack("BONUS", blobs[2:5]),
                                   "EXTRA.CWD": cw.make_pack("EXTRA", blobs[:1])}, fs="fat16")
    return OUT / "card.img"


def drive(script, outdir, card=None):
    if outdir.exists():
        shutil.rmtree(outdir)
    cmd = [HERE / "chsim" / "chdrive.py", "--sim", ROOT]
    if card:
        cmd += ["--card", card]
    r = run(cmd + [script, outdir])
    return r.returncode == 0, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="skip the FAT32 card tests, and run each script once")
    ap.add_argument("--no-device", action="store_true", help="skip the device compile")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"), help="compare two runs' images")
    a = ap.parse_args()
    if a.compare:
        one, two = (Path(d) for d in a.compare)
        files = sorted(p.name for p in one.iterdir() if p.suffix in (".png", ".gif") and p.name != "sheet.png")
        diff = [f for f in files if not (two / f).exists() or frames_hash(one / f) != frames_hash(two / f)]
        print(f"{len(files)} images, {len(diff)} differ" + (": " + " ".join(diff) if diff else ""))
        sys.exit(1 if diff else 0)
    ok = True

    print("== puzzles")
    r = run([HERE / "puzzles" / "build_pack.py", "--check"])
    print((r.stdout + r.stderr).strip()[-1500:])
    ok &= r.returncode == 0

    print("== host tests")
    r = run([HERE / "tests" / "run_tests.py"] + (["--quick"] if a.quick else []))
    print(r.stdout.strip()[-2500:])
    if r.returncode:
        print(r.stderr[-2000:])
        ok = False

    print("== scripts")
    card = make_card()
    for script in sorted((HERE / "scripts").glob("*.txt")):
        name = script.stem
        with_card = card if name.startswith("card_") else None
        good, text = drive(script, OUT / name, with_card)
        bugs = [ln for ln in text.splitlines() if ln.startswith("BUG")]
        line = f"{name:12s} {'ok' if good and not bugs else 'FAIL'}"
        if not good or bugs:
            print(line)
            print(text[-1500:])
            ok = False
            continue
        if not a.quick:
            again = OUT / f"{name}.again"
            good2, _ = drive(script, again, with_card)
            files = sorted(p.name for p in (OUT / name).iterdir() if p.suffix in (".png", ".gif"))
            diff = [f for f in files if not (again / f).exists() or frames_hash(OUT / name / f) != frames_hash(again / f)]
            shutil.rmtree(again, ignore_errors=True)
            if not good2 or diff:
                print(f"{line}  NOT DETERMINISTIC: {diff}")
                ok = False
                continue
            line += f"  ({len(files)} images, identical twice)"
        for ln in text.splitlines():
            if ln.startswith(("perf", "calibration")):
                line += "\n    " + ln
        print(line)

    if not a.no_device:
        print("== device build (compile only)")
        r = run([HERE / "device.py", "build"])
        print((r.stdout + r.stderr).strip()[-600:])
        ok &= r.returncode == 0

    print("ALL GOOD" if ok else "SOMETHING FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
