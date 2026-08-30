#ifndef MCP_TRANSPORT_H
#define MCP_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* Transport porting seam.
 *
 * The base does NOT define which transport carries MCP. A board provides
 * read/write over whatever link it can speak (UART, USB CDC, RS-485, SPI,
 * I2C, BLE, Wi-Fi/WebSocket, ...); the contract above this seam is unchanged.
 *
 * Both calls are non-blocking by contract: read() returns 0 when no bytes are
 * available rather than waiting, so mcp_server_process() can be called from a
 * cooperative super-loop without stalling the board's real-time work. */
typedef struct mcp_transport {
    /* Read up to `max` bytes into `buf`. Returns the count read (0 if none
     * available), or a negative value on error. */
    int (*read)(void* ctx, uint8_t* buf, size_t max);

    /* Write `len` bytes from `buf`. Returns the count written, or negative on
     * error. */
    int (*write)(void* ctx, const uint8_t* buf, size_t len);

    /* Opaque per-board context passed back to read/write. */
    void* ctx;
} mcp_transport_t;

#endif /* MCP_TRANSPORT_H */
