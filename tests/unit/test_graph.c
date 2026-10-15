/*
 * tests/unit/test_graph.c - unit tests for the lock-order graph and
 * cycle detection (Phase 3, Tests A-F from the verification plan).
 *
 * Links src/graph.c DIRECTLY (no LD_PRELOAD, no wrappers): cycles are
 * exercised as plain data-structure behaviour, with arbitrary address
 * tokens standing in for mutexes - exactly the course goal of testing
 * detection without deadlocking real threads.
 *
 * Cycle rule being tested (documents the semantics): for a proposed
 * dependency from -> to, a cycle exists IFF a path to ~> from already
 * exists in the graph BEFORE the proposal. The helper pair reflects
 * that: expect_cycle(from, to) == "proposing from->to closes a cycle".
 *
 * Covers:
 *   A - single dependency: observe A->B, no cycle
 *   B - two-lock cycle: A->B then B->A => cycle A->B->A
 *       + repeated detection is a benign no-op (warn once)
 *   C - three-lock cycle: A->B, B->C, then C->A => cycle A->B->C->A
 *   D - duplicate edge: repeated A->B stored exactly once
 *   E - independent dependencies: A->B and C->D => no cycle
 *   F - concurrent updates from many threads: no corruption, exact
 *       store-and-persist counts
 */

#include "rdp/graph.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, what)                                                \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    (what));                                             \
            exit(EXIT_FAILURE);                                          \
        }                                                                \
    } while (0)

static void ok(const char *name)
{
    printf("ok - %s\n", name);
}

static void print_cycle(const char *tag, void **path, size_t len)
{
    printf("    %s cycle:", tag);
    for (size_t i = 0; i < len; i++) {
        printf(" %p", path[i]);
        if (i + 1 < len)
            printf(" ->");
    }
    printf("\n");
}

/* Propose the dependency from -> to via the analysis API (which also
 * records it on first observation) and require NO cycle. Edge-count / dedup
 * expectations are asserted at the call sites, not here. */
static void expect_no_cycle(void *from, void *to)
{
    void *path[RDP_GRAPH_MAX_CYC];
    size_t plen = 0;

    CHECK(rdp_graph_cycle_for_edge(from, to, path, &plen, NULL, NULL) ==
              RDP_GRAPH_NO_CYCLE,
          "expected no cycle for this dependency");
    CHECK(plen == 0, "no path reported when there is no cycle");
}

/* Propose (and record) the closing dependency from -> to; require a
 * cycle with a correct reconstructed path to ~> from. */
static void expect_cycle(void *from, void *to)
{
    void *path[RDP_GRAPH_MAX_CYC];
    size_t plen = 0;
    void *ef = NULL, *et = NULL;

    CHECK(rdp_graph_cycle_for_edge(from, to, path, &plen, &ef, &et) ==
              RDP_GRAPH_CYCLE_FOUND,
          "expected cycle to be detected");
    CHECK(plen >= 2, "cycle path has at least two nodes");
    CHECK(ef == from && et == to, "closing edge reported correctly");
    CHECK(path[0] == to, "path starts at 'to'");
    CHECK(path[plen - 1] == from, "path ends at 'from'");
    print_cycle("detected", path, plen);
}

/* ------------------------------------------------------------------ */
/* Test A - single dependency: observe A->B, no cycle                   */
/* ------------------------------------------------------------------ */

static char tok_a, tok_b, tok_c, tok_d;
#define A ((void *)&tok_a)
#define B ((void *)&tok_b)
#define C ((void *)&tok_c)
#define D ((void *)&tok_d)

