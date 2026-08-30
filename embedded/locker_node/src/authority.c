/* Authority — see authority.h. Lifted out of the locker domain once a second
 * machine needed the same thing; the rules did not change in the move, and the
 * host and board suites that pinned them are what says so.
 */

#include "authority.h"
#include "json_min.h"
#include "node_board.h"
#include <stdio.h>
#include <string.h>

/* Ed25519 verification, provided by the C++ shim. Asymmetric on purpose: with
 * a shared secret the device could mint its own authority. */
int locker_ed25519_verify(const unsigned char sig[64],
                          const unsigned char pub[32],
                          const char* msg, unsigned long msg_len);

static authority_config_t s_cfg;

static unsigned long s_max_session = 0;
static int s_have_authority = 0;
static unsigned long s_valid_until_s = 0;
static int s_expired = 0;

static int s_time_known = 0;
static unsigned long s_time_base_s = 0;
static unsigned long s_uptime_base_ms = 0;

static int s_open = 0;
static char s_reason[96] = "no voucher presented";

/* Renewing state. `s_entered_ms` is on the device's own tick because the
 * accumulation must keep running while nobody is present — that is the whole
 * span being measured, and a locker can go days without a phone near it. */
static int s_renewing = 0;
static unsigned long s_unit_s = 0;
static unsigned long s_entered_ms = 0;

static unsigned long s_window_s = 0;
static unsigned long s_window_opened_ms = 0;
static int s_window_live = 0;

static unsigned long now_s(void) {
    return s_time_base_s + (board_uptime_ms() - s_uptime_base_ms) / 1000UL;
}

static void refuse(const char* why) {
    snprintf(s_reason, sizeof(s_reason), "%s", why);
}

static const authority_offer_t* offer_for(const char* action) {
    for (int i = 0; i < s_cfg.offer_count; i++) {
        if (strcmp(s_cfg.offers[i].id, action) == 0) return &s_cfg.offers[i];
    }
    return 0;
}

void authority_init(const authority_config_t* config) {
    s_cfg = *config;
}

const char* authority_occupancy_name(void) {
    return s_cfg.occupancy == AUTHORITY_SHARED ? "shared" : "exclusive";
}

static void write_ulong(mcp_writer_t* out, unsigned long v) {
    char tmp[24]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    char rev[24]; int r = 0;
    while (t) rev[r++] = tmp[--t];
    out->write(out, rev, (size_t)r);
}

static unsigned long parse_ulong(const char* p, size_t len) {
    unsigned long v = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9') break;
        v = v * 10UL + (unsigned long)(p[i] - '0');
    }
    return v;
}

/* base64url → bytes. Returns the byte count, or -1. Used for the signature
 * only, so it is deliberately strict: no padding, no whitespace, no '+' / '/'. */
static int b64url_decode(const char* in, size_t in_len,
                         unsigned char* out, size_t out_cap) {
    unsigned long acc = 0;
    int bits = 0;
    size_t n = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-') v = 62;
        else if (c == '_') v = 63;
        else return -1;
        acc = (acc << 6) | (unsigned long)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= out_cap) return -1;
            out[n++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    return (int)n;
}

/* How far behind its own estimate the node will still accept a correction.
 *
 * A phone reads its clock, then spends the round trip getting here, so the
 * value that arrives is always slightly stale. With a strict `<` the first
 * correction is the only one that is ever accepted — every later one looks
 * like a rollback and the estimate can never be pulled back down, which is the
 * one direction drift needs. Measured on this board: a two-second-old reading
 * was refused.
 *
 * Two seconds buys the round trip and gives away two seconds of an interval
 * that is measured in hours. A real rollback — undoing an expiry — is orders
 * of magnitude larger and still refused. */
#define TIME_BACKWARD_TOLERANCE_S 2UL

