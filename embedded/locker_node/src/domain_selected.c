/* The one domain this build links.
 *
 * A target links exactly one domain — the platform is shared, the machine is
 * not. Arduino compiles everything under `src/` with no way to exclude a file,
 * so two domains sitting there means two `domain_init` symbols and a link that
 * fails at the end of a long build. It did.
 *
 * So the domains live outside `src/` and this single translation unit pulls in
 * whichever one the build names:
 *
 *   -DDOMAIN_SOURCE='"../domains/vending.c"'
 *
 * Defaults to the locker, so an unqualified build is always the machine the
 * design document describes.
 */
#ifndef DOMAIN_SOURCE
#define DOMAIN_SOURCE "../domains/payment_locker.c"
#endif

#include DOMAIN_SOURCE