static void test_a_single_dependency(void)
{
    rdp_graph_cleanup();

    /* Once: observe A->B. The observation itself must not warn. */
    CHECK(rdp_graph_add_edge(A, B) == RDP_GRAPH_ADDED, "edge A->B added");
    CHECK(rdp_graph_edge_count() == 1, "exactly one edge stored");
    CHECK(rdp_graph_node_count() == 2, "two nodes registered");

    /* Analysis of the SAME dependency again: still no cycle. */
    expect_no_cycle(A, B);
    CHECK(rdp_graph_edge_count() == 1, "duplicate, nothing new stored");

    /* An unrelated, non-inverting dependency: first analysis RECORDS it
     * (this is exactly what the wrapper does on every attempt); a
     * second analysis of the same dependency records nothing new. */
    expect_no_cycle(B, C);
    CHECK(rdp_graph_edge_count() == 2, "B->C recorded by analysis");
    expect_no_cycle(B, C);
    CHECK(rdp_graph_edge_count() == 2, "repeated analysis deduplicates");
    CHECK(rdp_graph_node_count() == 3, "three nodes now");
    ok("Test A: single dependency stored, no cycle");
}

/* ------------------------------------------------------------------ */
/* Test B - two-lock cycle                                              */
/* ------------------------------------------------------------------ */

static void test_b_two_lock_cycle(void)
{
    void *path[RDP_GRAPH_MAX_CYC];
    size_t plen = 0;
    void *ef = NULL, *et = NULL;

    rdp_graph_cleanup();
    CHECK(rdp_graph_add_edge(A, B) == RDP_GRAPH_ADDED, "edge A->B added");

    /* Inverted order observed: adding B->A closes A->B->A. */
    expect_cycle(B, A);
    CHECK(rdp_graph_edge_count() == 2, "both cycle edges stored");

    /* The SAME inverted dependency seen again must be a benign no-op
     * (warn once per closing edge), not a fresh warning. */
    CHECK(rdp_graph_cycle_for_edge(B, A, path, &plen, &ef, &et) ==
              RDP_GRAPH_ALREADY_REPORTED,
          "duplicate closing edge => ALREADY_REPORTED");
    CHECK(rdp_graph_edge_count() == 2, "no new edge stored");
    ok("Test B: two-lock cycle A->B->A detected (warn-once verified)");
}

/* ------------------------------------------------------------------ */
/* Test C - three-lock cycle                                            */
/* ------------------------------------------------------------------ */

static void test_c_three_lock_cycle(void)
{
    rdp_graph_cleanup();
    CHECK(rdp_graph_add_edge(A, B) == RDP_GRAPH_ADDED, "edge A->B added");
    CHECK(rdp_graph_add_edge(B, C) == RDP_GRAPH_ADDED, "edge B->C added");
    CHECK(rdp_graph_edge_count() == 2, "chain A->B->C stored");

    /* Closing edge C->A: joining it forms A->B->C->A. */
    expect_cycle(C, A);
    CHECK(rdp_graph_edge_count() == 3, "closing edge stored");

    /* Duplicate closing edge is deduplicated (requirement 5). */
    CHECK(rdp_graph_add_edge(C, A) == RDP_GRAPH_DUPLICATE,
          "duplicate closing edge not stored twice");
    CHECK(rdp_graph_edge_count() == 3, "still exactly three edges");
    ok("Test C: three-lock cycle A->B->C->A detected");
}

/* ------------------------------------------------------------------ */
/* Test D - duplicate edge                                              */
/* ------------------------------------------------------------------ */

static void test_d_duplicate_edge(void)
{
    rdp_graph_cleanup();
    for (int i = 0; i < 10; i++)
        CHECK(rdp_graph_add_edge(A, B) ==
                  (i == 0 ? RDP_GRAPH_ADDED : RDP_GRAPH_DUPLICATE),
              "first add => ADDED, repeats => DUPLICATE");
    CHECK(rdp_graph_edge_count() == 1, "exactly one A->B stored");
    CHECK(rdp_graph_node_count() == 2, "exactly two nodes");
    /* And analysis of the duplicate is cycle-free here (no inversion). */
    expect_no_cycle(A, B);
    ok("Test D: duplicate edges stored once");
}

/* ------------------------------------------------------------------ */
/* Test E - independent dependencies                                    */
/* ------------------------------------------------------------------ */

