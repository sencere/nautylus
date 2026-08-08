CC?=cc
CFLAGS?=-std=c99 -Wall -Wextra -Wpedantic -O2
CPPFLAGS?=-Isrc

BUILD_DIR=build
SRC_DIR=src
TEST_DIR=tests
EXAMPLE_DIR=examples

LIB_OBJ=$(BUILD_DIR)/nautylus.o
CLI_OBJ=$(BUILD_DIR)/nautylus.c99main.o
TEST_OBJ=$(BUILD_DIR)/test_nautylus.o
EXAMPLE_OBJ=$(BUILD_DIR)/basic.o

CLI=$(BUILD_DIR)/nautylus
TEST_BIN=$(BUILD_DIR)/test_nautylus
EXAMPLE_BIN=$(BUILD_DIR)/basic

all: $(CLI)

test: $(TEST_BIN) $(CLI)
	./$(TEST_BIN)

examples: $(EXAMPLE_BIN)

perf: $(CLI)
	./$(CLI) bench $(BUILD_DIR)/perf.ng 1000

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(CLI): $(LIB_OBJ) $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_BIN): $(LIB_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(EXAMPLE_BIN): $(LIB_OBJ) $(EXAMPLE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(LIB_OBJ): $(SRC_DIR)/nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus.c -o $@

$(CLI_OBJ): $(SRC_DIR)/nautylus.c99main.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SRC_DIR)/nautylus.c99main.c -o $@

$(TEST_OBJ): $(TEST_DIR)/test_nautylus.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DNAUTYLUS_CLI=\"./$(CLI)\" $(CFLAGS) -c $(TEST_DIR)/test_nautylus.c -o $@

$(EXAMPLE_OBJ): $(EXAMPLE_DIR)/basic.c $(SRC_DIR)/nautylus.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(EXAMPLE_DIR)/basic.c -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f test.ng

.PHONY: all test examples perf clean
