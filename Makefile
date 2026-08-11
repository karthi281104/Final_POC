##############################################################################
# Makefile — Real-Time Stock Market Data Cache System
#
# Targets:
#   make            - build the main program (stock_cache)
#   make run        - build and run it interactively
#   make test       - build and run all 7 CUnit test binaries
#   make valgrind   - run the main program under Valgrind (memory check)
#   make helgrind   - run the main program under Valgrind's Helgrind
#                     (thread/data-race detector)
#   make cppcheck   - run cppcheck static analysis
#   make misra      - run cppcheck's MISRA-C addon
#   make coverage   - build with gcov instrumentation, run a scripted
#                     session, generate .gcov coverage reports
#   make coverage-html - same, then build an HTML report (needs lcov)
#   make check-tools - report which of the above tools are installed
#   make clean      - remove all build artifacts and generated reports
#   make help       - show this list
##############################################################################

CC          := gcc
STD         := -std=c11
WARN        := -Wall -Wextra -Wpedantic
INC         := -Iinclude
# POSIX source defines needed on Linux for pthread_rwlock, strnlen, etc.
POSIX_DEFS  := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS      := $(STD) $(WARN) $(INC) $(POSIX_DEFS)
LDFLAGS     := -pthread

# Try to detect CUnit via pkg-config. If pkg-config/CUnit not found,
# fall back to linking with -lcunit and continue (user may install
# system CUnit package). Tests will warn if the header/library are
# missing at runtime or build time.
CUNIT_CFLAGS := $(shell pkg-config --cflags CUnit 2>/dev/null)
CUNIT_LIBS   := $(shell pkg-config --libs CUnit 2>/dev/null)
ifeq ($(strip $(CUNIT_LIBS)),)
CUNIT_LIBS := -lcunit
endif
ifneq ($(strip $(CUNIT_CFLAGS)),)
CFLAGS += $(CUNIT_CFLAGS)
endif

SRC_DIR     := src
TEST_DIR    := test
COVERAGE_DIR:= coverage

TARGET      := stock_cache

