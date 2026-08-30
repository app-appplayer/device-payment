/* Unmanned locker — the device half of the device-payment chain.
 *
 * Design: device-payment/design/locker-node-2026-08-30.md
 *
 * The reference `led` domain shows declaration → render → tool → physical act.
 * That is remote control. What this domain adds is the part the platform is
 * actually about: a signed voucher, verified HERE with no network, bound to an
 * action this node declared, and an authority interval this node evaluates for
 * itself.
 *
 * Everything refuses by default. There is no path that opens the latch without
 * a voucher that passed every check below, and no "log it and continue".
 *
 * The latch is the on-board LED — this board has no solenoid. Lit means open.
 * Said on screen too, so a demo never implies a lock that is not there.
 */

#include "domain.h"
#include "authority.h"
#include "mcp_writer.h"
#include "json_min.h"
#include "node_board.h"
#include <stdio.h>

/* Supplied by the target — a board's flash, a virtual node's file. The domain
 * only passes them along; where a rental is kept is not a locker's business
 * any more than a signature is. */
extern authority_store_write_fn node_store_write;
extern authority_store_read_fn node_store_read;
#include <string.h>

/* Jump to the chip's USB bootloader. Provided by the board binding; absent on
 * the host build, where reflashing is not a thing. */
void board_enter_dfu(void);

/* Set by sys.dfu, acted on by the tick — so the reply is on the wire before
 * the chip leaves. */
static int s_dfu_requested = 0;

/* The service public key, provisioned at build time. Flash, not a secure
 * element — this board has none, and that gap is written down rather than
 * papered over. Replaced by tools/mint_voucher.dart's key at build time. */
static const unsigned char SERVICE_PUBKEY[32] = {
#include "service_pubkey.inc"
};

/* ---- what this node offers -------------------------------------------- */

/* Rentals. `within_s` is how long after paying you have to actually open,
 * within that session. */
static const authority_offer_t OFFERS[] = {
    { "locker-4h", 4UL * 3600UL, 30UL, AUTHORITY_FIXED },
    { "locker-24h", 24UL * 3600UL, 120UL, AUTHORITY_FIXED },
    /* Short on purpose: an expiry check that takes four hours to run is an
     * expiry check nobody runs. */
    { "locker-demo-20s", 20UL, 10UL, AUTHORITY_FIXED },
};

#define TARGET_REF "B12"

/* How many people this machine can serve at once.
 *
 * A domain answer, not a transport one, and the question it turns on is
 * whether the action OCCUPIES the machine.
 *
 *   vending    — dispensing is over in seconds and the machine is free again.
 *                Order does not matter and several people can be served at
 *                once; nothing is being held.
 *   laundry    — the drum is taken for 42 minutes.
 *   car wash   — the bay is taken.
 *   locker     — the door is taken for four hours.
 *
 * The last three are one-at-a-time not because a wire cannot carry two
 * conversations but because there is one drum, one bay, one door. This node is
 * a locker, so: exclusive.
 *
 * Note it is not read off the performance kind. A wash is `once` and still
 * occupies; a dispense is `once` and does not. Occupancy is its own fact about
 * the machine and only the domain knows it.
 *
 * Declared rather than policed here. A byte stream has no notion of who is
 * speaking — two hosts opening the same serial port simply interleave, and no
 * code in this domain can tell them apart. The domain owes the statement; a
 * transport that HAS peers (TCP, GATT) is where it can be held to. */
/* A rented door is taken for hours; nothing else can be handed it meanwhile.
 * A vending chute is free again in seconds and says `shared`. */
static void latch(int on) { board_led_set(on); }

/* ---- tool: sys.dfu ------------------------------------------------------ */

/* Reflash without touching the board. Only the first flash needs BOOT0 held
 * while NRST is tapped; after that this puts the chip in its own bootloader on
 * command, which is what makes iterating on verification logic bearable.
 *
 * Deliberately NOT gated on an authority: this is a development node and the
 * gate would be theatre — anyone who can reach the wire can also reach the
 * button. A shipping node would not carry this tool at all.
 */
static int tool_sys_dfu(const char* args, size_t args_len, mcp_writer_t* out) {
    (void)args; (void)args_len;
    /* Answered before jumping: nothing returns from board_enter_dfu, and a
     * caller left without a reply cannot tell success from a crash. */
    mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"entering bootloader\"}]");
    s_dfu_requested = 1;
    return 0;
}

/* ---- served documents -------------------------------------------------- */

