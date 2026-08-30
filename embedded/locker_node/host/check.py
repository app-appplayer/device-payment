#!/usr/bin/env python3
"""The design's verification plan, run against the domain on this machine.

    python3 host/check.py

Checks 1–10 of `device-payment/design/locker-node-2026-08-30.md` §8, minus the
ones that need a latch to look at. Every refusal case is here, because a node
that only proves its happy path is a node that verifies nothing.

The vouchers are minted by the same tool the board's key came from, so a check
that passes here passes for the same reason it will pass on hardware.
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

DEVICE = "stm32.h723"
TARGET = "B12"


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


class Node:
    """One session with the node, over the same newline JSON-RPC a board speaks."""

    def __init__(self):
        self.p = subprocess.Popen([str(NODE)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  env=dict(os.environ, NODE_STORE=STORE))
        self._id = 0
        self._send({"jsonrpc": "2.0", "id": self._next(), "method": "initialize",
                    "params": {"protocolVersion": "2025-03-26",
                               "capabilities": {},
                               "clientInfo": {"name": "check", "version": "1"}}})
        self._read()
        self._send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _next(self):
        self._id += 1
        return self._id

    def _send(self, msg):
        self.p.stdin.write((json.dumps(msg) + "\n").encode())
        self.p.stdin.flush()

    def _read(self):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise SystemExit("node closed the stream")
            msg = json.loads(line)
            # Live pushes arrive unsolicited; a reply is what has an id.
            if "id" in msg:
                return msg

    def call(self, tool, args=None):
        i = self._next()
        self._send({"jsonrpc": "2.0", "id": i, "method": "tools/call",
                    "params": {"name": tool, "arguments": args or {}}})
        reply = self._read()
        content = reply.get("result", {}).get("content", [])
        return "".join(c.get("text", "") for c in content)

    def close(self):
        self.p.stdin.close()
        self.p.wait(timeout=5)


# Its own store, cleared first. The machines remember now — an authority
# outlives the process — so a check that does not say which machine state it
# starts from is a check whose result depends on what ran before it. One did:
# a leftover rental turned the first presentation into a release.
STORE = "/tmp/device-payment-check.store"
FAILED = []


def check(number, what, condition, detail=""):
    mark = "ok  " if condition else "FAIL"
    print(f"  {mark}  {number}. {what}" + (f"   [{detail}]" if detail else ""))
    if not condition:
        FAILED.append(f"{number}. {what} — {detail}")


def main():
    if os.path.exists(STORE):
        os.remove(STORE)
    now = int(time.time())
    session = 100

    print("locker node — design §8")

    # 2. no time yet: the interval cannot be judged, so it does not open.
    n = Node()
    check(2, "opening without a time correction is refused",
          "refused" in n.call("locker.open"), n.call("locker.status"))

    # A voucher cannot be judged either, for the same reason.
    v = mint("locker-4h", session)
    check("2b", "a voucher without a time correction is refused",
          "refused" in n.call("voucher.present", v))

    # 10. a correction that moves the estimate backward is refused.
    n.call("time.sync", {"epoch": now})
    check(10, "time moving backward is refused",
          "refused" in n.call("time.sync", {"epoch": now - 600}))

    # 3 & 4. a valid voucher is accepted and the locker opens.
    session += 1
    v = mint("locker-4h", session)
    check(3, "a valid voucher is accepted",
          "accepted" in n.call("voucher.present", v))
    check(4, "the locker opens", n.call("locker.open").strip() == "open")

    # 5. one byte of the signature changed — refused.
    session += 1
    v = mint("locker-4h", session)
    bad = dict(v)
    sig = list(bad["sig"])
    sig[0] = "A" if sig[0] != "A" else "B"
    bad["sig"] = "".join(sig)
    check(5, "a tampered signature is refused",
          "signature does not verify" in n.call("voucher.present", bad))

    # 6. a valid signature over another device — refused.
    v = mint("locker-4h", session, device="stm32.someone-else")
    check(6, "a voucher for another device is refused",
          "another device" in n.call("voucher.present", v))

    # 7. signed, but for an action this node never offered.
    v = mint("locker-forever", session)
    check(7, "an action the node never offered is refused",
          "never offered" in n.call("voucher.present", v))

    # 7b. signed, but for another locker.
    v = mint("locker-4h", session, target="C99")
    check("7b", "a voucher for another target is refused",
          "another target" in n.call("voucher.present", v))

    # 8. replay — the same session value again.
    v = mint("locker-4h", session)
    check(8, "the first use of this session value is accepted",
          "accepted" in n.call("voucher.present", v))
    check("8b", "replaying it is refused",
          "already used" in n.call("voucher.present", v))
    check("8c", "an older session value is refused",
          "already used" in n.call("voucher.present", mint("locker-4h", 1)))

    n.close()

    # 9. expiry, with nothing sent to the node in between.
    #
    # A FRESH node, because the checks above leave a four-hour authority
    # standing and an "it opens" that is really the old one still holding is
    # exactly the false pass this is meant to catch.
    #
    # The interval starts NOW rather than at the minter's default backdate: a
    # window shorter than that backdate is already over when it is signed, and
    # the node was right to refuse it.
    expiry = Node()
    now2 = int(time.time())
    expiry.call("time.sync", {"epoch": now2})
    v = mint("locker-demo-20s", 500, seconds=6, not_before=now2)
    check(9, "a short voucher is accepted",
          "accepted" in expiry.call("voucher.present", v))
    check("9b", "and it opens while it lasts",
          expiry.call("locker.open").strip() == "open")
    print("       waiting out the interval — nothing is sent to the node")
    time.sleep(9)
    # The node is asked to open only after the interval has passed. Nothing
    # told it to expire; it decided from the time it holds.
    check("9c", "after the interval it refuses, with no message having arrived",
          "expired" in expiry.call("locker.open"))
    expiry.close()

    # 11. the session window. The rental is untouched — what ran out is this
    # session's chance to act on it. A node that let the door open forever
    # after one authorisation would have no notion of "being here".
    win = Node()
    now3 = int(time.time())
    win.call("time.sync", {"epoch": now3})
    check(11, "a voucher with a session window is accepted",
          "accepted" in win.call("voucher.present",
                                 mint("locker-demo-20s", 600, seconds=3600,
                                      not_before=now3)))
    check("11b", "and it opens immediately",
          win.call("locker.open").strip() == "open")
    print("       waiting out the session window (10 s)")
    time.sleep(12)
    check("11c", "after the window the door refuses, naming the window",
          "session window closed" in win.call("locker.open"))
    check("11d", "the authority itself is still alive",
          "remaining=" in win.call("locker.status") and
          "0s" not in win.call("locker.status").split("remaining=")[1][:6],
          win.call("locker.status"))
    # 12. presenting again on a fresh link reopens the chance.
    v = mint("locker-demo-20s", 601, seconds=3600, not_before=int(time.time()))
    win.call("time.sync", {"epoch": int(time.time())})
    check(12, "presenting again reopens the window",
          "accepted" in win.call("voucher.present", v))
    check("12b", "and the door opens", win.call("locker.open").strip() == "open")
    win.close()

    # 13. the forward bound. Without it a presented time can be pushed a year
    # ahead and a standing rental dies on the spot — and where the estimate is
    # billed rather than compared, the same jump is over-metering that the
    # person pays for.
    fwd = Node()
    now4 = int(time.time())
    fwd.call("time.sync", {"epoch": now4})
    check(13, "a jump far into the future is refused",
          "jumps forward" in fwd.call("time.sync",
                                      {"epoch": now4 + 365 * 24 * 3600}))
    check("13b", "and an ordinary correction still lands",
          "time=" in fwd.call("time.sync", {"epoch": int(time.time())}))
    check("13c", "a standing authority survives the attempt",
          "accepted" in fwd.call("voucher.present",
                                 mint("locker-4h", 700, seconds=3600,
                                      not_before=int(time.time()))) and
          fwd.call("locker.open").strip() == "open")

    # 14. a correction placed before the accepted window is refused. Which
    # rule refuses it is not the point and is not asserted: the backward rule
    # reaches it first today, and the window floor behind it is there for when
    # the authority outlives a reboot and the estimate does not. Pinning the
    # message would pin the order rather than the behaviour.
    check(14, "a correction into the past is refused, whichever rule catches it",
          "refused" in fwd.call("time.sync", {"epoch": now4 - 3600}))
    fwd.close()

    print()
    if FAILED:
        print(f"{len(FAILED)} failed:")
        for f in FAILED:
            print(f"  - {f}")
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
