/* Parking barrier — the renewing branch.
 *
 * The first two domains sell a fixed thing: four hours, one coffee. This one
 * sells entry to a state at a rate. Nothing is bought in advance beyond the
 * first half hour; the authority renews by itself while the car is in the bay,
 * and the release is what fixes the total.
 *
 * Three properties come with that shape and none of them are optional:
 *
 *   - the same gesture does both transitions. Tap on the way in, tap on the
 *     way out. There is no second procedure and no attendant.
 *   - release is never conditioned on what is owed. A barrier that holds a car
 *     hostage until the bill clears is the coin locker demanding cash from
 *     someone collecting their things.
 *   - the accumulation is counted while nobody is there. That is the whole
 *     measurement, and it is why this machine needs continuous power and a
 *     locker does not.
 *
 * The platform holds all three. This file says what a car park is.
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

static const unsigned char SERVICE_PUBKEY[32] = {
#include "service_pubkey.inc"
};

/* This machine's own identity key. Different key, different job: it says which
 * machine is speaking and cannot mint an authority. Not committed — a
 * published device key is a device anyone can impersonate. */
static const unsigned char DEVICE_PRIVKEY[32] = {
#include "device_privkey.inc"
};

/* Renewing: `seconds` is the unit period, not a duration. Half an hour is the
 * rate a driver is quoted, so it is the rate the machine counts in. */
static const authority_offer_t OFFERS[] = {
    { "park-hourly", 30UL * 60UL, 0UL, AUTHORITY_RENEWING },
    /* Short unit so the accumulation can be watched rather than described. */
    { "park-demo-2s", 2UL, 0UL, AUTHORITY_RENEWING },
    /* The same barrier also sells a flat day — a fixed authority beside a
     * renewing one, on one machine, because the shapes are per offer and not
     * per node. */
    { "park-flat-day", 12UL * 3600UL, 60UL, AUTHORITY_FIXED },
};

#define TARGET_REF "GATE1"

/* No session window on the renewing offers. A window bounds the gap between
 * authorising and acting, which makes sense when someone is standing there; a
 * car that entered at nine and leaves at five is not late for anything. */

/* The boom. Raised means the bay is taken — on this board the LED again. */
static void boom(int on) { board_led_set(on); }

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

static const mcp_tool_t PARKING_TOOLS[] = {
    { "voucher.present",
      "Present a signed voucher. Entering if the bay is free, releasing if it "
      "is taken — the same gesture either way.",
      "{\"type\":\"object\",\"properties\":{"
        "\"deviceId\":{\"type\":\"string\"},\"action\":{\"type\":\"string\"},"
        "\"notBefore\":{\"type\":\"integer\"},\"notAfter\":{\"type\":\"integer\"},"
        "\"target\":{\"type\":\"string\"},\"session\":{\"type\":\"integer\"},"
        "\"sig\":{\"type\":\"string\"}},"
      "\"required\":[\"deviceId\",\"action\",\"notBefore\",\"notAfter\","
        "\"target\",\"session\",\"sig\"]}",
      authority_present },
    { "gate.raise", "Raise the boom, if the held authority still stands",
      "{\"type\":\"object\",\"properties\":{}}", authority_act },
    { "gate.status", "Bay state, units accumulated and the last reason",
      "{\"type\":\"object\",\"properties\":{}}", authority_status },
    { "device.assert",
      "This device's signed identity assertion, to be relayed unchanged",
      "{\"type\":\"object\",\"properties\":{}}", authority_assert },
    { "time.sync", "Present the current time at this proximity event",
      "{\"type\":\"object\",\"properties\":{\"epoch\":{\"type\":\"integer\"}},"
      "\"required\":[\"epoch\"]}", authority_time_sync },
};

