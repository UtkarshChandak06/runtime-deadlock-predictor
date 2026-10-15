/*
 * tests/integration/tracker_probe.h - shared helper for the Phase 2
 * integration tests.
 *
 * The tracker lives in the PRELOADED library, which a test binary does
 * not link against. Like hooks.c does for the real pthread functions,
 * we locate the exported tracker symbols with dlsym(RTLD_DEFAULT, ...):
 *
 *   - preloaded run  -> symbols found, tracker assertions ACTIVE,
 *   - bare run       -> symbols absent, tests still verify normal
 *                       pthread behaviour and skip tracker assertions
 *                       (transparency: the same binary must pass both
 *                       ways).
 *
 * rdp_tracker_count/holds/depth operate on the CALLING thread, so each
 * thread queries its own thread-local collection - which is exactly how
 * the tests observe per-thread isolation.
 */

#ifndef TRACKER_PROBE_H
#define TRACKER_PROBE_H

#include <dlfcn.h>   /* dlsym, RTLD_DEFAULT */
#include <stddef.h>  /* size_t */
#include <stdio.h>   /* fprintf */
#include <stdlib.h>  /* exit */

typedef size_t (*rdp_count_fn)(void);
typedef int (*rdp_holds_fn)(void *);
typedef size_t (*rdp_depth_fn)(void *);
typedef size_t (*rdp_dropped_fn)(void);

typedef struct {
    rdp_count_fn count;
    rdp_holds_fn holds;
    rdp_depth_fn depth;
    rdp_dropped_fn dropped;
    int available; /* 1 when running under LD_PRELOAD with the tracker */
} tracker_probe;

static inline void tracker_probe_load(tracker_probe *p)
{
    p->count = (rdp_count_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_count");
    p->holds = (rdp_holds_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_holds");
    p->depth = (rdp_depth_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_depth");
    p->dropped =
        (rdp_dropped_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_dropped");
    p->available = (p->count != NULL && p->holds != NULL &&
                    p->depth != NULL && p->dropped != NULL);
    if (!p->available)
        printf("note: tracker symbols not found (bare run) - "
               "tracker assertions skipped\n");
}

/* Fail fast with file:line context; exit code != 0 fails any harness. */
#define TEST_CHECK(cond, what)                                             \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
            exit(EXIT_FAILURE);                                            \
        }                                                                  \
    } while (0)

#endif /* TRACKER_PROBE_H */
