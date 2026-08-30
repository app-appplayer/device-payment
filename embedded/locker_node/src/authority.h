/* Authority — the platform half of a device-payment node.
 *
 * A locker, a car wash, a vending machine and a charger differ in what they
 * sell and what they actuate. They do not differ in how an authority is
 * established: a signed voucher arrives, the device verifies it against a key
 * it was given, refuses replays from state it already holds, and judges an
 * interval with no network. Writing that once per machine is how four
 * implementations end up with four different sets of holes.
 *
 * So it lives here, and a domain supplies only what is actually its own:
 *
 *   - who it is
 *   - what it offers, and under what conditions
 *   - whether the action occupies the machine
 *   - what to actuate when the authority stands
 *
 * The domain never sees a signature, a session value or a clock.
 */
#ifndef AUTHORITY_H
#define AUTHORITY_H

#include <stddef.h>
#include "mcp_writer.h"

/* Which of the two shapes an offer takes.
 *
 * They are not variations of one thing. A fixed authority is compared against
 * an expiry and the answer is true or false; a renewing one has no expiry to
 * compare — it accumulates until someone releases it, and what the device
 * determines is HOW MUCH was used. The patent separates them because the
 * demands differ: the second needs the estimate bounded in both directions,
 * time counted while nobody is present, and therefore continuous power. */
typedef enum {
    /* Buy four hours, get four hours. Expires on its own. */
    AUTHORITY_FIXED,
    /* Enter a state at a unit rate. Only the first unit is bought; the
     * authority renews each period by itself until a release event, and the
     * release is what fixes the total. */
    AUTHORITY_RENEWING,
} authority_shape;

/* One thing the machine offers. `id` is what a voucher must name; a voucher
 * for anything else is refused however well it is signed. */
typedef struct {
    const char* id;
    /* Fixed: how long the authority lasts. Renewing: the unit period. */
    unsigned long seconds;
    /* How long after acceptance the action may still be taken WITHIN THAT
     * SESSION. Zero means no session window — the interval alone governs.
     *
     * Measured on the device's own tick: the span is seconds inside one
     * connection, so nothing outside the device gets a say in it. This is a
     * different layer from the interval above, which spans sessions and does
     * need a presented time. */
    unsigned long within_s;
    authority_shape shape;
} authority_offer_t;

/* Whether the action occupies the machine.
 *
 * Not a transport question and not derivable from the offer: a wash is a
 * single run and holds the drum for 42 minutes; a vend is a single run and
 * holds nothing. Only the domain knows, so only the domain says. Declared in
 * the manifest so a host can show it; a byte stream cannot police it, and a
 * transport with real peers (TCP, GATT) is where it can be. */
typedef enum {
    AUTHORITY_EXCLUSIVE, /* one at a time — a door, a bay, a drum */
    AUTHORITY_SHARED,    /* order does not matter — a chute */
} authority_occupancy;

/* Where the authority is kept across a power cut.
 *
 * A rental that vanishes when the mains blink would free every door in the
 * building, so the authority has to outlive the board. The estimate of the
 * time does NOT — uptime restarts at zero and the presented time is gone —
 * and that asymmetry is the whole reason the floor at the accepted voucher's
 * window exists: on a machine that wakes up holding an authority but no clock,
 * it is the only thing between a first correction and anywhere the presenter
 * likes.
 *
 * Supplied by the target rather than the platform: a board has emulated
 * EEPROM, a virtual node has a file, and neither belongs in here. `read`
 * returns non-zero when it filled the buffer. Absent hooks mean a machine that
 * forgets, which is a choice a target is allowed to make explicitly. */
typedef void (*authority_store_write_fn)(const void* data, unsigned long len);
typedef int (*authority_store_read_fn)(void* data, unsigned long len);

/* What the machine does when an authority stands. Called only after every
 * check has passed. `on` is 1 to act, 0 to stand down. */
typedef void (*authority_actuate_fn)(int on);

