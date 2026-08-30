#!/usr/bin/env python3
"""The renewing branch, on the parking domain.

    python3 host/renewing_check.py

`check.py` covers the fixed shape: buy four hours, get four hours, watch it
expire. This one covers the shape the patent calls its own — entry to a state
at a unit rate, accumulating while nobody is there, ended by the same gesture
that began it.

What has to hold, and none of it is optional:

  - nothing is bought ahead beyond the first unit
  - the accumulation continues with no phone near the machine
  - a renewing authority does not expire; it waits to be released
  - the SAME presentation releases it — no second procedure
  - release is not conditioned on what is owed
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
NODE = HERE / "parking_host"
MINT = HERE.parent / "tools" / "mint_voucher"
# Its own store, cleared first. The machines remember now — an authority
# outlives the process — so a check that does not say which machine state it
# starts from is a check whose result depends on what ran before it. One did:
# a leftover rental turned the first presentation into a release.
STORE = "/tmp/device-payment-renewing.store"
FAILED = []


def mint(action, session, seconds=600, target="GATE1", not_before=None):
    args = ["dart", "run", "bin/mint.dart", "sign", "--device", "stm32.h723",
            "--action", action, "--target", target, "--session", str(session),
            "--seconds", str(seconds)]
    if not_before is not None:
        args += ["--notBefore", str(not_before)]
    out = subprocess.run(args, cwd=MINT, capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"mint failed: {out.stderr}")
    return json.loads(out.stdout.strip().splitlines()[-1])


class Node:
    def __init__(self):
        self.p = subprocess.Popen([str(NODE)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  env=dict(os.environ, NODE_STORE=STORE))
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

    def close(self):
        self.p.stdin.close()


def check(number, what, condition, detail=""):
    print(f"  {'ok  ' if condition else 'FAIL'}  {number}. {what}"
          + (f"   [{detail}]" if detail else ""))
    if not condition:
        FAILED.append(f"{number}. {what} — {detail}")


def main():
    if os.path.exists(STORE):
        os.remove(STORE)
    n = Node()
    print("parking — the renewing branch (patent §4.3)")

    session = int(time.time()) % 100000
    n.call("time.sync", {"epoch": int(time.time())})

    check(1, "the first presentation enters the state",
          "accepted" in n.call("voucher.present",
                               mint("park-demo-2s", session,
                                    not_before=int(time.time()))))
    check("1b", "and it does not promise an end time",
          "renewing until released" in n.call("gate.status"))
    check(2, "the boom raises", n.call("gate.raise").strip() == "open")

    print("       waiting — nothing is sent to the machine")
    time.sleep(6)
    status = n.call("gate.status")
    units = int(status.split("units=")[1].split()[0])
    check(3, "it accumulated while nobody was there", units >= 3, status)
    check("3b", "and it is not counting down to anything",
          "remaining=0s" in status, status)
    check(4, "a renewing authority does not expire",
          "expired" not in n.call("gate.raise"))

    session += 1
    released = n.call("voucher.present",
                      mint("park-demo-2s", session,
                           not_before=int(time.time())))
    check(5, "the same gesture releases it, and reports the total",
          "released units=" in released, released)
    check(6, "after release the boom stays down",
          "refused" in n.call("gate.raise"))

    # The fixed shape on the same machine — the shapes are per offer, not per
    # node, and a car park sells both.
    session += 1
    check(7, "a fixed offer on the same machine still counts down",
          "accepted" in n.call("voucher.present",
                               mint("park-flat-day", session, seconds=3600,
                                    not_before=int(time.time()))))
    check("7b", "and that one does have time remaining",
          "remaining=3" in n.call("gate.status"), n.call("gate.status"))

    n.close()
    print()
    if FAILED:
        print(f"{len(FAILED)} failed:")
        for f in FAILED:
            print(f"  - {f}")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