/* How far AHEAD of its own reckoning the device will follow a correction.
 *
 * The patent bounds the estimate in both directions and says why: a phone that
 * can push the clock forward without limit can end a stranger's rental, and if
 * the estimate is ever integrated into a bill (a charger, a car wash) a
 * forward jump is not an inconvenience — it is over-metering, and the person
 * pays for it, not the party that presented the time.
 *
 * The limit is derived from the device's own monotonic source, which is what
 * it has: since the last accepted correction its tick says N seconds passed,
 * so a claim of much more than N is refused. The device is not required to be
 * accurate, only monotonic — the absolute position comes from outside and the
 * guarantee that it does not jump comes from inside. Neither alone is enough.
 *
 * Slack is proportional plus a floor: an oscillator drifts by a fraction of
 * elapsed time, while a round trip costs a couple of seconds no matter how
 * long the device has been up. 1/64 is ~1.5%, generous for any crystal and far
 * below the jump needed to skip a four-hour rental. */
#define TIME_FORWARD_FLOOR_S 5UL
#define TIME_FORWARD_DRIFT_SHIFT 6 /* elapsed/64 */

/* The device also refuses to place its estimate before the start of the
 * authority it most recently accepted.
 *
 * Measured: as the state stands this rule cannot fire. The backward rule above
 * catches the same corrections first, because a voucher that was accepted had
 * to be valid at the time — so its window starts at or before the estimate,
 * never after.
 *
 * It is kept because that is a property of the state, not of the rule, and the
 * state is about to change. A real locker must survive a power cut: a rental
 * that vanishes when the mains blink would free every door in the building. The
 * moment the authority is persisted and the time estimate is not, this floor is
 * the only thing standing between a rebooted machine and a first correction
 * placed anywhere the presenter likes. Written now, with the reason, rather
 * than discovered missing then. */
static unsigned long s_accepted_not_before = 0;

/* The proximity event carries the time. A correction that moves the estimate
 * meaningfully BACKWARD is refused: winding the clock back is the cheapest way
 * to undo an expiry, and accepting it would make every interval advisory. */
int authority_time_sync(const char* args, size_t args_len, mcp_writer_t* out) {
    size_t vlen = 0;
    const char* v = args ? json_member(args, args_len, "epoch", &vlen) : 0;
    if (!v) {
        mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"refused: no epoch\"}]");
        return 0;
    }
    unsigned long presented = parse_ulong(v, vlen);
    if (s_time_known && presented + TIME_BACKWARD_TOLERANCE_S < now_s()) {
        mcp_writer_str(out,
            "[{\"type\":\"text\",\"text\":\"refused: time moves backward\"}]");
        return 0;
    }
    /* Not before the authority currently being honoured started. */
    if (presented + TIME_BACKWARD_TOLERANCE_S < s_accepted_not_before) {
        mcp_writer_str(out,
            "[{\"type\":\"text\",\"text\":\"refused: before the accepted "
            "voucher's window\"}]");
        return 0;
    }
    if (s_time_known) {
        /* What this device's own tick says has passed since the last accepted
         * correction, plus what a drifting crystal and a round trip can
         * honestly account for. More than that is a jump, not a correction. */
        const unsigned long elapsed =
            (board_uptime_ms() - s_uptime_base_ms) / 1000UL;
        const unsigned long allowed = s_time_base_s + elapsed +
            (elapsed >> TIME_FORWARD_DRIFT_SHIFT) + TIME_FORWARD_FLOOR_S;
        if (presented > allowed) {
            /* Refused, and the device carries on with its own estimate — the
             * worst outcome is an error, never an authority destroyed or
             * revived. */
            mcp_writer_str(out,
                "[{\"type\":\"text\",\"text\":\"refused: time jumps "
                "forward\"}]");
            return 0;
        }
    }
    s_time_base_s = presented;
    s_uptime_base_ms = board_uptime_ms();
    s_time_known = 1;
    mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"time=");
    write_ulong(out, now_s());
    mcp_writer_str(out, "\"}]");
    return 0;
}

