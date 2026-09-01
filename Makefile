# Earshot - build the receiver library, the CLI decoder and the tests.
# See tests/run.sh for the CI entry point.

CC      ?= cc
OPT     ?= -O2
CSTD    ?= -std=c99
WARN    ?= -Wall -Wextra
CFLAGS  ?= $(OPT) $(CSTD) $(WARN) -Iinclude -Isrc
LDLIBS  := -lm
AR      ?= ar

LIB_SRC := src/earshot.c src/fountain.c src/envelope.c src/siphash.c src/crc.c
LIB_OBJ := $(LIB_SRC:.c=.o)
HEADERS := include/earshot.h src/earshot_internal.h

.PHONY: all test clean
all: libearshot.a tests/decode tests/test_unit

libearshot.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

tests/decode: tests/decode.c libearshot.a
	$(CC) $(CFLAGS) $< libearshot.a $(LDLIBS) -o $@

tests/test_unit: tests/test_unit.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $< $(LIB_OBJ) $(LDLIBS) -o $@

test: all
	./tests/test_unit
	python3 tests/test_e2e.py

clean:
	rm -f $(LIB_OBJ) libearshot.a tests/decode tests/test_unit
