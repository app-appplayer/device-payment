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
    authority_occupancy occupancy;
    authority_actuate_fn actuate;
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

/* Called from the serve loop. Drops the authority the moment its interval
 * ends, which is what makes expiry happen with nothing arriving to cause it. */
void authority_tick(void);

#endif /* AUTHORITY_H */
