/* The two keys this node is built with, and a readable failure when they are
 * not there yet.
 *
 * Neither is committed. The device's private key must not be — a published
 * device key is a device anyone can impersonate, and the assertion it signs
 * would prove nothing. The service's public key is harmless on its own, but
 * shipping half a pair invites the reading that the repository comes with a
 * working one; it does not, and it should not.
 *
 * So both are generated, and a build that has not generated them says what to
 * do instead of naming a missing file. The first version of this failed with
 *
 *     fatal error: 'device_privkey.inc' file not found
 *
 * which is true and tells a reader nothing.
 */
#ifndef NODE_KEYS_H
#define NODE_KEYS_H

#if defined(__has_include)
#  if !__has_include("service_pubkey.inc") || !__has_include("device_privkey.inc")
#    error "No keys yet. Run: cd tools/mint_voucher && dart run bin/mint.dart keygen"
#  endif
#endif

/* The service's public half. This node verifies vouchers against it and cannot
 * make one — the private half lives with the issuer and nowhere else. */
static const unsigned char SERVICE_PUBKEY[32] = {
#include "service_pubkey.inc"
};

/* This machine's own private key. It says which machine is speaking; it cannot
 * mint an authority. A machine that could do both would authorise itself. */
static const unsigned char DEVICE_PRIVKEY[32] = {
#include "device_privkey.inc"
};

#endif /* NODE_KEYS_H */
