/* Vending machine.
 *
 * The second domain on the same platform, and the reason `authority.c` exists.
 * Nothing about vouchers, signatures, replay or intervals appears here — that
 * is settled once and this file only says what a vending machine is:
 *
 *   - what it sells and for how long the authority to take it lasts
 *   - that the chute is free again in seconds, so it is NOT one-at-a-time
 *   - that acting means running the auger
 *
 * The contrast with the locker is the point. Both are `once`-and-done in the
 * sense that you pay and take, yet one occupies the machine for hours and the
 * other for a moment. Occupancy is its own fact and only the domain knows it,
 * which is why the platform asks rather than infers.
 */

#include "domain.h"
#include "authority.h"
#include "mcp_writer.h"
#include "node_board.h"
#include <stdio.h>

/* Supplied by the target — a board's flash, a virtual node's file. The domain
 * only passes them along; where a rental is kept is not a locker's business
 * any more than a signature is. */
extern authority_store_write_fn node_store_write;
extern authority_store_read_fn node_store_read;

#include "node_keys.h"


/* Short authorities, short windows: you are standing at the machine. A minute
 * to take what you paid for is generous; an hour would mean a stranger's
 * unclaimed drink sitting in the machine's memory all afternoon. */
static const authority_offer_t OFFERS[] = {
    { "vend-water", 60UL, 60UL, AUTHORITY_FIXED },
    { "vend-coffee", 60UL, 60UL, AUTHORITY_FIXED },
    { "vend-demo-20s", 20UL, 10UL, AUTHORITY_FIXED },
};

/* Slot, not bay. A voucher naming another slot is refused. */
#define TARGET_REF "A3"

/* The auger runs for a moment and the chute is clear again. On this board the
 * LED stands in for it, exactly as the locker's latch does — the difference
 * between the two machines is not the actuator, it is how long it holds. */
static void auger(int on) { board_led_set(on); }

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

static const mcp_tool_t VENDING_TOOLS[] = {
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
    { "vend.dispense", "Dispense, if the held authority still stands",
      "{\"type\":\"object\",\"properties\":{}}", authority_act },
    { "vend.status", "Slot state, remaining authority and the last reason",
      "{\"type\":\"object\",\"properties\":{}}", authority_status },
    { "device.assert",
      "This device's signed identity assertion, to be relayed unchanged",
      "{\"type\":\"object\",\"properties\":{}}", authority_assert },
    { "time.sync", "Present the current time at this proximity event",
      "{\"type\":\"object\",\"properties\":{\"epoch\":{\"type\":\"integer\"}},"
      "\"required\":[\"epoch\"]}", authority_time_sync },
};

static const mcp_resource_t VENDING_RESOURCES[] = {
    { "ui://app", "application/json", serve_ui_app },
    { "ui://page/main", "application/json", serve_page_main },
    { "ui://app/info", "application/json", serve_app_info },
    { "bundle://manifest.json", "application/json", serve_manifest },
    { "state://vending", "application/json", authority_state_json },
};

void domain_init(mcp_server_t* s, mcp_transport_t transport,
                 const char* id, const char* name, const char* version,
                 const char* trust) {
    const authority_config_t cfg = {
        .device_id = id,
        .target = TARGET_REF,
        .offers = OFFERS,
        .offer_count = (int)(sizeof(OFFERS) / sizeof(OFFERS[0])),
        .service_pubkey = SERVICE_PUBKEY,
        .device_privkey = DEVICE_PRIVKEY,
        /* Filled after the page is rendered: the hash covers what this node
         * actually serves, so it cannot be taken before there is one. */
        .spec = 0,
        /* The difference from the locker, in one line. */
        .occupancy = AUTHORITY_SHARED,
        .store_write = node_store_write,
        .store_read = node_store_read,
        .actuate = auger,
    };
    authority_init(&cfg);

    snprintf(s_ui, sizeof(s_ui),
        "{\"type\":\"application\",\"title\":\"%s\","
          "\"routes\":{\"/\":\"ui://page/main\"},\"initialRoute\":\"/\"}",
        name);

    const int page_len = snprintf(s_page, sizeof(s_page),
        "{\"type\":\"page\",\"title\":\"%s\","
          "\"lifecycle\":{"
            "\"onReady\":[{\"type\":\"resource\",\"action\":\"subscribe\","
                "\"uri\":\"state://vending\",\"binding\":\"slot\"}],"
            "\"onDestroy\":[{\"type\":\"resource\",\"action\":\"unsubscribe\","
                "\"uri\":\"state://vending\"}]},"
          "\"state\":{\"initial\":{\"slot\":{\"remaining_s\":0,"
            "\"reason\":\"nothing paid for\"},\"seller\":\"%s\"}},"
          "\"content\":{\"type\":\"singleChildScrollView\",\"child\":{"
            "\"type\":\"linear\",\"direction\":\"vertical\",\"padding\":20,"
            "\"spacing\":14,\"children\":["
              "{\"type\":\"text\",\"text\":\"Slot %s\",\"variant\":\"headlineSmall\"},"
              "{\"type\":\"text\",\"text\":\"Take it and the machine is free again "
                "- others do not wait behind you.\",\"variant\":\"bodyMedium\"},"
              "{\"type\":\"text\",\"text\":\"%s - several at a time - key in flash, "
                "no secure element - auger is the on-board LED\","
                "\"variant\":\"labelSmall\"},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":4,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"Right now\",\"variant\":\"labelLarge\"},"
                  "{\"type\":\"text\",\"text\":\"{{slot.reason}}\",\"variant\":\"bodySmall\"}"
                "]}},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":8,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"Water 500 ml\",\"variant\":\"titleMedium\"},"
                  "{\"type\":\"button\",\"label\":\"Pay - 900 KRW\","
                    "\"onTap\":{\"type\":\"payment\",\"seller\":\"{{seller}}\","
                      "\"itemId\":\"vend-water\"}}"
                "]}},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":8,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"Hot coffee\",\"variant\":\"titleMedium\"},"
                  "{\"type\":\"button\",\"label\":\"Pay - 1,500 KRW\","
                    "\"onTap\":{\"type\":\"payment\",\"seller\":\"{{seller}}\","
                      "\"itemId\":\"vend-coffee\"}}"
                "]}},"
              "{\"type\":\"button\",\"label\":\"Dispense\","
                "\"onTap\":{\"type\":\"tool\",\"tool\":\"vend.dispense\"}}"
            "]}}}",
        name, "euid_svLR9zNzvMlm4Db0tuEa", TARGET_REF, id);
    /* Loud, not silent: see authority_page_fits. */
    authority_page_fits(s_page, sizeof(s_page), page_len, name);
    /* The declaration is now real, so the assertion can cover it. */
    authority_set_spec(s_page);

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
        "\"description\":\"Vending slot - voucher verified on the device.\"}",
        id, name, version);

    mcp_server_init(s, name, version,
                    VENDING_TOOLS,
                    sizeof(VENDING_TOOLS) / sizeof(VENDING_TOOLS[0]),
                    VENDING_RESOURCES,
                    sizeof(VENDING_RESOURCES) / sizeof(VENDING_RESOURCES[0]),
                    transport);
}

void domain_session_reset(void) { authority_session_reset(); }

void domain_tick(mcp_server_t* s) {
    authority_tick();
    static unsigned long last_s = 0;
    unsigned long t = board_uptime_ms() / 1000UL;
    if (t == last_s) return;
    last_s = t;
    mcp_server_notify_resource_updated(s, "state://vending");
}