static const mcp_resource_t PARKING_RESOURCES[] = {
    { "ui://app", "application/json", serve_ui_app },
    { "ui://page/main", "application/json", serve_page_main },
    { "ui://app/info", "application/json", serve_app_info },
    { "bundle://manifest.json", "application/json", serve_manifest },
    { "state://parking", "application/json", authority_state_json },
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
        /* One bay, one car. */
        .occupancy = AUTHORITY_EXCLUSIVE,
        .store_write = node_store_write,
        .store_read = node_store_read,
        .actuate = boom,
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
                "\"uri\":\"state://parking\",\"binding\":\"bay\"}],"
            "\"onDestroy\":[{\"type\":\"resource\",\"action\":\"unsubscribe\","
                "\"uri\":\"state://parking\"}]},"
          "\"state\":{\"initial\":{\"bay\":{\"remaining_s\":0,"
            "\"reason\":\"bay free\"},\"seller\":\"%s\"}},"
          "\"content\":{\"type\":\"singleChildScrollView\",\"child\":{"
            "\"type\":\"linear\",\"direction\":\"vertical\",\"padding\":20,"
            "\"spacing\":14,\"children\":["
              "{\"type\":\"text\",\"text\":\"Gate %s\",\"variant\":\"headlineSmall\"},"
              "{\"type\":\"text\",\"text\":\"Tap on the way in and again on the way "
                "out. Only the time you used is settled.\",\"variant\":\"bodyMedium\"},"
              "{\"type\":\"text\",\"text\":\"%s - one car at a time - leaving is never "
                "held for payment - boom is the on-board LED\","
                "\"variant\":\"labelSmall\"},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":4,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"Right now\",\"variant\":\"labelLarge\"},"
                  "{\"type\":\"text\",\"text\":\"{{bay.reason}}\",\"variant\":\"bodyMedium\"}"
                "]}},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":8,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"Park now\",\"variant\":\"titleMedium\"},"
                  "{\"type\":\"text\",\"text\":\"1,200 KRW per 30 min. Nothing is "
                    "bought ahead beyond the first.\",\"variant\":\"bodySmall\"},"
                  "{\"type\":\"button\",\"label\":\"Pay - 1,200 KRW / 30 min\","
                    "\"onTap\":{\"type\":\"payment\",\"seller\":\"{{seller}}\","
                      "\"itemId\":\"park-hourly\"}}"
                "]}},"
              "{\"type\":\"card\",\"child\":{\"type\":\"linear\","
                "\"direction\":\"vertical\",\"padding\":16,\"spacing\":8,"
                "\"children\":["
                  "{\"type\":\"text\",\"text\":\"All day, flat\",\"variant\":\"titleMedium\"},"
                  "{\"type\":\"button\",\"label\":\"Pay - 12,000 KRW\","
                    "\"onTap\":{\"type\":\"payment\",\"seller\":\"{{seller}}\","
                      "\"itemId\":\"park-flat-day\"}}"
                "]}},"
              "{\"type\":\"button\",\"label\":\"Raise\","
                "\"onTap\":{\"type\":\"tool\",\"tool\":\"gate.raise\"}}"
            "]}}}",
        name, "euid_svLR9zNzvMlm4Db0tuEa", TARGET_REF, id);
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
        "\"description\":\"Parking barrier - renewing authority, released by "
        "the same gesture that started it.\"}", id, name, version);

    mcp_server_init(s, name, version,
                    PARKING_TOOLS,
                    sizeof(PARKING_TOOLS) / sizeof(PARKING_TOOLS[0]),
                    PARKING_RESOURCES,
                    sizeof(PARKING_RESOURCES) / sizeof(PARKING_RESOURCES[0]),
                    transport);
}

void domain_session_reset(void) { authority_session_reset(); }

void domain_tick(mcp_server_t* s) {
    authority_tick();
    static unsigned long last_s = 0;
    unsigned long t = board_uptime_ms() / 1000UL;
    if (t == last_s) return;
    last_s = t;
    mcp_server_notify_resource_updated(s, "state://parking");
}
