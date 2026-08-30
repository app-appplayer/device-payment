#!/usr/bin/env python3
"""The design's verification plan, run against the BOARD.

    python3 host/board_check.py [/dev/cu.usbmodemXXXX]

Same checks as `check.py`, except this one talks to the WeAct H723 over the
board wire. The host run says the rules are right; this one says they are right
on a chip that has 320 KB of RAM, no clock and no secure element, and that the
Ed25519 verification actually runs there.

Time is read fresh at every use. An earlier version captured it once at the top
and fed a value several seconds stale by the time it was presented — the board
then refused vouchers as `not yet valid`, correctly, and the test was the thing
that was wrong.
"""

import json
import subprocess
import sys
import time
from pathlib import Path

import serial

HERE = Path(__file__).resolve().parent
MINT = HERE.parent / "tools" / "mint_voucher"
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem365D395E33331"

DEVICE = "stm32.h723"
TARGET = "B12"
FAILED = []


def mint(action, session, seconds=3600, device=DEVICE, target=TARGET,
         not_before=None):
    args = ["dart", "run", "bin/mint.dart", "sign",
            "--device", device, "--action", action, "--target", target,
            "--session", str(session), "--seconds", str(seconds)]
    if not_before is not None:
        args += ["--notBefore", str(not_before)]
    out = subprocess.run(args, cwd=MINT, capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"mint failed: {out.stderr}")
    return json.loads(out.stdout.strip().splitlines()[-1])


class Board:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=4)
        time.sleep(0.4)
        self.s.reset_input_buffer()
        self.buf = b""
        self.id = 0
        self._send({"jsonrpc": "2.0", "id": self._next(), "method": "initialize",
                    "params": {"protocolVersion": "2025-03-26",
                               "capabilities": {},
                               "clientInfo": {"name": "board-check",
                                              "version": "1"}}})
        self._await(self.id)
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _next(self):
        self.id += 1
        return self.id

    def _send(self, msg):
        self.s.write((json.dumps(msg) + "\n").encode())
        self.s.flush()

    def _line(self):
        started = time.time()
        while b"\n" not in self.buf:
            chunk = self.s.read(2048)
            if not chunk and time.time() - started > 8:
                return None
            self.buf += chunk
        line, _, rest = self.buf.partition(b"\n")
        self.buf = rest
        try:
            return json.loads(line)
        except Exception:
            return None

    def _await(self, want):
        while True:
            msg = self._line()
            if msg is None:
                return None
            # The node pushes state://locker every second; a reply has an id.
            if msg.get("id") == want:
                return msg

    def call(self, tool, args=None):
        want = self._next()
        self._send({"jsonrpc": "2.0", "id": want, "method": "tools/call",
                    "params": {"name": tool, "arguments": args or {}}})
        msg = self._await(want)
        if msg is None:
            return "(no reply)"
        return "".join(c.get("text", "")
                       for c in msg.get("result", {}).get("content", []))

    def close(self):
        self.s.close()


def check(number, what, condition, detail=""):
    print(f"  {'ok  ' if condition else 'FAIL'}  {number}. {what}"
          + (f"   [{detail}]" if detail else ""))
    if not condition:
        FAILED.append(f"{number}. {what} — {detail}")