/* The signed byte string, rebuilt HERE from the fields this node just read.
 * Signing a re-serialised JSON document breaks on one space; more importantly,
 * rebuilding it means the node verifies exactly the values it is about to act
 * on — a field the parser ignored is a field the signature never covered. */
static int rebuild_signed_input(char* buf, size_t cap,
                                const char* device_id, const char* action,
                                unsigned long not_before, unsigned long not_after,
                                const char* target, unsigned long session) {
    int n = snprintf(buf, cap, "%s\n%s\n%lu\n%lu\n%s\n%lu",
                     device_id, action, not_before, not_after, target, session);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}


int authority_present(const char* args, size_t args_len,
                                mcp_writer_t* out) {
    char device_id[32] = {0}, action[32] = {0}, target[16] = {0};
    char sig_b64[128] = {0};
    unsigned long not_before = 0, not_after = 0, session = 0;
    size_t vlen = 0;
    const char* v;

    /* 1. shape */
    if (!args) { refuse("no voucher"); goto refused; }
    v = json_member(args, args_len, "deviceId", &vlen);
    if (!v) { refuse("missing deviceId"); goto refused; }
    json_string_copy(v, vlen, device_id, sizeof(device_id));
    v = json_member(args, args_len, "action", &vlen);
    if (!v) { refuse("missing action"); goto refused; }
    json_string_copy(v, vlen, action, sizeof(action));
    v = json_member(args, args_len, "target", &vlen);
    if (!v) { refuse("missing target"); goto refused; }
    json_string_copy(v, vlen, target, sizeof(target));
    v = json_member(args, args_len, "notBefore", &vlen);
    if (!v) { refuse("missing notBefore"); goto refused; }
    not_before = parse_ulong(v, vlen);
    v = json_member(args, args_len, "notAfter", &vlen);
    if (!v) { refuse("missing notAfter"); goto refused; }
    not_after = parse_ulong(v, vlen);
    v = json_member(args, args_len, "session", &vlen);
    if (!v) { refuse("missing session"); goto refused; }
    session = parse_ulong(v, vlen);
    v = json_member(args, args_len, "sig", &vlen);
    if (!v) { refuse("missing sig"); goto refused; }
    json_string_copy(v, vlen, sig_b64, sizeof(sig_b64));

    /* 2. signature — before anything else is believed, and with no network */
    {
        unsigned char sig[64];
        if (b64url_decode(sig_b64, strlen(sig_b64), sig, sizeof(sig)) != 64) {
            refuse("bad signature encoding");
            goto refused;
        }
        char signed_input[192];
        int n = rebuild_signed_input(signed_input, sizeof(signed_input),
                                     device_id, action, not_before, not_after,
                                     target, session);
        if (n < 0) { refuse("voucher too long"); goto refused; }
        if (!locker_ed25519_verify(sig, s_cfg.service_pubkey, signed_input,
                                   (unsigned long)n)) {
            refuse("signature does not verify");
            goto refused;
        }
    }

    /* 3. this device */
    if (strcmp(device_id, s_cfg.device_id) != 0) {
        refuse("voucher is for another device");
        goto refused;
    }

    /* 4. an action this node declared */
    {
        const authority_offer_t* offer = offer_for(action);
        if (!offer) {
            refuse("action was never offered by this node");
            goto refused;
        }
        s_window_s = offer->within_s;
        s_renewing = (offer->shape == AUTHORITY_RENEWING);
        s_unit_s = offer->seconds;
    }

    /* 5. this target */
    if (strcmp(target, s_cfg.target) != 0) {
        refuse("voucher is for another target");
        goto refused;
    }

    /* 6. replay — from state this device holds */
    if (session <= s_max_session) {
        refuse("session already used");
        goto refused;
    }

    /* 7. the interval, judged locally. Unknown time is not a reason to open. */
    if (!s_time_known) { refuse("no time yet — cannot judge the interval"); goto refused; }
    {
        unsigned long t = now_s();
        if (t < not_before) { refuse("not yet valid"); goto refused; }
        if (t > not_after) { refuse("expired"); goto refused; }
    }

    /* The same gesture does both transitions. A verified presentation to an
     * occupied machine RELEASES it — there is no second procedure to learn and
     * no attendant to find.
     *
     * Release is not conditioned on what is owed, and that is a requirement
     * rather than a kindness: holding the door until the bill is settled would
     * rebuild the coin locker that demands cash from someone who came to
     * collect their things. The device has no notion of money to consult even
     * if it wanted to. Every other refusal above still applies — a release is
     * a VERIFIED presentation, not an unchecked one. */
    if (s_renewing && s_have_authority) {
        const unsigned long units = authority_units();
        s_have_authority = 0;
        s_open = 0;
        s_window_live = 0;
        if (s_cfg.actuate) s_cfg.actuate(0);
        s_max_session = session;
        snprintf(s_reason, sizeof(s_reason), "released after %lu unit(s)",
                 units);
        mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"released units=");
        write_ulong(out, units);
        mcp_writer_str(out, "\"}]");
        return 0;
    }

    s_max_session = session;
    s_accepted_not_before = not_before;
    s_have_authority = 1;
    s_entered_ms = board_uptime_ms();
    s_expired = 0;
    /* The window starts at acceptance, not at connect: what it bounds is the
     * gap between authorising and acting. */
    s_window_opened_ms = board_uptime_ms();
    s_window_live = (s_window_s > 0);
    s_valid_until_s = not_after;
    if (s_renewing) {
        /* No "until". Saying one would name a moment this authority does not
         * have — it renews until someone releases it, and a screen promising
         * an end time promises the wrong thing. */
        snprintf(s_reason, sizeof(s_reason), "accepted %s - renewing until "
                 "released", action);
    } else {
        snprintf(s_reason, sizeof(s_reason), "accepted %s until %lu", action,
                 not_after);
    }
    mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"accepted ");
    mcp_writer_str(out, action);
    mcp_writer_str(out, "\"}]");
    return 0;

