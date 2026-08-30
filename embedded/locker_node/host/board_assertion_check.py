#!/usr/bin/env python3
"""The device proves which machine is speaking — on the board. Patent §3.2.

    python3 host/board_assertion_check.py

Everything else in this tree runs service → device: the service signs, the
device checks. This is the other direction, and without it the comparison the
whole arrangement rests on only works one way.

The device signs

    deviceId || counter || nonce || H(declaration)

and the phone relays it UNCHANGED. Two things follow:

  - the counter never repeats, so an assertion captured from this device
    cannot be replayed to the service on its behalf;
  - the declaration is signed over, so the phone cannot tell the service about
    an offer the machine never made.

The device key is not the service key and cannot stand in for it. A device that
could mint its own authority would be a machine that authorises itself; this
one can only say who it is.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

import serial

HERE = Path(__file__).resolve().parent
PORT = "/dev/cu.usbmodem365D395E33331"
MINT = HERE.parent / "tools" / "mint_voucher"
FAILED = []


class Node:
    """The board, over the wire it speaks. Its counter and its replay ceiling
    both survived the last power cut, so nothing here assumes a fresh chip."""

    def __init__(self):
        self.s = serial.Serial(PORT, 115200, timeout=8)
        time.sleep(1.0)
        self.s.reset_input_buffer()
        self.buf = b""
        self.id = 0
        self._send({"jsonrpc": "2.0", "id": self._next(), "method": "initialize",
                    "params": {"protocolVersion": "2025-03-26",
                               "capabilities": {},
                               "clientInfo": {"name": "check", "version": "1"}}})
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
            chunk = self.s.read(4096)
            if not chunk and time.time() - started > 12:
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
        return "".join(c.get("text", "")
                       for c in msg.get("result", {}).get("content", []))

    def read(self, uri):
        want = self._next()
        self._send({"jsonrpc": "2.0", "id": want, "method": "resources/read",
                    "params": {"uri": uri}})
        return self._await(want)["result"]["contents"][0]["text"]

    def close(self):
        self.s.close()


def mint(assertion=None, spec=None, session=1):
    args = ["dart", "run", "bin/mint.dart", "sign", "--device", "stm32.h723",
            "--action", "locker-4h", "--target", "B12",
            "--session", str(session), "--seconds", "3600"]
    if assertion is not None:
        args += ["--assertion", json.dumps(assertion)]
    if spec is not None:
        args += ["--spec", spec]
    r = subprocess.run(args, cwd=MINT, capture_output=True, text=True)
    out = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()
    return r.returncode, out


def check(number, what, condition, detail=""):
    print(f"  {'ok  ' if condition else 'FAIL'}  {number}. {what}"
          + (f"   [{detail}]" if detail else ""))
    if not condition:
        FAILED.append(f"{number}. {what} — {detail}")


def main():
    counters = MINT / "counters.json"
    if os.path.exists(counters):
        os.remove(counters)

    n = Node()
    print("the device asserts, the service verifies — patent §3.2")

    first = json.loads(n.call("device.assert"))
    spec = n.read("ui://page/main")
    check(1, "the device signs an assertion over its own declaration",
          all(k in first for k in
              ("deviceId", "counter", "nonce", "specHash", "sig")),
          f"counter={first['counter']}")

    rc, out = mint(first, spec, 501)
    check(2, "the service verifies it, and only then mints", rc == 0 and '"sig"' in out)

    rc, out = mint(first, spec, 502)
    check(3, "the same assertion cannot be used twice",
          rc != 0 and "already used" in out, out[:60])

    second = json.loads(n.call("device.assert"))
    check(4, "the counter advances", second["counter"] == first["counter"] + 1,
          f"{first['counter']} -> {second['counter']}")

    rc, out = mint(second, spec.replace("Locker B12", "Locker ZZ9", 1), 503)
    check(5, "a declaration the device did not sign is refused",
          rc != 0 and "not the one the device signed" in out, out[:70])

    third = json.loads(n.call("device.assert"))
    tampered = dict(third)
    sig = list(tampered["sig"])
    sig[0] = "A" if sig[0] != "A" else "B"
    tampered["sig"] = "".join(sig)
    rc, out = mint(tampered, spec, 504)
    check(6, "a tampered assertion is refused",
          rc != 0 and "does not verify" in out, out[:60])

    # The nonce is drawn, not counted. Two assertions in a row must differ in
    # more than the number that was always going to change.
    fourth = json.loads(n.call("device.assert"))
    check(7, "the nonce is drawn rather than derived",
          fourth["nonce"] != third["nonce"])

    # The end of the chain: a voucher minted only because the assertion
    # verified, presented back to the machine that made the assertion.
    #
    # The session value is read off the board rather than invented. Its replay
    # ceiling survived the last power cut, so a number picked out of the air is
    # a number the board has very likely already refused — which it did, and
    # the board was right.
    seen = int(n.call("locker.status").split("session=")[1].split()[0])
    spec_now = n.read("ui://page/main")
    fresh = json.loads(n.call("device.assert"))
    rc, voucher = mint(fresh, spec_now, seen + 1)
    check(8, "the service minted it, because the assertion verified", rc == 0,
          voucher[:60])
    n.call("time.sync", {"epoch": int(time.time())})
    presented = n.call("voucher.present", json.loads(voucher))
    check("8a", "and the machine that asserted accepts it",
          "accepted" in presented, presented)
    check("8b", "and the door opens", n.call("locker.open").strip() == "open")

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
