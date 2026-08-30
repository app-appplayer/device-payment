#ifndef DOMAINS_DOMAIN_H
#define DOMAINS_DOMAIN_H

/* base on the include path (-I .../c_cpp/mcp/server) */
#include "mcp_server.h"

/* STANDARD domain entry point.
 *
 * Every domain implements this ONE signature, so a target's main is
 * domain-agnostic: it calls domain_init() and the build chooses which domain's
 * .c provides the symbol (domains/led/led.c, or domains/samples/<x>/<x>.c —
 * the main never changes). A target links exactly one domain.
 *
 * `led` is the REFERENCE node — what hosts are written against. To try a
 * capability out, add a domain under domains/samples/ and select it at build
 * time; do not edit the reference. See domains/README.md.
 *
 * Device identity (id / name / version, and an optional `trust` JSON block) is
 * INJECTED by the target — the one domain serves under any node identity. Pass
 * trust=NULL for an unsigned node. */
void domain_init(mcp_server_t* s, mcp_transport_t transport,
                 const char* id, const char* name, const char* version,
                 const char* trust);

/* FR-LIVE producer hook. Called from the node's serve loop (each iteration) so
 * the domain can push live values: it reads its sensors and, on change, calls
 * mcp_server_notify_resource_updated() for any subscribed live resource. Runs on
 * the serve loop, so its emits never interleave a response. A domain with no
 * live resources leaves this a no-op. */
void domain_tick(mcp_server_t* s);

/* The peer went away. A domain drops whatever was mid-transaction and keeps
 * what was bought — a session that ends is not a session that finished. The
 * target calls this on link loss; a domain with no session state leaves it a
 * no-op. */
void domain_session_reset(void);

#endif /* DOMAINS_DOMAIN_H */