typedef struct {
    const char* device_id;
    /* The reference this machine answers for — a locker bay, a wash bay, a
     * slot. A voucher naming another one is refused. */
    const char* target;
    const authority_offer_t* offers;
    int offer_count;
    /* The service public key this device verifies against. 32 bytes. */
    const unsigned char* service_pubkey;
    /* This device's own private key, 32 bytes, used ONLY to say which machine
     * is speaking. It cannot mint an authority — that takes the service key,
     * which is not here and never will be. Provisioned into flash on this
     * board; a secure element is where it belongs and this one has none. */
    const unsigned char* device_privkey;
    /* The declaration this device serves, hashed into every assertion so a
     * relayer cannot present a specification the device did not make. */
    const char* spec;
    authority_occupancy occupancy;
    authority_actuate_fn actuate;
    authority_store_write_fn store_write;
    authority_store_read_fn store_read;
} authority_config_t;

void authority_init(const authority_config_t* config);

/* `"exclusive"` / `"shared"`, for the manifest. */
const char* authority_occupancy_name(void);

/* The four tools every device-payment node has. A domain registers them under
 * whatever names it likes and adds its own beside them. */
int authority_present(const char* args, size_t args_len, mcp_writer_t* out);
int authority_act(const char* args, size_t args_len, mcp_writer_t* out);
int authority_status(const char* args, size_t args_len, mcp_writer_t* out);
int authority_time_sync(const char* args, size_t args_len, mcp_writer_t* out);

/* Live state for a `state://` resource: open, remaining seconds, last reason. */
int authority_state_json(const char* uri, mcp_writer_t* out);

/* Seconds left on the held authority, 0 if none or if it renews. */
unsigned long authority_remaining_s(void);

/* Units accumulated on a renewing authority, 0 otherwise. What the release
 * event reports and what the service settles against. */
unsigned long authority_units(void);

/* The link dropped. Session-scoped state goes; the authority does not — what
 * was bought is not undone by a phone walking away. */
void authority_session_reset(void);

/* Guard a rendered document against silent truncation.
 *
 * `snprintf` cuts and says nothing, and a page cut mid-string is still served.
 * The host then reports only that the uri "did not resolve to a UI
 * definition", which points at the host — the one place the fault is not. It
 * cost a real debugging round the first time it happened.
 *
 * Pass what snprintf returned. If it did not fit, the buffer is replaced with
 * a small VALID page that says so on screen, and 0 is returned. A machine that
 * cannot show its terms must say that, not show nothing.
 */
int authority_page_fits(char* buf, size_t cap, int written, const char* title);

/* The device's identity assertion (patent §3.2).
 *
 * Not a name being announced. The device signs
 *
 *     deviceId || counter || nonce || H(declaration)
 *
 * and the phone relays it to the service UNCHANGED. Two things follow, and
 * both are the reason this exists rather than a bare identifier:
 *
 *   - the counter never repeats, so an assertion captured from this device
 *     cannot be replayed to the service on its behalf;
 *   - the declaration is signed over, so the phone cannot tell the service a
 *     different offer than the machine actually made. That is what makes the
 *     comparison at the far end mean something in both directions.
 *
 * The counter outlives a power cut for the same reason the replay counter
 * does: a machine that restarts its count issues an assertion it has issued
 * before.
 */
int authority_assert(const char* args, size_t args_len, mcp_writer_t* out);

/* Hand the platform the declaration it will hash. Called after the page is
 * rendered, because the hash has to cover what this node actually serves — a
 * hash taken over an empty buffer proves the node made no offer at all. */
void authority_set_spec(const char* spec);

/* Whether the authority currently held was restored from storage rather than
 * granted in this run. A machine that came back up holding a rental it cannot
 * yet judge has to say so — to a person waiting at the door, "wait for a
 * phone" and "you have nothing" are not the same sentence. */
int authority_resumed(void);

/* Called from the serve loop. Drops the authority the moment its interval
 * ends, which is what makes expiry happen with nothing arriving to cause it. */
void authority_tick(void);

#endif /* AUTHORITY_H */
