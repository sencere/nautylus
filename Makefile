CC?=cc
FUZZ_CC?=clang
AR?=ar
INSTALL?=install
CFLAGS?=-std=c99 -Wall -Wextra -Wpedantic -O2
LDLIBS?=-lm
CPPFLAGS?=-Isrc
VERSION?=0.1.0-alpha
PREFIX?=/usr/local
BINDIR?=$(PREFIX)/bin
LIBDIR?=$(PREFIX)/lib
INCLUDEDIR?=$(PREFIX)/include
PKGCONFIGDIR?=$(LIBDIR)/pkgconfig

BUILD_DIR=build
SRC_DIR=src
TEST_DIR=tests
EXAMPLE_DIR=examples

LIB_OBJ=$(BUILD_DIR)/nautylus.o
CRYPTO_OBJ=$(BUILD_DIR)/nautylus_crypto.o
STATIC_LIB=$(BUILD_DIR)/libnautylus.a
SHARED_LIB=$(BUILD_DIR)/libnautylus.so
PKG_CONFIG_FILE=$(BUILD_DIR)/nautylus.pc
CLI_OBJ=$(BUILD_DIR)/nautylus.c99main.o
TEST_OBJ=$(BUILD_DIR)/test_nautylus.o
FUZZ_OBJ=$(BUILD_DIR)/fuzz_nautylus.o
EXAMPLE_SRCS=$(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS=$(patsubst $(EXAMPLE_DIR)/%.c,$(BUILD_DIR)/%,$(EXAMPLE_SRCS))

CLI=$(BUILD_DIR)/nautylus
TEST_BIN=$(BUILD_DIR)/test_nautylus
FUZZ_BIN=$(BUILD_DIR)/fuzz_nautylus

all: $(CLI) $(STATIC_LIB) $(SHARED_LIB) $(PKG_CONFIG_FILE)

test: $(TEST_BIN) $(CLI)
	./$(TEST_BIN)

examples: $(EXAMPLE_BINS)

bindings: $(SHARED_LIB)

check: test examples

sanitizer:
	$(MAKE) clean
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 $(MAKE) test CFLAGS='-std=c99 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 $(MAKE) fuzz-smoke CFLAGS='-std=c99 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'

fuzz-smoke: $(FUZZ_BIN)
	./$(FUZZ_BIN) $(wildcard $(TEST_DIR)/corpus/*)

fuzz: $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus_crypto.c $(TEST_DIR)/fuzz_nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(FUZZ_CC) $(CPPFLAGS) -DNAUTYLUS_LIBFUZZER -std=c99 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=fuzzer,address,undefined $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus_crypto.c $(TEST_DIR)/fuzz_nautylus.c -o $(BUILD_DIR)/fuzz_nautylus_libfuzzer $(LDLIBS) $(CRYPTO_LDLIBS)

profile: $(CLI)
	sh scripts/profile.sh ./$(CLI) $(BUILD_DIR)/profile

release-check: clean all check fuzz-smoke

perf: $(CLI)
	./$(CLI) bench $(BUILD_DIR)/perf.ng 1000

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CLI): $(LIB_OBJ) $(CRYPTO_OBJ) $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(CRYPTO_LDLIBS)

$(SHARED_LIB): $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus_crypto.c -o $@ $(LDLIBS) $(CRYPTO_LDLIBS)

$(STATIC_LIB): $(LIB_OBJ) $(CRYPTO_OBJ)
	$(AR) rcs $@ $^

$(PKG_CONFIG_FILE): nautylus.pc.in FORCE | $(BUILD_DIR)
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@LIBDIR@|$(LIBDIR)|g' -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' -e 's|@VERSION@|$(VERSION)|g' -e 's|@LDLIBS@|$(LDLIBS) $(CRYPTO_LDLIBS)|g' $< > $@

$(TEST_BIN): $(LIB_OBJ) $(CRYPTO_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(CRYPTO_LDLIBS)

$(FUZZ_BIN): $(LIB_OBJ) $(CRYPTO_OBJ) $(FUZZ_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(CRYPTO_LDLIBS)

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIB_OBJ) $(CRYPTO_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(CRYPTO_LDLIBS)

$(LIB_OBJ): $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus.c -o $@

$(CRYPTO_OBJ): $(SRC_DIR)/nautylus_crypto.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus_crypto.c -o $@

$(CLI_OBJ): $(SRC_DIR)/nautylus.c99main.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus.c99main.c -o $@

$(TEST_OBJ): $(TEST_DIR)/test_nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DNAUTYLUS_CLI=\"./$(CLI)\" $(CFLAGS) -c $(TEST_DIR)/test_nautylus.c -o $@

$(FUZZ_OBJ): $(TEST_DIR)/fuzz_nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(TEST_DIR)/fuzz_nautylus.c -o $@

$(BUILD_DIR)/%.o: $(EXAMPLE_DIR)/%.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) build-asan
	rm -f test.ng

install: all
	$(INSTALL) -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 0755 $(CLI) $(DESTDIR)$(BINDIR)/nautylus
	$(INSTALL) -m 0644 $(STATIC_LIB) $(DESTDIR)$(LIBDIR)/libnautylus.a
	$(INSTALL) -m 0755 $(SHARED_LIB) $(DESTDIR)$(LIBDIR)/libnautylus.so
	$(INSTALL) -m 0644 $(SRC_DIR)/nautylus.h $(DESTDIR)$(INCLUDEDIR)/nautylus.h
	$(INSTALL) -m 0644 $(PKG_CONFIG_FILE) $(DESTDIR)$(PKGCONFIGDIR)/nautylus.pc

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/nautylus
	rm -f $(DESTDIR)$(LIBDIR)/libnautylus.a
	rm -f $(DESTDIR)$(LIBDIR)/libnautylus.so
	rm -f $(DESTDIR)$(INCLUDEDIR)/nautylus.h
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/nautylus.pc

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
CRYPTO_LDLIBS ?= -ldl
else
CRYPTO_LDLIBS ?=
endif

FORCE:

.PHONY: all test examples bindings check sanitizer fuzz-smoke fuzz profile release-check perf clean install uninstall FORCE
