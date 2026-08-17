CC?=cc
CFLAGS?=-std=c99 -Wall -Wextra -Wpedantic -O2
LDLIBS?=-lm
CPPFLAGS?=-Isrc

BUILD_DIR=build
SRC_DIR=src
TEST_DIR=tests
EXAMPLE_DIR=examples

LIB_OBJ=$(BUILD_DIR)/nautylus.o
CRYPTO_OBJ=$(BUILD_DIR)/nautylus_crypto.o
SHARED_LIB=$(BUILD_DIR)/libnautylus.so
CLI_OBJ=$(BUILD_DIR)/nautylus.c99main.o
TEST_OBJ=$(BUILD_DIR)/test_nautylus.o
EXAMPLE_SRCS=$(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS=$(patsubst $(EXAMPLE_DIR)/%.c,$(BUILD_DIR)/%,$(EXAMPLE_SRCS))

CLI=$(BUILD_DIR)/nautylus
TEST_BIN=$(BUILD_DIR)/test_nautylus

all: $(CLI) $(SHARED_LIB)

test: $(TEST_BIN) $(CLI)
	./$(TEST_BIN)

examples: $(EXAMPLE_BINS)

bindings: $(SHARED_LIB)

perf: $(CLI)
	./$(CLI) bench $(BUILD_DIR)/perf.ng 1000

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CLI): $(LIB_OBJ) $(CRYPTO_OBJ) $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(CRYPTO_LDLIBS)

$(SHARED_LIB): $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus_crypto.c -o $@ $(LDLIBS) $(CRYPTO_LDLIBS)

$(TEST_BIN): $(LIB_OBJ) $(CRYPTO_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(CRYPTO_LDLIBS)

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(LIB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(LIB_OBJ): $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus.c -o $@

$(CRYPTO_OBJ): $(SRC_DIR)/nautylus_crypto.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus_crypto.c -o $@

$(CLI_OBJ): $(SRC_DIR)/nautylus.c99main.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus.c99main.c -o $@

$(TEST_OBJ): $(TEST_DIR)/test_nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DNAUTYLUS_CLI=\"./$(CLI)\" $(CFLAGS) -c $(TEST_DIR)/test_nautylus.c -o $@

$(BUILD_DIR)/%.o: $(EXAMPLE_DIR)/%.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) build-asan
	rm -f test.ng

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
CRYPTO_LDLIBS ?= -ldl
else
CRYPTO_LDLIBS ?=
endif

.PHONY: all test examples bindings perf clean