static void test_e_independent(void)
{
    rdp_graph_cleanup();
    expect_no_cycle(A, B); /* observe A->B */
    expect_no_cycle(C, D); /* observe C->D: disjoint segment */
    CHECK(rdp_graph_edge_count() == 2, "two independent edges stored");
    CHECK(rdp_graph_node_count() == 4, "four distinct nodes");

    /* No cross-segment cycle: propose a fresh inter-segment edge. */
    expect_no_cycle(D, A);
    CHECK(rdp_graph_edge_count() == 3, "new inter-segment edge recorded");

    rdp_graph_cleanup();
    CHECK(rdp_graph_edge_count() == 0, "cleanup resets the graph");
    CHECK(rdp_graph_node_count() == 0, "cleanup removes nodes");
    ok("Test E: independent dependencies, no cycle");
}

/* ------------------------------------------------------------------ */
/* Test F - concurrent graph updates                                    */
/* ------------------------------------------------------------------ */

#define NWORKERS 4
static char conc_tok[NWORKERS * 3];
#define NCHECKS 200

/* Every worker builds a chain me -> me2 -> me3 over ITS OWN three
 * tokens. All chains are acyclic, so every round must return a
 * cycle-free outcome; corrupted synchronization would surface as a
 * wrong result, a crash, or drifted counts afterward. */
static void *concurrent_worker(void *arg)
{
    long k = (long)arg;
    void *me = &conc_tok[3 * k];
    void *me2 = &conc_tok[3 * k + 1];
    void *me3 = &conc_tok[3 * k + 2];

    for (int i = 0; i < NCHECKS; i++) {
        int res = rdp_graph_cycle_for_edge(me, me2, NULL, NULL, NULL, NULL);
        CHECK(res == RDP_GRAPH_NO_CYCLE, "own chain edge is cycle-free");
        res = rdp_graph_cycle_for_edge(me2, me3, NULL, NULL, NULL, NULL);
        CHECK(res == RDP_GRAPH_NO_CYCLE, "own chain edge is cycle-free");
    }
    return NULL;
}

static void test_f_concurrent_updates(void)
{
    pthread_t th[NWORKERS];

    rdp_graph_cleanup();
    CHECK(rdp_graph_node_count() == 0, "graph empty at test start");
    CHECK(rdp_graph_dropped() == 0, "nothing dropped at test start");

    for (long k = 0; k < NWORKERS; k++) {
        CHECK(pthread_create(&th[k], NULL, concurrent_worker,
                             (void *)k) == 0,
              "pthread_create");
    }
    for (int k = 0; k < NWORKERS; k++) {
        CHECK(pthread_join(th[k], NULL) == 0, "pthread_join");
    }

    /* Every chain edge must have persisted exactly once. */
    for (long k = 0; k < NWORKERS; k++) {
        CHECK(rdp_graph_add_edge(&conc_tok[3 * k],
                                 &conc_tok[3 * k + 1]) ==
                  RDP_GRAPH_DUPLICATE,
              "worker edge survived the storm");
        CHECK(rdp_graph_add_edge(&conc_tok[3 * k + 1],
                                 &conc_tok[3 * k + 2]) ==
                  RDP_GRAPH_DUPLICATE,
              "worker edge survived the storm");
    }
    CHECK(rdp_graph_edge_count() == 2 * NWORKERS,
          "exactly 2 edges per worker stored");
    CHECK(rdp_graph_node_count() == 3 * NWORKERS,
          "exactly 3 nodes per worker stored");
    CHECK(rdp_graph_dropped() == 0, "nothing dropped (fit in capacity)");

    ok("Test F: concurrent updates keep the graph consistent");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    rdp_graph_init();

    test_a_single_dependency();
    test_b_two_lock_cycle();
    test_c_three_lock_cycle();
    test_d_duplicate_edge();
    test_e_independent();
    test_f_concurrent_updates();

    rdp_graph_cleanup();
    printf("TEST test_graph PASS\n");
    return EXIT_SUCCESS;
}