refused:
    mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"refused: ");
    mcp_writer_str(out, s_reason);
    mcp_writer_str(out, "\"}]");
    return 0;
}

/* Nothing is asked of anyone here. The interval either still stands by this
 * device's own reckoning or it does not, and when it does not the locker stops
 * opening without receiving any message saying so. */
int authority_act(const char* args, size_t args_len,
                            mcp_writer_t* out) {
    (void)args; (void)args_len;
    if (!s_have_authority) {
        refuse(s_expired ? "authority expired" : "no authority held");
    } else if (!s_time_known) {
        refuse("no time yet — cannot judge the interval");
    } else if (!s_renewing && now_s() > s_valid_until_s) {
        s_have_authority = 0;
        s_expired = 1;
        refuse("authority expired");
    } else if (s_window_live &&
               board_uptime_ms() - s_window_opened_ms > s_window_s * 1000UL) {
        /* The authority is untouched — it is this session's chance to use it
         * that has passed. Present again on a new link and the door opens. */
        s_window_live = 0;
        refuse("session window closed — present again");
    } else {
        s_open = 1;
        if (s_cfg.actuate) s_cfg.actuate(1);
        mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"open\"}]");
        return 0;
    }
    s_open = 0;
    if (s_cfg.actuate) s_cfg.actuate(0);
    mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"refused: ");
    mcp_writer_str(out, s_reason);
    mcp_writer_str(out, "\"}]");
    return 0;
}

static unsigned long remaining_s(void) {
    if (!s_have_authority || !s_time_known) return 0;
    unsigned long t = now_s();
    return (t >= s_valid_until_s) ? 0 : (s_valid_until_s - t);
}

