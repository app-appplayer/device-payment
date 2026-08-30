#!/usr/bin/env python3
"""The device proves which machine is speaking — patent §3.2.

    python3 host/assertion_check.py

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
from pathlib import Path

HERE = Path(__file__).resolve().parent
NODE = HERE / "payment_locker_host"
MINT = HERE.parent / "tools" / "mint_voucher"
STORE = "/tmp/device-payment-assertion.store"
FAILED = []


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

    def _await(self, want):
        while True:
            msg = json.loads(self.p.stdout.readline())
            if msg.get("id") == want:
                return msg

    def call(self, tool):
        want = self._next()
        self._send({"jsonrpc": "2.0", "id": want, "method": "tools/call",
                    "params": {"name": tool, "arguments": {}}})
        msg = self._await(want)
        return "".join(c.get("text", "")
                       for c in msg.get("result", {}).get("content", []))

    def read(self, uri):
        want = self._next()
        self._send({"jsonrpc": "2.0", "id": want, "method": "resources/read",
                    "params": {"uri": uri}})
        return self._await(want)["result"]["contents"][0]["text"]

    def close(self):
        self.p.stdin.close()


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
    for path in (STORE, MINT / "counters.json"):
        if os.path.exists(path):
            os.remove(path)

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

    n.close()
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