static char s_ui[512];
static char s_page[2560];
static char s_manifest[768];
static char s_info[512];

static int serve_ui_app(const char* uri, mcp_writer_t* out) {
    (void)uri; return mcp_writer_str(out, s_ui);
}
static int serve_page_main(const char* uri, mcp_writer_t* out) {
    (void)uri; return mcp_writer_str(out, s_page);
}
static int serve_app_info(const char* uri, mcp_writer_t* out) {
    (void)uri; return mcp_writer_str(out, s_info);
}
static int serve_manifest(const char* uri, mcp_writer_t* out) {
    (void)uri; return mcp_writer_str(out, s_manifest);
}

static const mcp_tool_t LOCKER_TOOLS[] = {
    { "voucher.present",
      "Present a signed voucher. Verified on the device, offline.",
      "{\"type\":\"object\",\"properties\":{"
        "\"deviceId\":{\"type\":\"string\"},\"action\":{\"type\":\"string\"},"
        "\"notBefore\":{\"type\":\"integer\"},\"notAfter\":{\"type\":\"integer\"},"
        "\"target\":{\"type\":\"string\"},\"session\":{\"type\":\"integer\"},"
        "\"sig\":{\"type\":\"string\"}},"
      "\"required\":[\"deviceId\",\"action\",\"notBefore\",\"notAfter\","
        "\"target\",\"session\",\"sig\"]}",
      authority_present },
    { "locker.open", "Open, if the held authority still stands",
      "{\"type\":\"object\",\"properties\":{}}", authority_act },
    { "locker.status", "Lock state, remaining interval and the last reason",
      "{\"type\":\"object\",\"properties\":{}}", authority_status },
    { "time.sync", "Present the current time at this proximity event",
      "{\"type\":\"object\",\"properties\":{\"epoch\":{\"type\":\"integer\"}},"
      "\"required\":[\"epoch\"]}", authority_time_sync },
    { "sys.dfu", "Enter the chip bootloader so the next flash needs no button",
      "{\"type\":\"object\",\"properties\":{}}", tool_sys_dfu },
};
static const size_t LOCKER_TOOL_COUNT =
    sizeof(LOCKER_TOOLS) / sizeof(LOCKER_TOOLS[0]);

static const mcp_resource_t LOCKER_RESOURCES[] = {
    { "ui://app", "application/json", serve_ui_app },
    { "ui://page/main", "application/json", serve_page_main },
    { "ui://app/info", "application/json", serve_app_info },
    { "bundle://manifest.json", "application/json", serve_manifest },
    { "state://locker", "application/json", authority_state_json },
};
static const size_t LOCKER_RESOURCE_COUNT =
    sizeof(LOCKER_RESOURCES) / sizeof(LOCKER_RESOURCES[0]);

