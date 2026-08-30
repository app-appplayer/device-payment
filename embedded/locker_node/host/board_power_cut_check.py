#!/usr/bin/env python3
"""The rental survives the board losing power — on the board.

    python3 host/board_power_cut_check.py            # buy, then reboot
    python3 host/board_power_cut_check.py --after    # check what came back

Two runs, because a power cut is not something a script can do to a chip on its
own. Between them the board is rebooted — pull the cable, or let `sys.dfu` and
a reflash do it, which lands in the same place: a fresh boot with the flash
intact.

The authority lives in the EEPROM the STM32 emulates in a flash page. The time
estimate does not, and must not: uptime restarted and no presented time
survived. So the board wakes up holding a rental it cannot yet judge, says so,
and refuses to open until a phone tells it what time it is.

That state is also the only one in which the floor at the accepted voucher's
window can do anything — with no estimate, the backward rule has nothing to
compare against, and the floor is all that stands between the first correction
and anywhere the presenter likes.
"""

import json
import subprocess
import sys
import time
from pathlib import Path

import serial

HERE = Path(__file__).resolve().parent
MINT = HERE.parent / "tools" / "mint_voucher"
PORT = "/dev/cu.usbmodem365D395E33331"
FAILED = []


def mint(action, session, not_before, seconds=3600):
    args = ["dart", "run", "bin/mint.dart", "sign", "--device", "stm32.h723",
            "--action", action, "--target", "B12", "--session", str(session),
            "--seconds", str(seconds), "--notBefore", str(not_before)]
    out = subprocess.run(args, cwd=MINT, capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"mint failed: {out.stderr}")
    return json.loads(out.stdout.strip().splitlines()[-1])


class Board:
    def __init__(self, port=PORT):
        self.s = serial.Serial(port, 115200, timeout=4)
        time.sleep(0.5)
        self.s.reset_input_buffer()
        self.buf = b""
        self.id = 0
        self._send({"jsonrpc": "2.0", "id": self._next(), "method": "initialize",
                    "params": {"protocolVersion": "2025-03-26",
                               "capabilities": {},
                               "clientInfo": {"name": "power-cut",
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


def before():
    b = Board()
    print("board — buying a rental, then the power goes")
    now = int(time.time())
    session = now % 90000 + 3000
    b.call("time.sync", {"epoch": now})
    check(1, "a rental is bought",
          "accepted" in b.call("voucher.present",
                               mint("locker-4h", session, now)))
    check(2, "and the door opens", b.call("locker.open").strip() == "open")
    b.close()
    print()
    print("  now reboot the board, then run: "
          "python3 host/board_power_cut_check.py --after")


def after():
    b = Board()
    print("board — what came back")
    status = b.call("locker.status")
    check(3, "the rental came back", "remaining=" in status, status)
    check("3b", "and it says it is waiting for the time",
          "resumed after power loss" in status, status)
    check(4, "the door does not open yet", "refused" in b.call("locker.open"))
    check("4b", "the latch did not come back up", "LOCKED" in status, status)
    now = int(time.time())
    check(5, "a first correction before the rental's window is refused",
          "before the accepted" in b.call("time.sync", {"epoch": now - 7200}))
    check(6, "an honest correction is accepted",
          "time=" in b.call("time.sync", {"epoch": int(time.time())}))
    check(7, "and then the door opens",
          b.call("locker.open").strip() == "open")
    check("7b", "and the screen says so rather than the last refusal",
          "reason=open" in b.call("locker.status"), b.call("locker.status"))
    b.close()


def main():
    if "--after" in sys.argv:
        after()
    else:
        before()
    print()
    if FAILED:
        print(f"{len(FAILED)} failed:")
        for f in FAILED:
            print(f"  - {f}")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
