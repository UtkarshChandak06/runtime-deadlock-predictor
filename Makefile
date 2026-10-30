# Run with GNU Make inside Linux/WSL, using GCC's POSIX thread support.
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
TARGET = build/hello_threads

.PHONY: all run clean help banking-safe banking-unsafe banking-deadlock-build banking-build check-phase2
all: $(TARGET)

# The order-only prerequisite creates the output directory when needed.
$(TARGET): examples/basic/hello_threads.c | build
	$(CC) $(CFLAGS) -pthread $< -o $@

build:
	mkdir -p build

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build

help:
	@printf '%s\n' 'make check-final            Run all phase gates, detection matrix and real browser tests' 'make check-detection        Save real expected-versus-observed detection matrix' 'make benchmark              Run bounded paired C benchmarks; preserve raw CSV'
	@printf '%s\n' 'make frontend-install       Install locked frontend dependencies' 'make frontend-run           Run local dashboard on 127.0.0.1:5173' 'make frontend-build         Type-check and build dashboard' 'make check-phase6           Run frontend build and unit/component checks' 'make check-phase6-e2e       Own local servers and test real browser/C integration'
	@printf '%s\n' 'make backend-install        Install local backend virtual environment' 'make backend-run            Run local API on 127.0.0.1:8000' 'make check-phase5           Verify real API, WebSocket and process lifecycle'
	@printf '%s\n' 'make       Build the two-thread example' 'make run   Build and run the example' 'make clean Remove generated build files' 'make help  Show this help'
	@printf '%s\n' 'make banking-safe           Build and run safe banking' 'make banking-unsafe         Build and run sequential unsafe banking' 'make banking-deadlock-build Build ONLY the deadlock fixture' 'make check-phase2           Run bounded banking and Phase 1 checks'
	@printf '%s\n' 'make analysis-demo          Run explicit-event graph demo' 'make check-phase3           Run graph/tracker/analyzer tests' 'make check-phase3-sanitize  Run optional Address/Undefined sanitizers' 'make check-phase3-tsan      Run optional ThreadSanitizer'
	@printf '%s\n' 'make monitor-build          Build real pthread preload monitor' 'make monitor-safe           Monitor safe banking (events on stderr)' 'make monitor-unsafe         Monitor unsafe banking (events on stderr)' 'make monitor-deadlock       Run bounded monitored deadlock harness' 'make check-phase4           Run runtime integration tests' 'make check-phase4-ubsan     Run runtime tests with UndefinedBehaviorSanitizer'

build/banking_%: examples/banking/%.c examples/banking/banking.c examples/banking/banking.h | build
	$(CC) $(CFLAGS) -pthread examples/banking/$*.c examples/banking/banking.c -o $@

build/test_banking_validation: tests/test_banking_validation.c examples/banking/banking.c examples/banking/banking.h | build
	$(CC) $(CFLAGS) -pthread -Iexamples/banking tests/test_banking_validation.c examples/banking/banking.c -o $@

banking-build: build/banking_safe build/banking_unsafe build/banking_deadlock build/test_banking_validation network-build

.PHONY: network-build monitor-network-safe monitor-network-cycle
network-build: build/network_safe build/network_cycle

build/network_safe: examples/banking/network.c | build
	$(CC) $(CFLAGS) -pthread -DNETWORK_ORDERED=1 $< -o $@

build/network_cycle: examples/banking/network.c | build
	$(CC) $(CFLAGS) -pthread -DNETWORK_ORDERED=0 $< -o $@

monitor-network-safe: monitor-build network-build
	LD_PRELOAD=./build/libdeadlock_monitor.so ./build/network_safe

monitor-network-cycle: monitor-build network-build
	LD_PRELOAD=./build/libdeadlock_monitor.so ./build/network_cycle

banking-safe: build/banking_safe
	./build/banking_safe

banking-unsafe: build/banking_unsafe
	./build/banking_unsafe

banking-deadlock-build: build/banking_deadlock

check-phase2:
	bash scripts/check_phase2.sh

