CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Iinclude -Isrc
AR ?= ar
ARFLAGS ?= rcs

LIB = libnautylus.a
SRC = src/graph.c src/slab.c src/vector.c
OBJ = $(SRC:.c=.o)
CLI = nautylus

TEST_GRAPH = tests/c/test_graph
TEST_VECTOR = tests/c/test_vector

.PHONY: all clean test

all: $(LIB) $(CLI)

$(LIB): $(OBJ)
	$(AR) $(ARFLAGS) $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_GRAPH): tests/c/test_graph.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lnautylus -o $@

$(TEST_VECTOR): tests/c/test_vector.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -lnautylus -o $@

test: $(TEST_GRAPH) $(TEST_VECTOR)
	./$(TEST_GRAPH)
	./$(TEST_VECTOR)

clean:
	rm -f $(OBJ) $(LIB) $(TEST_GRAPH) $(TEST_VECTOR) $(CLI)
$(CLI): src/nautylus_cli.c src/jsmn.c $(LIB)
	$(CC) $(CFLAGS) $^ -L. -lnautylus -o $@
