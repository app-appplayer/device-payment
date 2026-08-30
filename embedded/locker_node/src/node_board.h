#ifndef BOARD_H
#define BOARD_H

/* HAL contract — the on-board LED capability the LED domain talks to.
 *
 * The domain (tools, resources, served UI) talks only to this interface, so the
 * same domain is verified on the host with a mock and runs on-device against a
 * platform binding. Each platform implements it (platforms/<p>/hal/…), and the
 * host_test provides an in-memory mock. Richer boards expose more capabilities
 * through additional contract headers (a provider-registry model), never by
 * widening this one. */

/* Drive the on-board LED on (non-zero) or off (zero). */
void board_led_set(int on);

/* Return the last commanded LED state (1 on, 0 off). */
int board_led_get(void);

/* Unpredictable bytes for a device nonce. A counter will not do: the value
 * exists so that an assertion captured from this device cannot be replayed on
 * its behalf, and a predictable one is a replayable one. */
/* Returns non-zero when it filled the buffer. A machine that cannot draw one
 * must refuse to assert rather than assert something predictable — and must
 * not hang trying, which takes the whole node with it. */
int board_random(unsigned char* out, unsigned long len);

/* Milliseconds since boot (monotonic). */
unsigned long board_uptime_ms(void);

#endif /* BOARD_H */
