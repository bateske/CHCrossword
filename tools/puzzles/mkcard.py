"""Make an SD card image with puzzle packs on it, for the simulator and tests.

    python tools/puzzles/mkcard.py OUT.img PACK.CWD [PACK.CWD ...] [--fs fat16|fat32]

The packs go in the folder CHCW, where the game looks for them. (On a real
card just copy the .CWD files into a folder named CHCW; the card must be
FAT16 or FAT32, which is how cards up to 32 GB come.)
Run in the simulator with: python tools/chsim/chdrive.py --sim . --card OUT.img SCRIPT OUTDIR
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fatimg  # noqa: E402


def make(out, packs, fs="fat16", **kw):
    """packs: {"NAME.CWD": bytes}. Returns fatimg's Layout."""
    files = {f"CHCW/{name}": data for name, data in packs.items()}
    return fatimg.build_image(str(out), files, fs=fs, label="CHCROSSWORD", **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("packs", nargs="+")
    ap.add_argument("--fs", default="fat16", choices=("fat16", "fat32"))
    a = ap.parse_args()
    lay = make(a.out, {Path(p).name.upper(): Path(p).read_bytes() for p in a.packs}, a.fs)
    print(f"{a.out}: {lay.fs}, {lay.image_sectors * 512:,} B, {len(a.packs)} pack(s) in CHCW")


if __name__ == "__main__":
    main()