int authority_status(const char* args, size_t args_len,
                              mcp_writer_t* out) {
    (void)args; (void)args_len;
    mcp_writer_str(out, "[{\"type\":\"text\",\"text\":\"");
    mcp_writer_str(out, s_open ? "OPEN" : "LOCKED");
    mcp_writer_str(out, " remaining=");
    /* Through the public accessor, not the internal one: that is where the
     * renewing case is decided, and reading around it produced a screen that
     * counted down an authority which does not expire. */
    write_ulong(out, authority_remaining_s());
    mcp_writer_str(out, "s");
    /* A renewing authority has no remaining — it has a count. Reported beside
     * the zero rather than instead of it, so a reader can tell "nothing left"
     * from "counts instead of counting down". */
    if (s_renewing) {
        mcp_writer_str(out, " units=");
        write_ulong(out, authority_units());
    }
    mcp_writer_str(out, " session=");
    write_ulong(out, s_max_session);
    mcp_writer_str(out, s_time_known ? " time=synced" : " time=unknown");
    mcp_writer_str(out, " reason=");
    mcp_writer_str(out, s_reason);
    mcp_writer_str(out, "\"}]");
    return 0;
}


int authority_page_fits(char* buf, size_t cap, int written, const char* title) {
    if (written > 0 && (size_t)written < cap) return 1;
    snprintf(buf, cap,
        "{\"type\":\"page\",\"title\":\"%s\",\"content\":{"
          "\"type\":\"linear\",\"direction\":\"vertical\",\"padding\":20,"
          "\"spacing\":8,\"children\":["
            "{\"type\":\"text\",\"text\":\"This machine cannot show its terms\","
              "\"variant\":\"titleMedium\"},"
            "{\"type\":\"text\",\"text\":\"Its page did not fit the buffer it was "
              "built in, so it is not being served. Nothing here is safe to act "
              "on.\",\"variant\":\"bodySmall\"}"
          "]}}",
        title);
    return 0;
}

unsigned long authority_remaining_s(void) {
    /* A renewing authority has nothing left to count down — it runs until it
     * is released, which is the point. */
    return s_renewing ? 0 : remaining_s();
}

/* Units accumulated since entry, first unit included because it is the one
 * that was bought. Counted on the device's own elapsed source, which keeps
 * running whether or not anyone is present — the gap between presentations is
 * exactly what is being measured. */
unsigned long authority_units(void) {
    if (!s_renewing || !s_have_authority || s_unit_s == 0) return 0;
    const unsigned long elapsed_s = (board_uptime_ms() - s_entered_ms) / 1000UL;
    return 1UL + elapsed_s / s_unit_s;
}

int authority_state_json(const char* uri, mcp_writer_t* out) {
    (void)uri;
    mcp_writer_str(out, "{\"open\":");
    mcp_writer_str(out, s_open ? "true" : "false");
    mcp_writer_str(out, ",\"remaining_s\":");
    write_ulong(out, remaining_s());
    mcp_writer_str(out, ",\"reason\":");
    mcp_writer_json_string(out, s_reason);
    mcp_writer_str(out, "}");
    return 0;
}

/* The link dropped. Session-scoped state goes; the authority does not. */
void authority_session_reset(void) {
    s_window_live = 0;
    s_window_s = 0;
    if (s_open) {
        s_open = 0;
        if (s_cfg.actuate) s_cfg.actuate(0);
        refuse("session ended");
    }
}

/* Expiry is not an event that arrives — it is the absence of one, noticed
 * here. */
void authority_tick(void) {
    /* Nothing to expire on a renewing authority. It renews by itself and stops
     * when someone releases it — the service keeps accumulating meanwhile and
     * the device is never told. */
    if (s_renewing) return;
    if (s_have_authority && s_time_known && now_s() > s_valid_until_s) {
        s_have_authority = 0;
        s_expired = 1;
        s_open = 0;
        if (s_cfg.actuate) s_cfg.actuate(0);
        refuse("authority expired");
    }
}
