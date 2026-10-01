"""Everything that can be checked without the board, in one go.

    python tools/check.py [--quick] [--no-device]
    python tools/check.py --compare out/moments out/dev_moments

  1. The host tests (tools/tests): the rules against a second implementation,
     whole games through the game's own calls, save and reload, the CPU.
  2. The network: the simulator's build of the game and the host tests must
     report the same evaluation of the opening position, to the last bit.
  3. Every script in tools/scripts runs in the simulator twice: the two runs
     must draw identical frames (the game is deterministic), with no drawing
     into a frame still being sent (the simulator's BUG lines).
  4. The release build compiles for the device and fits (tools/check_size.py).

Screenshots, contact sheets and GIFs are left in out/<script>/ to look at.

--compare A B: the images two runs of a script left (say the simulator's and
the board's, from tools/device.py run): the same frames, pixel for pixel?
"""
import argparse
import hashlib
import re
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


def drive(script, outdir):
    if outdir.exists():
        shutil.rmtree(outdir)
    r = run([HERE / "chsim" / "chdrive.py", "--sim", ROOT, script, outdir])
    return r.returncode == 0, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="the short host tests, and each script once")
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

    print("== host tests")
    r = run([HERE / "tests" / "run_tests.py"] + (["--quick"] if a.quick else []))
    print(r.stdout.strip()[-1500:])
    if r.returncode:
        print(r.stderr[-2000:])
        ok = False
    host_net = re.search(r"worth (-?\d+) to", r.stdout)

    print("== the network in the simulator")
    probe = OUT / "net.txt"
    OUT.mkdir(exist_ok=True)
    probe.write_text("say E\nwait 1\n")
    good, text = drive(probe, OUT / "net")
    sim_net = re.search(r"NET (-?\d+) ", text)
    if not (good and host_net and sim_net and host_net.group(1) == sim_net.group(1)):
        print(f"FAIL: host {host_net and host_net.group(1)}, simulator {sim_net and sim_net.group(1)}")
        ok = False
    else:
        print(f"opening position: {sim_net.group(1)} in both")

    print("== scripts")
    for script in sorted((HERE / "scripts").glob("*.txt")):
        name = script.stem
        good, text = drive(script, OUT / name)
        bugs = [ln for ln in text.splitlines() if ln.startswith("BUG")]
        line = f"{name:10s} {'ok' if good and not bugs else 'FAIL'}"
        if not good or bugs:
            print(line)
            print(text[-1500:])
            ok = False
            continue
        if not a.quick:
            again = OUT / f"{name}.again"
            good2, _ = drive(script, again)
            files = sorted(p.name for p in (OUT / name).iterdir() if p.suffix in (".png", ".gif"))
            diff = [f for f in files if not (again / f).exists() or frames_hash(OUT / name / f) != frames_hash(again / f)]
            shutil.rmtree(again, ignore_errors=True)
            if not good2 or diff:
                print(f"{line}  NOT DETERMINISTIC: {diff}")
                ok = False
                continue
            line += f"  ({len(files)} images, identical twice)"
        for ln in text.splitlines():
            if ln.startswith(("perf", "calibration", "THINK")):
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
