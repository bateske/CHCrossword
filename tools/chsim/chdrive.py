"""Drive a CHGame sketch - in the simulator or on the device - with a script.

    python chdrive.py --sim <sketch dir> <script> <outdir>
    python chdrive.py --device [--port COMx] <script> <outdir>

--id names the game's handshake reply (default CHCW, CHCrossword).
--card FILE (simulator): a FAT image to stand in for the SD card.

Both targets speak the same serial debug protocol (see the sketch's
src/debug/Debug.h), so one script produces comparable screenshots from each.

Script lines (# comments allowed):
    wait N              advance N frames
    free SECONDS        run free (real time) for a while, then lockstep again
    state               print the game's STATE line (the H command)
    expect KEY=VALUE .. fail unless the STATE line has these
    waitstate KEY=VALUE [MAX]   run until the STATE line has it (at most MAX frames, 2000)
    type LETTERS        on the open letter board: walk the glove to each letter and press A
    solve [N]           fill in the next N words not done yet (all of them by default), each
                        through the letter board as a player would: cursor to the word, A,
                        the letters still missing
    wrong               fill in the next word not done yet with its last missing letter wrong
    mark                remember the STATE line
    delta KEY=N ..      fail unless these have changed by N since `mark`
    rec start [EVERY] / rec stop NAME   record everything in between to NAME.gif
    tap BTN[+BTN] [H]   hold for H frames (default 3), then release, then 1 frame
    hold BTN[+BTN]      keep held until `release`
    release
    snap NAME           save NAME.png
    gif NAME N [EVERY]  record N frames (every EVERY-th) to NAME.gif
    say TEXT            send TEXT as a raw protocol line (game hooks: see Screens.cpp)
    perf                print the device's PERF line
Buttons: A B UP DOWN LEFT RIGHT START SELECT
"""
import argparse
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


from fbimage import sheet, to_image  # noqa: E402

BUTTONS = {"A": 1, "B": 2, "UP": 4, "DOWN": 8, "LEFT": 16, "RIGHT": 32, "START": 64, "SELECT": 128}


def mask_of(spec):
    m = 0
    for part in spec.upper().split("+"):
        if part:
            m |= BUTTONS[part]
    return m


class SimTransport:
    def __init__(self, exe):
        self.p = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, bufsize=0)
        self.bugs = []
        threading.Thread(target=self._err, daemon=True).start()

    def _err(self):
        for line in self.p.stderr:
            t = line.decode("latin-1").rstrip()
            self.bugs.append(t)
            sys.stderr.write(t + "\n")

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()

    def read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.p.stdout.read(n - len(buf))
            if not chunk:
                raise EOFError("simulator exited")
            buf += chunk
        return buf

    def readline(self):
        buf = b""
        while not buf.endswith(b"\n"):
            buf += self.read(1)
        return buf.decode("latin-1").strip()

    def close(self):
        self.p.stdin.close()
        rc = self.p.wait(10)
        return rc


class SerialTransport:
    def __init__(self, port=None):
        sys.path.insert(0, str(HERE.parent))           # tools/serialcap.py
        from serialcap import open_port
        self.s = open_port(port)
        self.s.timeout = 1
        time.sleep(0.3)
        self.s.reset_input_buffer()
        self.bugs = []

    def send(self, line):
        self.s.write((line + "\n").encode())

    def read(self, n):
        buf = b""
        end = time.time() + 10
        while len(buf) < n and time.time() < end:
            buf += self.s.read(n - len(buf))
        if len(buf) < n:
            raise TimeoutError(f"wanted {n} bytes, got {len(buf)}")
        return buf

    def readline(self, timeout=30.0):
        buf = b""
        end = time.time() + timeout
        while not buf.endswith(b"\n"):
            if time.time() > end:
                raise TimeoutError("no reply from device")
            buf += self.s.readline()
        return buf.decode("latin-1").strip()

    def close(self):
        self.s.close()
        return 0


