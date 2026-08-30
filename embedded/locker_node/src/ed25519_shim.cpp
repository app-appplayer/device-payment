/* Ed25519 verification for the locker domain.
 *
 * The domain is C and the Arduino Crypto library is C++, so the boundary lives
 * here and nowhere else.
 *
 * Verification only. This node holds a public key and can therefore check a
 * voucher; it holds no signing key and can therefore not make one. That
 * asymmetry is the property being demonstrated — with a shared secret the
 * device could mint its own authority, which is exactly what a locker must not
 * be able to do.
 */

#include "crypto/Ed25519.h"
#include "crypto/SHA512.h"

extern "C" int locker_ed25519_verify(const unsigned char sig[64],
                                     const unsigned char pub[32],
                                     const char* msg,
                                     unsigned long msg_len) {
    return Ed25519::verify(sig, pub, msg, (size_t)msg_len) ? 1 : 0;
}

/* Signing, for the device's own identity assertion — and ONLY that.
 *
 * This does not weaken the property the voucher path rests on. Two different
 * keys prove two different things: the service key mints authority, and it is
 * not here; the device key says which machine is speaking, and it cannot mint
 * anything. A machine that could do both would be a machine that authorises
 * itself.
 */
extern "C" void locker_ed25519_sign(unsigned char sig[64],
                                    const unsigned char priv[32],
                                    const char* msg,
                                    unsigned long msg_len) {
    uint8_t pub[32];
    Ed25519::derivePublicKey(pub, priv);
    Ed25519::sign(sig, priv, pub, msg, (size_t)msg_len);
}

/* Collision-resistant hash of the declaration, so the phone cannot relay a
 * specification other than the one the device signed over. */
extern "C" void locker_sha512(unsigned char out[64], const char* msg,
                              unsigned long msg_len) {
    SHA512 h;
    h.reset();
    h.update(msg, (size_t)msg_len);
    h.finalize(out, 64);
}
