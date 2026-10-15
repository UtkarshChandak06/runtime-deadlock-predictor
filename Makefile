# Makefile - Runtime Deadlock Prediction Using Lock-Order Graph Analysis
# Phase 1: LD_PRELOAD shared library + smoke test.
#
# Targets:
#   make / make all   build the library and the test programs
#   make lib          build build/libdeadlock_monitor.so only
#   make tests        build the test programs + unit tests only
#   make run          run unit tests, then every test UNDER LD_PRELOAD
#   make run-bare     run everything WITHOUT the monitor (comparison)
#   make grep         grep POTENTIAL DEADLOCK in a captured run (helper)
#   make clean        remove build/
#
# Linux only - on Windows use WSL2/Ubuntu, not Git Bash.

CC       := gcc
CFLAGS   := -Wall -Wextra -O2 -g
CPPFLAGS := -D_GNU_SOURCE -Iinclude
LDFLAGS  :=
LDLIBS   :=

BUILD     := build
LIB       := $(BUILD)/libdeadlock_monitor.so
LIB_SRCS  := src/hooks.c src/tracker.c src/log.c src/graph.c src/report.c
LIB_HDRS  := include/rdp/tracker.h include/rdp/internal.h include/rdp/graph.h include/rdp/report.h
TEST_SRCS := $(wildcard tests/integration/*.c)
TEST_BINS := $(patsubst tests/integration/%.c,$(BUILD)/%,$(TEST_SRCS))
UNIT_SRCS := $(wildcard tests/unit/*.c)
UNIT_BINS := $(patsubst tests/unit/%.c,$(BUILD)/unit_%,$(UNIT_SRCS))
LIB_ABS   := $(CURDIR)/$(LIB)

# Refuse to build on non-Linux hosts: LD_PRELOAD/dlsym(RTLD_NEXT) do not
# exist elsewhere, so a clear error beats a cryptic link failure.
ifeq ($(filter Linux,$(shell uname -s)),)
$(error This project builds only on Linux (WSL2/Ubuntu on Windows); detected: $(shell uname -s))
endif

.PHONY: all lib tests run run-bare clean

all: lib tests

lib: $(LIB)

tests: $(TEST_BINS) $(UNIT_BINS)

$(BUILD):
	mkdir -p $(BUILD)

# Shared library: -fPIC (required for .so code) + -shared (produce a .so).
# -ldl supplies dlsym on glibc < 2.34 (a harmless stub on newer glibc).
$(LIB): $(LIB_SRCS) $(LIB_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRDP_INTERPOSE -fPIC -shared $(LDFLAGS) -o $@ $(LIB_SRCS) -ldl -pthread

# Integration test programs: executables linked against pthreads; they
# locate the tracker via dlsym() at runtime, so nothing is linked in.
$(BUILD)/%: tests/integration/%.c tests/integration/tracker_probe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LDFLAGS) -o $@ $< $(LDLIBS)

# Unit tests: link the module under test directly (NO LD_PRELOAD).
$(BUILD)/unit_%: tests/unit/%.c src/tracker.c src/graph.c src/log.c src/report.c $(LIB_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LDFLAGS) -o $@ tests/unit/$*.c src/tracker.c src/graph.c src/log.c src/report.c $(LDLIBS)

# Run unit tests first (they gate everything), then the integration
# tests WITH the monitor preloaded; stop at the first failure.
run: all
	@set -e; for t in $(UNIT_BINS); do \
		echo "== $$t (unit, no preload) =="; \
		timeout 15s $$t; \
	done
	@set -e; for t in $(TEST_BINS); do \
		echo "== $$t (LD_PRELOAD=$(LIB_ABS)) =="; \
		timeout 15s env LD_PRELOAD=$(LIB_ABS) $$t; \
	done

# Same tests WITHOUT the monitor: behaviour must be identical (this is
# the transparency check; tracker assertions self-skip via dlsym probe).
run-bare: tests
	@set -e; for t in $(UNIT_BINS) $(TEST_BINS); do \
		echo "== $$t (bare, no monitor) =="; \
		timeout 15s $$t; \
	done

clean:
	rm -rf $(BUILD)



$(BUILD)/unit_test_log: LDFLAGS += -Wl,--wrap=write

.PHONY: check-diagnostics
check-diagnostics: all $(BUILD)/real_deadlock
	python3 tests/check_diagnostics.py


$(BUILD)/real_deadlock: tests/deadlock/real_deadlock.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -o $@ $<


.PHONY: check-sanitizers
check-sanitizers: all
	python3 tests/check_sanitizers.py


$(BUILD)/unit_test_log_cancel: LDFLAGS += -Wl,--wrap=write

$(BUILD)/robustness_resolver: tests/robustness/test_resolver.c $(LIB_SRCS) $(LIB_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DRDP_INTERPOSE -pthread -Wl,--wrap=dlsym -o $@ $< src/hooks.c src/tracker.c src/log.c src/graph.c src/report.c -ldl

.PHONY: check-robustness
check-robustness: $(BUILD)/robustness_resolver
	python3 tests/check_robustness.py


.PHONY: bench-build benchmark
bench-build: $(BUILD)/bench_mutex $(LIB)

$(BUILD)/bench_mutex: benchmarks/mutex_workload.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LDFLAGS) -o $@ $< -ldl $(LDLIBS)

benchmark: bench-build
	python3 benchmarks/run.py $(BENCH_ARGS)