void domain_init(mcp_server_t* s, mcp_transport_t transport,
                 const char* id, const char* name, const char* version,
                 const char* trust) {
    const authority_config_t cfg = {
        .device_id = id,
        .target = TARGET_REF,
        .offers = OFFERS,
        .offer_count = (int)(sizeof(OFFERS) / sizeof(OFFERS[0])),
        .service_pubkey = SERVICE_PUBKEY,
        .occupancy = AUTHORITY_EXCLUSIVE,
        .store_write = node_store_write,
        .store_read = node_store_read,
        .actuate = latch,
    };
    authority_init(&cfg);


    snprintf(s_ui, sizeof(s_ui),
        "{\"type\":\"application\",\"title\":\"%s\","
          "\"routes\":{\"/\":\"ui://page/main\"},\"initialRoute\":\"/\"}",
        name);

    /* The page states the terms and says plainly what this node is not: the
     * key sits in flash, and the latch is an LED. A demo that implied a secure
     * element or a real lock would be the expensive kind of demo. */
    const int page_len = snprintf(s_page, sizeof(s_page),
        "{\"type\":\"page\",\"title\":\"%s\","
          "\"lifecycle\":{"
            "\"onReady\":[{\"type\":\"resource\",\"action\":\"subscribe\","
                "\"uri\":\"state://locker\",\"binding\":\"locker\"}],"
            "\"onDestroy\":[{\"type\":\"resource\",\"action\":\"unsubscribe\","
                "\"uri\":\"state://locker\"}]},"
          "\"state\":{\"initial\":{\"locker\":{\"remaining_s\":0,"
            "\"reason\":\"no voucher presented\"},\"seller\":\"%s\"}},"
          "\"content\":{\"type\":\"singleChildScrollView\",\"child\":{"
            "\"type\":\"linear\",\"direction\":\"vertical\",\"padding\":20,"
            "\"spacing\":14,\"children\":["
              "{\"type\":\"text\",\"text\":\"Locker %s\",\"variant\":\"headlineSmall\"},"
              "{\"type\":\"text\",\"text\":\"Medium locker. The door opens while the "
                "authority lasts, and stops on its own.\",\"variant\":\"bodyMedium\"},"
              "{\"type\":\"text\",\"text\":\"%s - one person at a time - key in flash, "
                "no secure element - latch is the on-board LED\","
                "\"variant\":\"labelSmall\"},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":4,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"Right now\",\"variant\":\"labelLarge\"},"
                  "{\"type\":\"text\",\"text\":\"{{locker.remaining_s}} s remaining\","
                    "\"variant\":\"displaySmall\"},"
                  "{\"type\":\"text\",\"text\":\"{{locker.reason}}\",\"variant\":\"bodySmall\"}"
                "]}},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":8,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"4 hours\",\"variant\":\"titleMedium\"},"
                  "{\"type\":\"text\",\"text\":\"Opens as often as you like until it "
                    "runs out.\",\"variant\":\"bodySmall\"},"
                  "{\"type\":\"button\",\"label\":\"Pay - 3,000 KRW\","
                    "\"onTap\":{\"type\":\"payment\",\"seller\":\"{{seller}}\","
                      "\"itemId\":\"locker-4h\"}}"
                "]}},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":8,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"One day\",\"variant\":\"titleMedium\"},"
                  "{\"type\":\"text\",\"text\":\"Same locker, until this time "
                    "tomorrow.\",\"variant\":\"bodySmall\"},"
                  "{\"type\":\"button\",\"label\":\"Pay - 6,000 KRW\","
                    "\"onTap\":{\"type\":\"payment\",\"seller\":\"{{seller}}\","
                      "\"itemId\":\"locker-24h\"}}"
                "]}},"
              "{\"type\":\"button\",\"label\":\"Open\","
                "\"onTap\":{\"type\":\"tool\",\"tool\":\"locker.open\"}}"
            "]}}}",
        name, "euid_svLR9zNzvMlm4Db0tuEa", TARGET_REF, id);
    /* Loud, not silent: see authority_page_fits. */
    authority_page_fits(s_page, sizeof(s_page), page_len, name);

    if (trust && trust[0]) {
        snprintf(s_manifest, sizeof(s_manifest),
            "{\"manifest\":{\"id\":\"%s\",\"name\":\"%s\",\"version\":\"%s\","
            "\"entryPoint\":\"ui://app\",\"concurrency\":\"%s\",\"trust\":%s},"
            "\"ui\":{\"entry\":\"ui://app\"}}",
            id, name, version, authority_occupancy_name(), trust);
    } else {
        snprintf(s_manifest, sizeof(s_manifest),
            "{\"manifest\":{\"id\":\"%s\",\"name\":\"%s\",\"version\":\"%s\","
            "\"entryPoint\":\"ui://app\",\"concurrency\":\"%s\"},"
            "\"ui\":{\"entry\":\"ui://app\"}}",
            id, name, version, authority_occupancy_name());
    }

    snprintf(s_info, sizeof(s_info),
        "{\"id\":\"%s\",\"name\":\"%s\",\"version\":\"%s\","
        "\"description\":\"Unmanned locker - voucher verified on the device, "
        "interval judged locally.\"}", id, name, version);

    mcp_server_init(s, name, version,
                    LOCKER_TOOLS, LOCKER_TOOL_COUNT,
                    LOCKER_RESOURCES, LOCKER_RESOURCE_COUNT,
                    transport);
}

/* The link dropped. The platform decides what survives it. */
void domain_session_reset(void) { authority_session_reset(); }

/* Push the countdown once a second while someone is subscribed, and drop the
 * latch the moment the interval ends. Expiry is not an event that arrives —
 * it is the absence of one, noticed here. */
void domain_tick(mcp_server_t* s) {
    if (s_dfu_requested) {
        s_dfu_requested = 0;
        board_enter_dfu(); /* does not return */
    }
    authority_tick();
    static unsigned long last_s = 0;
    unsigned long t = board_uptime_ms() / 1000UL;
    if (t == last_s) return;
    last_s = t;
    mcp_server_notify_resource_updated(s, "state://locker");
}
