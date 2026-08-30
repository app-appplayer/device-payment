/* Local stand-in for the Crypto library's RNG.h.
 *
 * The vendored Ed25519 / Curve25519 sources include this header for KEY
 * GENERATION only (`generatePrivateKey`, `dh1`). This node never generates a
 * key — it holds a public key and verifies — so the generator is not wanted
 * here, and the library's real RNG.cpp cannot be compiled on this chip anyway:
 * the STM32H7 CMSIS header defines `RNG` as a macro for the peripheral, which
 * collides with the library's global of the same name.
 *
 * So `rand` is DECLARED and never defined. Verification links; anything that
 * reaches for randomness fails at link time with the symbol named. A stub that
 * returned zeroes would compile, run, and hand out a key made of nothing.
 */
#ifndef LOCKER_LOCAL_RNG_H
#define LOCKER_LOCAL_RNG_H

#undef RNG

#include <stddef.h>
#include <stdint.h>

class RNGClass {
public:
    void rand(uint8_t *data, size_t len);
};

extern RNGClass RNG;

#endif