def main():
    b = Board(PORT)
    print(f"locker node ON THE BOARD ({PORT}) — design §8")

    session = int(time.time()) % 100000  # never reused across runs

    # 2 and 2b need a board that has not been told the time yet, which means a
    # board that has just booted. It keeps its estimate across links — it is
    # continuously powered and a locker is not a session — so on a warm board
    # these two are not failures, they are not applicable. Said out loud rather
    # than quietly skipped: a check that silently disappears is a check nobody
    # notices the absence of.
    cold = "time=unknown" in b.call("locker.status")
    if cold:
        check(2, "opening before a time correction is refused",
              "refused" in b.call("locker.open"))
        check("2b", "a voucher before a time correction is refused",
              "refused" in b.call("voucher.present", mint("locker-4h", session)))
    else:
        print("  --    2, 2b. need a cold board — this one already knows the "
              "time (reflash to run them)")

    b.call("time.sync", {"epoch": int(time.time())})
    check(10, "a correction well behind the estimate is refused",
          "backward" in b.call("time.sync", {"epoch": int(time.time()) - 600}))
    check("10b", "an ordinary round trip is still accepted",
          "time=" in b.call("time.sync", {"epoch": int(time.time())}))

    session += 1
    b.call("time.sync", {"epoch": int(time.time())})
    check(3, "a valid voucher is accepted",
          "accepted" in b.call("voucher.present", mint("locker-4h", session)))
    check(4, "the locker opens — the LED is the latch",
          b.call("locker.open").strip() == "open")

    session += 1
    v = mint("locker-4h", session)
    bad = dict(v)
    sig = list(bad["sig"])
    sig[0] = "A" if sig[0] != "A" else "B"
    bad["sig"] = "".join(sig)
    check(5, "a tampered signature is refused ON THE CHIP",
          "signature does not verify" in b.call("voucher.present", bad))
    check(6, "a voucher for another device is refused",
          "another device" in b.call("voucher.present",
                                     mint("locker-4h", session,
                                          device="stm32.other")))
    check(7, "an action the node never offered is refused",
          "never offered" in b.call("voucher.present",
                                    mint("locker-forever", session)))
    check("7b", "a voucher for another target is refused",
          "another target" in b.call("voucher.present",
                                     mint("locker-4h", session, target="C99")))

    v = mint("locker-4h", session)
    check(8, "the first use of a session value is accepted",
          "accepted" in b.call("voucher.present", v))
    check("8b", "replaying it is refused",
          "already used" in b.call("voucher.present", v))
    check("8c", "an older session value is refused",
          "already used" in b.call("voucher.present", mint("locker-4h", 1)))

    # 9. expiry — the one that cannot be faked. Nothing is sent to the board
    # between the open and the refusal; it decides from the time it holds.
    session += 1
    b.call("time.sync", {"epoch": int(time.time())})
    now = int(time.time())
    check(9, "a short voucher is accepted",
          "accepted" in b.call("voucher.present",
                               mint("locker-demo-20s", session, seconds=8,
                                    not_before=now)))
    check("9b", "and it opens while it lasts",
          b.call("locker.open").strip() == "open")
    print("       waiting out the interval — nothing is sent to the board")
    time.sleep(12)
    check("9c", "after the interval it refuses, no message having arrived",
          "expired" in b.call("locker.open"))
    print("  status:", b.call("locker.status"))

    # 11. the session window — the reason an exclusive machine can be left
    # alone. The rental survives; this session's chance to act on it does not.
    session += 1
    b.call("time.sync", {"epoch": int(time.time())})
    check(11, "a voucher with a session window is accepted",
          "accepted" in b.call("voucher.present",
                               mint("locker-demo-20s", session, seconds=3600,
                                    not_before=int(time.time()))))
    check("11b", "and it opens immediately",
          b.call("locker.open").strip() == "open")
    print("       waiting out the session window (10 s)")
    time.sleep(12)
    check("11c", "after the window the door refuses, naming the window",
          "session window closed" in b.call("locker.open"))
    status = b.call("locker.status")
    check("11d", "the rental itself is untouched",
          "remaining=" in status and not status.split("remaining=")[1].startswith("0s"),
          status)

    # 13. the forward bound, on the chip. The earlier version of this firmware
    # accepted +1 year and killed a standing four-hour rental on the spot.
    session += 1
    b.call("time.sync", {"epoch": int(time.time())})
    check(13, "a valid rental stands",
          "accepted" in b.call("voucher.present",
                               mint("locker-4h", session, seconds=3600,
                                    not_before=int(time.time()))))
    now2 = int(time.time())
    check("13b", "a jump a year ahead is refused",
          "jumps forward" in b.call("time.sync",
                                    {"epoch": now2 + 365 * 24 * 3600}))
    check("13c", "and the rental is untouched",
          b.call("locker.open").strip() == "open")

    b.close()
    print()
    if FAILED:
        print(f"{len(FAILED)} failed:")
        for f in FAILED:
            print(f"  - {f}")
        sys.exit(1)
    print("all checks passed on the board")


if __name__ == "__main__":
    main()