SRCS        := $(wildcard $(SRC_DIR)/*.c)
OBJS        := $(SRCS:.c=.o)
LIB_SRCS    := $(filter-out $(SRC_DIR)/main.c,$(SRCS))

# --- cppcheck configuration -------------------------------------------
# Override on the command line if `find / -iname std.cfg` finds it
# somewhere other than this default, e.g.:
#   make cppcheck STD_CFG=/usr/local/share/cppcheck/cfg/std.cfg
STD_CFG     ?= /usr/share/cppcheck/cfg/std.cfg
SUPPRESSIONS:= cppcheck-suppressions.txt

# --- CUnit test binaries, each with its own exact dependency list -----
# (taken directly from the compile-command comment already documented
# at the top of each test/*.c file)
TEST_BINS   := test_hash test_maincache test_searchcache test_persistence \
               test_stats test_security test_integration

.PHONY: all run test valgrind helgrind cppcheck misra coverage \
        coverage-html check-tools clean help

##############################################################################
# Build the main program
##############################################################################
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	mkdir -p data logs
	./$(TARGET)

##############################################################################
# CUnit tests (test/ folder) — each linked against exactly the src files
# that test file's own header comment documents needing.
##############################################################################
$(TEST_DIR)/test_hash: $(TEST_DIR)/test_hash.c $(SRC_DIR)/cache.c $(SRC_DIR)/common.c $(SRC_DIR)/memory.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

$(TEST_DIR)/test_maincache: $(TEST_DIR)/test_maincache.c $(SRC_DIR)/cache.c $(SRC_DIR)/common.c $(SRC_DIR)/memory.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

$(TEST_DIR)/test_searchcache: $(TEST_DIR)/test_searchcache.c $(SRC_DIR)/searchcache.c $(SRC_DIR)/common.c $(SRC_DIR)/memory.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

$(TEST_DIR)/test_persistence: $(TEST_DIR)/test_persistence.c $(SRC_DIR)/persistence.c $(SRC_DIR)/cache.c $(SRC_DIR)/searchcache.c $(SRC_DIR)/common.c $(SRC_DIR)/memory.c $(SRC_DIR)/logger.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

$(TEST_DIR)/test_stats: $(TEST_DIR)/test_stats.c $(SRC_DIR)/stats.c $(SRC_DIR)/common.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

$(TEST_DIR)/test_security: $(TEST_DIR)/test_security.c $(SRC_DIR)/security.c $(SRC_DIR)/common.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

$(TEST_DIR)/test_integration: $(TEST_DIR)/test_integration.c $(SRC_DIR)/cache.c $(SRC_DIR)/searchcache.c $(SRC_DIR)/alerts.c $(SRC_DIR)/stats.c $(SRC_DIR)/query.c $(SRC_DIR)/feed.c $(SRC_DIR)/security.c $(SRC_DIR)/logger.c $(SRC_DIR)/common.c $(SRC_DIR)/memory.c
	$(CC) $(CFLAGS) $^ $(CUNIT_LIBS) $(LDFLAGS) -o $@

test: $(addprefix $(TEST_DIR)/,$(TEST_BINS))
	@echo "=================================================="
	@echo " Running all 7 CUnit test binaries"
	@echo "=================================================="
	@failures=0; \
	for t in $(TEST_BINS); do \
		echo "---- $$t ----"; \
		./$(TEST_DIR)/$$t; \
		if [ $$? -ne 0 ]; then failures=$$((failures+1)); fi; \
	done; \
	echo "=================================================="; \
	if [ $$failures -eq 0 ]; then \
		echo " ALL CUNIT SUITES PASSED"; \
	else \
		echo " $$failures SUITE(S) FAILED"; \
	fi; \
	echo "=================================================="; \
	exit $$failures

##############################################################################
# Valgrind — memory error / leak detection
##############################################################################
VALGRIND_SESSION := printf "admin\nadmin123\n1\nAAPL\n\n0\n0\n"

valgrind: $(TARGET)
	@command -v valgrind >/dev/null 2>&1 || { \
		echo "valgrind is not installed. Install it with:"; \
		echo "  sudo dnf install valgrind      # RHEL/CentOS/Fedora"; \
		echo "  sudo apt-get install valgrind  # Ubuntu/Debian"; \
		exit 1; \
	}
	mkdir -p data logs
	$(VALGRIND_SESSION) | valgrind \
		--leak-check=full \
		--show-leak-kinds=all \
		--track-origins=yes \
		--error-exitcode=1 \
		./$(TARGET)

##############################################################################
# Helgrind — Valgrind's thread/data-race detector
##############################################################################
helgrind: $(TARGET)
	@command -v valgrind >/dev/null 2>&1 || { \
		echo "valgrind (which provides Helgrind) is not installed. Install it with:"; \
		echo "  sudo dnf install valgrind      # RHEL/CentOS/Fedora"; \
		echo "  sudo apt-get install valgrind  # Ubuntu/Debian"; \
		exit 1; \
	}
	mkdir -p data logs
	$(VALGRIND_SESSION) | valgrind \
		--tool=helgrind \
		--error-exitcode=1 \
		./$(TARGET)

##############################################################################
# cppcheck — static analysis
##############################################################################
cppcheck:
	@command -v cppcheck >/dev/null 2>&1 || { \
		echo "cppcheck is not installed. Install it with:"; \
		echo "  sudo dnf install cppcheck      # RHEL/CentOS/Fedora"; \
		echo "  sudo apt-get install cppcheck  # Ubuntu/Debian"; \
		exit 1; \
	}
	@if [ ! -f "$(STD_CFG)" ]; then \
		echo "Warning: $(STD_CFG) not found."; \
		echo "Run: find / -iname std.cfg 2>/dev/null"; \
		echo "Then: make cppcheck STD_CFG=<path found above>"; \
	fi
	cppcheck --enable=performance,portability \
		--std=c11 --inconclusive --force \
		--library=$(STD_CFG) \
		--suppressions-list=$(SUPPRESSIONS) \
		$(INC) $(SRC_DIR)

##############################################################################
# MISRA-C — cppcheck's MISRA addon
##############################################################################
misra:
	@command -v cppcheck >/dev/null 2>&1 || { \
		echo "cppcheck is not installed. Install it with:"; \
		echo "  sudo dnf install cppcheck      # RHEL/CentOS/Fedora"; \
		echo "  sudo apt-get install cppcheck  # Ubuntu/Debian"; \
		exit 1; \
	}
	cppcheck --addon=misra \
		--suppress=misra-config \
		--std=c11 --platform=unix640 --force \
		--library=$(STD_CFG) \
		$(INC) $(SRC_DIR)
	@echo ""
	@echo "Note: cppcheck reports MISRA rule numbers (e.g. 'misra-c2012-8.4')"
	@echo "without the proprietary rule text (MISRA rule descriptions are"
	@echo "licensed content, not distributable). Look up rule numbers at"
	@echo "https://www.misra.org.uk/ if you need the full wording."

##############################################################################
# Code coverage — gcov (and optionally lcov/genhtml for an HTML report)
##############################################################################
COV_BUILD_DIR := cov_build
COV_CFLAGS  := $(CFLAGS) -O0 -fprofile-arcs -ftest-coverage
COV_LDFLAGS := $(LDFLAGS) --coverage
COV_TARGET  := stock_cache_coverage
COV_OBJS    := $(patsubst $(SRC_DIR)/%.c,$(COV_BUILD_DIR)/%.o,$(SRCS))

# Coverage objects live in their own directory (not alongside the normal
# .o files) so gcov's .gcno/.gcda files keep plain per-source names
# (cache.gcno) instead of being prefixed with the final executable name
# (which is what happens if all sources are compiled+linked in a single
# command instead of compiled to individual objects first).
$(COV_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(COV_BUILD_DIR)
	$(CC) $(COV_CFLAGS) -c $< -o $@

# A scripted session exercising all three roles and every Tester Module
# option, so coverage reflects real, broad execution rather than just
# the login screen.
COVERAGE_SESSION := printf "admin\nadmin123\n1\nAAPL\n\n4\nCOVX\nCoverage Test\n10.0\n\n5\nCOVX\n15.0\n\n6\nCOVX\n20\n1\n\n0\n1\nuser\nuser123\n1\nMSFT\n\n0\n1\ntester\ntester123\n1\n5\n\n6\n\n7\n\n8\n\n0\n0\n0\n"

coverage: clean $(COV_OBJS)
	$(CC) $(COV_OBJS) -o $(COV_TARGET) $(COV_LDFLAGS)
	mkdir -p data logs
	$(COVERAGE_SESSION) | ./$(COV_TARGET) > /dev/null 2>&1 || true
	mkdir -p $(COVERAGE_DIR)
	gcov -b -c --object-directory=$(COV_BUILD_DIR) $(SRC_DIR)/*.c
	@mv -f *.gcov $(COVERAGE_DIR)/ 2>/dev/null || true
	@echo ""
	@echo "=================================================="
	@echo " Coverage .gcov files written to $(COVERAGE_DIR)/"
	@echo " For a browsable HTML report, run: make coverage-html"
	@echo "=================================================="

coverage-html: coverage
	@command -v lcov >/dev/null 2>&1 || { \
		echo "lcov is not installed. Install it with:"; \
		echo "  sudo dnf install lcov      # RHEL/CentOS/Fedora"; \
		echo "  sudo apt-get install lcov  # Ubuntu/Debian"; \
		exit 1; \
	}
	lcov --capture --directory $(COV_BUILD_DIR) --output-file $(COVERAGE_DIR)/coverage.info
	genhtml $(COVERAGE_DIR)/coverage.info --output-directory $(COVERAGE_DIR)/html
	@echo "Open $(COVERAGE_DIR)/html/index.html in a browser to view it."

##############################################################################
# Utility targets
##############################################################################
check-tools:
	@echo "Checking which verification tools are available on this machine:"
	@for tool in gcc gcov valgrind cppcheck lcov genhtml; do \
		if command -v $$tool >/dev/null 2>&1; then \
			printf "  [FOUND]     %s\n" "$$tool"; \
		else \
			printf "  [MISSING]   %s\n" "$$tool"; \
		fi; \
	done

clean:
	rm -f $(SRC_DIR)/*.o
	rm -f $(TARGET) $(COV_TARGET)
	rm -f $(addprefix $(TEST_DIR)/,$(TEST_BINS))
	rm -rf $(COVERAGE_DIR) $(COV_BUILD_DIR)
	rm -f *.gcov

help:
	@echo "Available targets:"
	@echo "  make            - build the main program ($(TARGET))"
	@echo "  make run        - build and run it interactively"
	@echo "  make test       - build and run all 7 CUnit test binaries"
	@echo "  make valgrind   - run under Valgrind (memory leak/error check)"
	@echo "  make helgrind   - run under Valgrind's Helgrind (thread/race check)"
	@echo "  make cppcheck   - run cppcheck static analysis"
	@echo "  make misra      - run cppcheck's MISRA-C addon"
	@echo "  make coverage   - build with gcov instrumentation and generate reports"
	@echo "  make coverage-html - same, plus an HTML report (needs lcov)"
	@echo "  make check-tools - show which of the above tools are installed"
	@echo "  make clean      - remove all build artifacts and generated reports"