# Phase 3 is a static analysis library, not an LD_PRELOAD shared library.
ANALYSIS_SOURCES = monitor/src/graph.c monitor/src/tracker.c monitor/src/analyzer.c
ANALYSIS_HEADERS = $(wildcard monitor/include/rdp/*.h)
ANALYSIS_OBJECTS = $(patsubst monitor/src/%.c,build/analysis/%.o,$(ANALYSIS_SOURCES))
ANALYSIS_TESTS = graph tracker analyzer analyzer_concurrent

.PHONY: analysis-demo analysis-tests check-phase3 check-phase3-sanitize check-phase3-tsan
build/analysis:
	mkdir -p $@

build/analysis/%.o: monitor/src/%.c $(ANALYSIS_HEADERS) | build/analysis
	$(CC) $(CFLAGS) -pthread -Imonitor/include -c $< -o $@

build/libdeadlock_analysis.a: $(ANALYSIS_OBJECTS)
	$(AR) rcs $@ $^

build/graph_demo: examples/analysis/graph_demo.c build/libdeadlock_analysis.a $(ANALYSIS_HEADERS)
	$(CC) $(CFLAGS) -pthread -Imonitor/include $< build/libdeadlock_analysis.a -o $@

build/test_%: tests/test_%.c tests/analysis_test.h build/libdeadlock_analysis.a $(ANALYSIS_HEADERS)
	$(CC) $(CFLAGS) -pthread -Imonitor/include $< build/libdeadlock_analysis.a -o $@

analysis-demo: build/graph_demo
	./build/graph_demo

analysis-tests: $(addprefix build/test_,$(ANALYSIS_TESTS))

check-phase3:
	bash scripts/check_phase3.sh

# Separate binaries ensure sanitizer flags never reuse unsanitized objects.
check-phase3-sanitize: | build
	@set -e; for name in $(ANALYSIS_TESTS); do \
		$(CC) $(CFLAGS) -pthread -Imonitor/include -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_$$name.c $(ANALYSIS_SOURCES) -o build/sanitize_$$name; \
		ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s ./build/sanitize_$$name; \
	done

check-phase3-tsan: | build
	$(CC) $(CFLAGS) -pthread -Imonitor/include -fsanitize=thread -fno-omit-frame-pointer tests/test_analyzer_concurrent.c $(ANALYSIS_SOURCES) -o build/tsan_analyzer_concurrent
	TSAN_OPTIONS=halt_on_error=1 timeout 30s ./build/tsan_analyzer_concurrent

RUNTIME_SOURCES = monitor/src/hooks.c monitor/src/runtime.c monitor/src/events.c
MONITOR_SOURCES = $(ANALYSIS_SOURCES) $(RUNTIME_SOURCES)
MONITOR_OBJECTS = $(patsubst monitor/src/%.c,build/monitor/%.o,$(MONITOR_SOURCES))
.PHONY: monitor-build monitor-safe monitor-unsafe monitor-deadlock runtime-tests-build check-phase4 check-phase4-ubsan
build/monitor:
	mkdir -p $@

build/monitor/%.o: monitor/src/%.c $(ANALYSIS_HEADERS) | build/monitor
	$(CC) $(CFLAGS) -fPIC -pthread -Imonitor/include -c $< -o $@

build/libdeadlock_monitor.so: $(MONITOR_OBJECTS)
	$(CC) -shared -pthread $^ -ldl -o $@

monitor-build: build/libdeadlock_monitor.so

monitor-safe: monitor-build build/banking_safe
	LD_PRELOAD=./build/libdeadlock_monitor.so ./build/banking_safe

monitor-unsafe: monitor-build build/banking_unsafe
	LD_PRELOAD=./build/libdeadlock_monitor.so ./build/banking_unsafe

build/test_runtime_mutex: tests/test_runtime_mutex.c | build
	$(CC) $(CFLAGS) -pthread $< -o $@

runtime-tests-build: monitor-build banking-build build/test_runtime_mutex

monitor-deadlock: runtime-tests-build
	python3 tests/test_runtime.py --deadlock-only

check-phase4:
	bash scripts/check_phase4.sh

build/libdeadlock_monitor_ubsan.so: $(MONITOR_SOURCES) $(ANALYSIS_HEADERS) | build
	$(CC) $(CFLAGS) -fPIC -shared -pthread -Imonitor/include -fsanitize=undefined -fno-sanitize-recover=all $(MONITOR_SOURCES) -ldl -o $@

check-phase4-ubsan: runtime-tests-build build/libdeadlock_monitor_ubsan.so
	UBSAN_OPTIONS=halt_on_error=1 python3 tests/test_runtime.py --library ./build/libdeadlock_monitor_ubsan.so

.PHONY: backend-install backend-run check-phase5
backend-install:
	python3 scripts/install_backend.py

backend-run: monitor-build banking-build
	backend/.venv/bin/python -m uvicorn app.main:app --app-dir backend --host 127.0.0.1 --port 8000 --no-proxy-headers --ws websockets-sansio --timeout-graceful-shutdown 5

check-phase5:
	bash scripts/check_phase5.sh

.PHONY: frontend-install frontend-run frontend-build check-phase6 check-phase6-e2e
frontend-install:
	bash scripts/frontend.sh ci --no-audit --no-fund

frontend-run:
	bash scripts/frontend.sh run dev

frontend-build:
	bash scripts/frontend.sh run build

check-phase6:
	bash scripts/check_phase6.sh

check-phase6-e2e: monitor-build banking-build
	python3 scripts/check_phase6_e2e.py

.PHONY: benchmark-build benchmark
build/benchmark: benchmarks/benchmark.c | build
	$(CC) -O2 -std=c11 -Wall -Wextra -Wpedantic -pthread $< -o $@

benchmark-build: build/benchmark monitor-build

benchmark: benchmark-build
	python3 benchmarks/run_benchmarks.py

.PHONY: check-detection check-final
check-detection: runtime-tests-build
	python3 scripts/evaluate_detection.py

check-final:
	bash scripts/check_final.sh