class Driver:
    def __init__(self, t, ident="CHCW"):
        self.t = t
        self.ident = ident

    def expect(self, prefix, tries=50):
        for _ in range(tries):
            line = self.t.readline()
            if line.startswith(prefix):
                return line
        raise RuntimeError(f"never saw {prefix!r}")

    def cmd(self, line, prefix="OK"):
        self.t.send(line)
        return self.expect(prefix)

    def handshake(self, tries=20):
        # A freshly uploaded board may still be enumerating; replies sent
        # before the host opened the port are dropped, so ask until answered.
        for _ in range(tries):
            self.t.send("?")
            try:
                line = self.t.readline(1.0) if isinstance(self.t, SerialTransport) else self.t.readline()
                if line.startswith(self.ident):
                    return line
            except TimeoutError:
                time.sleep(0.3)
        raise RuntimeError("device never answered '?'")

    def frames(self, n):
        rec = getattr(self, "rec", None)
        if rec is None:
            if n > 0:
                self.cmd(f"N {n}")
            return
        # Recording: a frame at a time, keeping every rec_every-th.
        for _ in range(n):
            self.cmd("N 1")
            self.rec_count += 1
            if self.rec_count % self.rec_every == 0:
                rec.append(self.shot())

    def query(self, line, prefix):
        """Send a game command and return its reply line starting with prefix."""
        self.t.send(line)
        got = None
        for _ in range(100):
            reply = self.t.readline()
            if reply.startswith(prefix):
                got = reply.strip()
            elif reply.startswith("OK"):
                return got
            elif reply.startswith(("ERR", "HELD")):
                raise SystemExit(f"game refused: {line}")
        raise RuntimeError(f"no reply to {line}")

    def buttons(self, mask):
        self.cmd(f"K {mask:x}")

    def press(self, spec, hold=3):
        self.buttons(mask_of(spec))
        self.frames(hold)
        self.buttons(0)
        self.frames(1)

    def state(self):
        """The game's STATE line as a dict."""
        line = self.query("H", "STATE")
        return dict(f.split("=") for f in line.split()[1:])

    def type(self, letters):
        """Walk the letter board's glove to each letter (9 keys a row) and press A."""
        for ch in letters.upper():
            k = int(self.state()["key"])
            want = ord(ch) - 65
            dx, dy = want % 9 - k % 9, want // 9 - k // 9
            for _ in range(abs(dx)):
                self.press("RIGHT" if dx > 0 else "LEFT", 2)
            for _ in range(abs(dy)):
                self.press("DOWN" if dy > 0 else "UP", 2)
            self.press("A", 2)

    def wrong(self):
        """The next word not done, typed with its last missing letter one on in the alphabet."""
        _, w, cell, down, answer, have = self.query("W", "WORD").split()
        k = have.index(".")
        step = int(self.state()["n"]) if down == "1" else 1
        self.cmd(f"C {int(cell) + k * step} {down}")
        self.frames(1)
        self.press("A")
        self.frames(10)
        missing = [a for a, h in zip(answer, have) if h == "."]
        missing[-1] = chr((ord(missing[-1]) - 65 + 1) % 26 + 65)
        self.type("".join(missing))

    def solve(self, count):
        while count:
            word = self.query("W", "WORD").split()
            if word[1] == "none":
                break
            _, w, cell, down, answer, have = word
            # The cursor to the word's first empty cell, then type what is missing.
            k = have.index(".") if "." in have else next(i for i in range(len(answer)) if have[i] != answer[i])
            step = int(self.state()["n"]) if down == "1" else 1
            self.cmd(f"C {int(cell) + k * step} {down}")
            self.frames(1)
            if "." not in have:
                # A wrong letter in a full word: rub it out (the board's B), type it again.
                self.press("A")
                self.frames(10)
                self.press("B")
                self.frames(2)
                self.type(answer[k])
            else:
                self.press("A")
                self.frames(10)
                self.type("".join(a for a, h in zip(answer, have) if h == "."))
            # Let the word's show run, and the board shut.
            for _ in range(400):
                st = self.state()
                if st["board"] == "0" and (st["busy"] == "0" or st["solved"] == "1"):
                    break
                if st["board"] == "1" and st["busy"] == "0":
                    self.press("START")
                self.frames(2)
            count -= 1

    def shot(self):
        self.t.send("S")
        hdr = self.expect("FB ")
        n = int(hdr.split()[2])
        return self.t.read(n)

    def run(self, script, outdir, scale=3):
        outdir = Path(outdir)
        outdir.mkdir(parents=True, exist_ok=True)
        self.handshake()
        self.cmd("L1")
        snaps = []
        for raw in Path(script).read_text().splitlines():
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            op, *args = line.split()
            if getattr(self, "verbose", False):
                print(">", line, flush=True)
            if op == "wait":
                self.frames(int(args[0]))
            elif op == "tap":
                self.buttons(mask_of(args[0]))
                self.frames(int(args[1]) if len(args) > 1 else 3)
                self.buttons(0)
                self.frames(1)
            elif op == "hold":
                self.buttons(mask_of(args[0]))
            elif op == "release":
                self.buttons(0)
            elif op == "snap":
                im = to_image(self.shot(), scale)
                im.save(outdir / f"{args[0]}.png")
                snaps.append((args[0], im))
            elif op == "gif":
                name, count = args[0], int(args[1])
                every = int(args[2]) if len(args) > 2 else 1
                frames = []
                for _ in range(count):
                    frames.append(to_image(self.shot(), 2))
                    self.frames(every)
                frames[0].save(outdir / f"{name}.gif", save_all=True, append_images=frames[1:],
                               duration=int(1000 * every / 60), loop=0)
            elif op == "say":
                self.t.send(" ".join(args))
                for _ in range(10000):
                    line = self.t.readline()
                    if line.startswith("OK"):
                        break
                    if line.startswith("ERR"):
                        raise SystemExit(f"game refused: {raw.strip()}")
            elif op == "free":
                # Run in real time for N seconds (device timing is only
                # meaningful free-running), then return to lockstep.
                self.cmd("L0")
                time.sleep(float(args[0]))
                self.cmd("L1")
            elif op == "state":
                print(self.query("H", "STATE"), flush=True)
            elif op == "expect":
                st = self.state()
                for kv in args:
                    k, v = kv.split("=")
                    if st.get(k) != v:
                        raise SystemExit(f"expect {kv}: the game has {k}={st.get(k)} ({raw.strip()})")
            elif op == "waitstate":
                k, v = args[0].split("=")
                for _ in range(int(args[1]) if len(args) > 1 else 2000):
                    if self.state().get(k) == v:
                        break
                    self.frames(1)
                else:
                    raise SystemExit(f"waitstate {args[0]}: never happened")
            elif op == "type":
                self.type(args[0])
            elif op == "wrong":
                self.wrong()
            elif op == "mark":
                self.mark = self.state()
            elif op == "delta":
                st = self.state()
                for kv in args:
                    k, v = kv.split("=")
                    if int(st[k]) - int(self.mark[k]) != int(v):
                        raise SystemExit(f"delta {kv}: {k} went from {self.mark[k]} to {st[k]} ({raw.strip()})")
            elif op == "solve":
                self.solve(int(args[0]) if args else 100000)
            elif op == "rec":
                # rec start [EVERY]: record from here (every EVERY-th frame, 3);
                # rec stop NAME: write NAME.gif.
                if args[0] == "start":
                    self.rec, self.rec_count = [], 0
                    self.rec_every = int(args[1]) if len(args) > 1 else 3
                else:
                    shots, self.rec = self.rec, None
                    frames = [to_image(d, 2) for d in shots]
                    frames[0].save(outdir / f"{args[1]}.gif", save_all=True, append_images=frames[1:],
                                   duration=int(1000 * self.rec_every / 60), loop=0)
                    print(f"{args[1]}.gif: {len(frames)} frames", flush=True)
            elif op == "freegif":
                # Free-running (real time, as on the board: the CPU thinks in
                # bursts) for SECONDS, a shot every EVERY seconds into a GIF.
                # A shot waits for the game's next frame.
                name, secs, every = args[0], float(args[1]), float(args[2])
                self.cmd("L0")
                frames, t0 = [], time.time()
                while time.time() - t0 < secs:
                    frames.append(to_image(self.shot(), 2))
                    time.sleep(every)
                self.cmd("L1")
                frames[0].save(outdir / f"{name}.gif", save_all=True, append_images=frames[1:],
                               duration=int(1000 * every), loop=0)
            elif op == "step":
                # One frame at a time, as the free-running game does (N k runs
                # up to three logic ticks per drawn frame, like a slow frame's
                # catch-up).
                for _ in range(int(args[0])):
                    self.frames(1)
            elif op == "cal":
                # Simulator: host time of the primitives the CHGfx benchmark
                # measured on the board -> device ns per host ns.
                self.t.send("Q")
                vals = [int(v) for v in self.expect("CAL").split()[1:]]
                self.expect("OK")
                device_us = [93, 4, 100, 246, 263]   # benchmark-results.txt, 12 bpp run
                ratios = [d * 1000.0 / max(h, 1) for d, h in zip(device_us, vals)]
                self.ratio = sum(ratios) / len(ratios)
                print(f"calibration: device/host = {self.ratio:.1f} (clear, hline, blit16, text24, circle: {[round(r) for r in ratios]})")
            elif op == "perf":
                line = self.cmd("P", "PERF")
                label = " ".join(args)
                kv = dict(f.split("=") for f in line.split()[1:] if "=" in f)
                if getattr(self, "ratio", None) and "pcrnd" in kv:
                    avg = int(kv["pcrnd"]) * self.ratio / 1e6
                    mx = int(kv["pcmax"]) * self.ratio / 1e6
                    print(f"perf {label}: est. device render avg {avg:.1f} ms, max {mx:.1f} ms ({kv['frames']} frames)")
                else:
                    print(line)
            elif op == "prof":
                print(self.cmd("T", "PROF"))
            else:
                raise SystemExit(f"bad script line: {raw}")
        if snaps:
            sh = sheet([im for _, im in snaps], cols=4)
            sh.save(outdir / "sheet.png")
        return snaps


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--sim", metavar="SKETCH")
    g.add_argument("--device", action="store_true")
    ap.add_argument("--port")
    ap.add_argument("--id", default="CHCW", help="handshake prefix the game answers '?' with")
    ap.add_argument("-v", "--verbose", action="store_true", help="echo each script line")
    ap.add_argument("-D", dest="defines", action="append", default=[])
    ap.add_argument("--card", help="(simulator) a FAT image for the SD card")
    ap.add_argument("script")
    ap.add_argument("outdir")
    a = ap.parse_args()
    if a.sim:
        from chsim import build
        if a.card:
            os.environ["CHCW_CARD"] = str(Path(a.card).resolve())
        t = SimTransport(build(a.sim, a.defines))
    else:
        t = SerialTransport(a.port)
    d = Driver(t, a.id)
    d.verbose = a.verbose
    try:
        d.run(a.script, a.outdir)
    finally:
        if a.device:
            try:
                d.buttons(0)
                d.cmd("L0")
            except Exception:
                pass
        rc = t.close()
    bugs = [b for b in getattr(t, "bugs", []) if b.startswith("BUG")]
    if bugs:
        raise SystemExit(f"{len(bugs)} simulator bug report(s)")
    print(f"ok: {a.outdir}")
    return rc


if __name__ == "__main__":
    main()
