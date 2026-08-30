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

extern "C" int locker_ed25519_verify(const unsigned char sig[64],
                                     const unsigned char pub[32],
                                     const char* msg,
                                     unsigned long msg_len) {
    return Ed25519::verify(sig, pub, msg, (size_t)msg_len) ? 1 : 0;
}
