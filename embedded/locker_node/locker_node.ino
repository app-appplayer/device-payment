/* Unmanned locker node — WeAct MiniH723VGTX over USB CDC.
 *
 * Design: device-payment/design/locker-node-2026-08-30.md
 *
 * Same shape as the reference stm32 target: Arduino's cooperative super-loop
 * feeds Serial bytes to the shared stream server, and the domain / server /
 * framing are the same C the posix and esp32 nodes use. Only the domain
 * differs — this one verifies vouchers instead of toggling a light on request.
 *
 * Built here rather than in embedded/mcp_node/.../domains/samples/ so the
 * reference tree keeps one meaning. This node belongs to the device-payment
 * axis and is managed with the rest of its evidence.
 */

extern "C" {
#include "domain.h"
#include "mcp_stream_serve.h"
void board_arduino_init(void);
}

/* Whether a host currently has the CDC port open. USB CDC carries DTR, and
 * STM32duino surfaces it as `Serial`'s truthiness — which is the only session
 * boundary this wire has. Without watching it the node cannot tell a peer that
 * left from a peer that is simply quiet, and anything waiting on the link
 * would outlive the link. */
static bool s_peer_present = false;

static mcp_stream_serve_t s_serve;

/* Identity injected here — one domain serves under any node identity. `trust`
 * is null: this board has no secure element and the key it verifies against
 * sits in flash, so claiming a trust block would be claiming a property the
 * hardware does not have. */
static void app_init(mcp_server_t* s, mcp_transport_t t) {
    domain_init(s, t, "stm32.h723", "Locker B12", "0.1.0", 0);
}

static int send_sink(void* ctx, const uint8_t* data, size_t len) {
    (void)ctx;
    return (int)Serial.write(data, len);
}

void setup() {
    Serial.begin(115200);
    board_arduino_init();
    mcp_stream_serve_init(&s_serve, send_sink, 0, app_init);
}

void loop() {
    const bool peer = (bool)Serial;
    if (peer != s_peer_present) {
        s_peer_present = peer;
        if (!peer) {
            /* Framing first, then the domain: a half-read line from the peer
             * that left must not be handed to the one that arrives next. */
            mcp_stream_serve_reset(&s_serve);
            domain_session_reset();
        }
    }

    while (Serial.available() > 0) {
        uint8_t b = (uint8_t)Serial.read();
        mcp_stream_serve_feed(&s_serve, &b, 1);
    }
    domain_tick(&s_serve.server);
}
