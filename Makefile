# Earshot - build the receiver library, the device transmitter, the CLI tools
# and the tests. See tests/run.sh for the CI entry point.

CC      ?= cc
OPT     ?= -O2
CSTD    ?= -std=c99
WARN    ?= -Wall -Wextra
CFLAGS  ?= $(OPT) $(CSTD) $(WARN)
LDLIBS  := -lm
AR      ?= ar

RX_INC  := -Iinclude -Isrc
TX_INC  := -Iinclude -Isrc/tx

RX_SRC  := src/earshot.c src/fountain.c src/envelope.c src/siphash.c src/crc.c
RX_OBJ  := $(RX_SRC:.c=.o)
RX_HDR  := include/earshot.h src/earshot_internal.h

TX_OBJ  := src/tx/earshot_tx.o
TX_HDR  := include/earshot_tx.h src/tx/soliton_table.h

.PHONY: all test clean
all: libearshot.a tests/decode tests/test_unit tests/emit

libearshot.a: $(RX_OBJ)
	$(AR) rcs $@ $^

$(RX_OBJ): %.o: %.c $(RX_HDR)
	$(CC) $(CFLAGS) $(RX_INC) -c $< -o $@

src/tx/earshot_tx.o: src/tx/earshot_tx.c $(TX_HDR)
	$(CC) $(CFLAGS) $(TX_INC) -c $< -o $@

tests/decode: tests/decode.c libearshot.a
	$(CC) $(CFLAGS) $(RX_INC) $< libearshot.a $(LDLIBS) -o $@

tests/test_unit: tests/test_unit.c $(RX_OBJ)
	$(CC) $(CFLAGS) $(RX_INC) $< $(RX_OBJ) $(LDLIBS) -o $@

tests/emit: tests/emit.c $(TX_OBJ) src/siphash.o src/crc.o
	$(CC) $(CFLAGS) $(TX_INC) $< $(TX_OBJ) src/siphash.o src/crc.o -o $@

test: all
	./tests/test_unit
	python3 tests/test_e2e.py

clean:
	rm -f $(RX_OBJ) $(TX_OBJ) libearshot.a tests/decode tests/test_unit tests/emit
