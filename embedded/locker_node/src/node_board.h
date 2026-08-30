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

/* Milliseconds since boot (monotonic). */
unsigned long board_uptime_ms(void);

#endif /* BOARD_H */
