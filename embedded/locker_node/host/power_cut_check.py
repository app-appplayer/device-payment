#!/usr/bin/env python3
"""The authority outlives the machine losing power.

    python3 host/power_cut_check.py

A rental that vanishes when the mains blink would free every door in the
building, so this is not a nicety. Killing the process and starting it again is
this build's power cut; what matters is what comes back.

What must come back: the authority, and the replay counter. What must NOT: the
time estimate, because uptime restarted and no presented time survived — and
the latch, because a door found open after a power cut is a door nobody chose
to leave open.

This is also the run where the floor at the accepted voucher's window finally
does something. On a warm machine the backward rule reaches every correction
first; on one that woke up holding a rental and no clock, the floor is the only
thing between the first correction and anywhere the presenter likes.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
NODE = HERE / "payment_locker_host"
MINT = HERE.parent / "tools" / "mint_voucher"
STORE = "/tmp/device-payment-powercut.store"
FAILED = []


def mint(action, session, seconds=3600, not_before=None):
    args = ["dart", "run", "bin/mint.dart", "sign", "--device", "stm32.h723",
            "--action", action, "--target", "B12", "--session", str(session),
            "--seconds", str(seconds)]
    if not_before is not None:
        args += ["--notBefore", str(not_before)]
    out = subprocess.run(args, cwd=MINT, capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"mint failed: {out.stderr}")
    return json.loads(out.stdout.strip().splitlines()[-1])


class Node:
    def __init__(self):
        env = dict(os.environ, NODE_STORE=STORE)
        self.p = subprocess.Popen([str(NODE)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, env=env)
        self.id = 0
        self._send({"jsonrpc": "2.0", "id": self._next(), "method": "initialize",
                    "params": {"protocolVersion": "2025-03-26",
                               "capabilities": {},
                               "clientInfo": {"name": "check", "version": "1"}}})
        self.p.stdout.readline()
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _next(self):
        self.id += 1
        return self.id

    def _send(self, msg):
        self.p.stdin.write((json.dumps(msg) + "\n").encode())
        self.p.stdin.flush()

    def call(self, tool, args=None):
        want = self._next()
        self._send({"jsonrpc": "2.0", "id": want, "method": "tools/call",
                    "params": {"name": tool, "arguments": args or {}}})
        while True:
            msg = json.loads(self.p.stdout.readline())
            if msg.get("id") == want:
                return "".join(c.get("text", "")
                               for c in msg.get("result", {}).get("content", []))

    def cut_power(self):
        self.p.kill()
        self.p.wait()


def check(number, what, condition, detail=""):
    print(f"  {'ok  ' if condition else 'FAIL'}  {number}. {what}"
          + (f"   [{detail}]" if detail else ""))
    if not condition:
        FAILED.append(f"{number}. {what} — {detail}")


def main():
    if os.path.exists(STORE):
        os.remove(STORE)
    print("locker — the authority outlives a power cut")

    session = int(time.time()) % 100000
    n = Node()
    now = int(time.time())
    n.call("time.sync", {"epoch": now})
    check(1, "a rental is bought",
          "accepted" in n.call("voucher.present",
                               mint("locker-4h", session, not_before=now)))
    check(2, "and the door opens", n.call("locker.open").strip() == "open")
    n.cut_power()
    print("       power cut")

    back = Node()
    status = back.call("locker.status")
    check(3, "the rental came back", "remaining=" in status, status)
    check("3b", "and it says it is waiting for the time",
          "resumed after power loss" in status, status)
    check(4, "the door does NOT open yet — there is no clock to judge with",
          "refused" in back.call("locker.open"))
    check("4b", "the latch did not come back up", "LOCKED" in status, status)

    # The floor. On this machine — awake, holding a rental, no estimate — a
    # first correction placed before the rental began is refused. Nothing else
    # would catch it: the backward rule has no estimate to compare against.
    check(5, "a first correction before the rental's window is refused",
          "before the accepted" in back.call("time.sync",
                                             {"epoch": now - 7200}))
    check(6, "an honest correction is accepted",
          "time=" in back.call("time.sync", {"epoch": int(time.time())}))
    check(7, "and then the door opens again",
          back.call("locker.open").strip() == "open")

    # The replay counter survived too: a machine that forgets it honours a
    # voucher already used.
    check(8, "a session value used before the cut is still refused",
          "already used" in back.call("voucher.present",
                                      mint("locker-4h", session)))
    back.cut_power()
    os.remove(STORE)

    print()
    if FAILED:
        print(f"{len(FAILED)} failed:")
        for f in FAILED:
            print(f"  - {f}")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
