/* Runs the locker domain on this machine, over stdin/stdout.
 *
 *   make -C host && ./host/locker_host
 *
 * Same domain C the board runs — only the HAL and the byte source differ. The
 * point is to get the refusal rules right before touching hardware: every
 * check in the design's verification plan except the ones that need a latch
 * can be exercised here, and a flash cycle costs a physical button press.
 */

#include "domain.h"
#include "mcp_stream_serve.h"
#include "node_board.h"
#include "authority.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int s_led = 0;
void board_led_set(int on) { s_led = on ? 1 : 0; }
int board_led_get(void) { return s_led; }

/* There is nothing to reflash on this machine. The node stops serving, which
 * is the observable truth of "the chip left" — returning quietly would make
 * `sys.dfu` report success for something that did not happen. */
void board_enter_dfu(void) {
    _exit(70);
}

unsigned long board_uptime_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    static unsigned long base = 0;
    unsigned long now = (unsigned long)tv.tv_sec * 1000UL +
                        (unsigned long)(tv.tv_usec / 1000);
    if (base == 0) base = now;
    return now - base;
}

/* Where a virtual node keeps what must outlive it. A file, because a process
 * that is killed and restarted is this build's power cut and the check that
 * matters is what comes back. Path from the environment so two machines on one
 * desk do not share a rental. */
static const char* store_path(void) {
    const char* p = getenv("NODE_STORE");
    return p ? p : "/tmp/device-payment-node.store";
}

static void host_store_write(const void* data, unsigned long len) {
    FILE* f = fopen(store_path(), "wb");
    if (!f) return;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
}

static int host_store_read(void* data, unsigned long len) {
    FILE* f = fopen(store_path(), "rb");
    if (!f) return 0;
    const size_t n = fread(data, 1, (size_t)len, f);
    fclose(f);
    return n == (size_t)len;
}

/* Handed to the domain, which hands them to the platform. */
authority_store_write_fn node_store_write = host_store_write;
authority_store_read_fn node_store_read = host_store_read;

static mcp_stream_serve_t s_serve;

/* stdout for the stdio mode, a socket for the TCP one. The domain and the
 * server never learn which. */
static int s_peer_fd = -1;

static int send_sink(void* ctx, const uint8_t* data, size_t len) {
    (void)ctx;
    if (s_peer_fd >= 0) {
        ssize_t n = write(s_peer_fd, data, len);
        return n < 0 ? -1 : (int)n;
    }
    fwrite(data, 1, len, stdout);
    fflush(stdout);
    return (int)len;
}

/* Identity is injected by the target, and on a board that is a line in the
 * sketch. A virtual node has no sketch, so it takes it from the environment —
 * which is also what lets one binary stand in for a fleet of the same machine
 * at different addresses.
 *
 * The device id defaults to the board's so a voucher minted for the board
 * works here unchanged; that is the point of running the same domain twice. */
static void app_init(mcp_server_t* s, mcp_transport_t t) {
    const char* id = getenv("NODE_ID");
    const char* name = getenv("NODE_NAME");
    domain_init(s, t,
                id ? id : "stm32.h723",
                name ? name : "Virtual node",
                "0.1.0", 0);
}

/* Serve one peer at a time over ndjson/TCP — the board binding
 * (`specs/platform/17-device-discovery.md` §3), so a host dials this exactly
 * as it dials a board.
 *
 * This is what makes a machine that does not exist yet testable: a car wash
 * needs a bay and a laundry needs a drum, but the part being verified is the
 * authority, and that runs the same here as on the chip. The board proves the
 * chip; these prove the fleet.
 *
 * One peer at a time regardless of what the domain declares — the second
 * caller is closed rather than queued. `shared` is a statement about the
 * machine's occupancy, not a promise that this development harness multiplexes
 * sessions. */
static int serve_tcp(int port) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return 1;
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Loopback only: this node has no authentication. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) return 1;
    if (listen(srv, 1) != 0) return 1;
    /* Flushed, because a harness waits for this line before it dials. */
    fprintf(stderr, "listening tcp://127.0.0.1:%d\n", port);
    fflush(stderr);

    for (;;) {
        int peer = accept(srv, 0, 0);
        if (peer < 0) continue;
        s_peer_fd = peer;
        uint8_t buf[512];
        for (;;) {
            /* Polled rather than blocked on the read.
             *
             * A board runs a super-loop: it ticks whether or not anything
             * arrived, which is how live state keeps flowing and how an
             * interval expires with nobody talking. A blocking read here made
             * this node tick only when spoken to — the screen kept showing the
             * reason from before the last call, and the expiry that the whole
             * design turns on would never fire on its own. */
            struct pollfd pfd = { .fd = peer, .events = POLLIN };
            int ready = poll(&pfd, 1, 100);
            if (ready < 0) break;
            if (ready > 0) {
                ssize_t n = read(peer, buf, sizeof(buf));
                if (n <= 0) break;
                for (ssize_t i = 0; i < n; i++) {
                    mcp_stream_serve_feed(&s_serve, &buf[i], 1);
                }
            }
            domain_tick(&s_serve.server);
        }
        close(peer);
        s_peer_fd = -1;
        /* The peer went away. Framing first, then the domain — a half-read
         * line must not reach the next caller. */
        mcp_stream_serve_reset(&s_serve);
        domain_session_reset();
    }
}

int main(int argc, char** argv) {
    mcp_stream_serve_init(&s_serve, send_sink, 0, app_init);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0 && i + 1 < argc) {
            return serve_tcp(atoi(argv[i + 1]));
        }
    }

    int c;
    while ((c = getchar()) != EOF) {
        uint8_t b = (uint8_t)c;
        mcp_stream_serve_feed(&s_serve, &b, 1);
        /* The board ticks from its super-loop; here every byte is close
         * enough, and expiry is what the tick is for. */
        domain_tick(&s_serve.server);
    }
    /* EOF is this build's link loss. */
    domain_session_reset();
    return 0;
}
