CC      = cc
CFLAGS  = -std=gnu11 -Wall -Wextra -g -O0 -Iinclude -D_XOPEN_SOURCE=700 -Wno-deprecated-declarations
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=build/%.o)
DEMOS   = $(patsubst demos/%.c,bin/%,$(wildcard demos/*.c))

all: $(DEMOS)

build/%.o: src/%.c include/mpt.h src/mpt_internal.h | build
	$(CC) $(CFLAGS) -c $< -o $@

bin/%: demos/%.c $(OBJ) | bin
	$(CC) $(CFLAGS) $< $(OBJ) -o $@

bin/test_basic: tests/test_basic.c $(OBJ) | bin
	$(CC) $(CFLAGS) $< $(OBJ) -o $@

build bin:
	mkdir -p $@

run: all
	@for d in $(DEMOS); do echo; echo "=== $$d ==="; ./$$d; done

test: bin/test_basic
	./bin/test_basic

clean:
	rm -rf build bin

.PHONY: all run test clean
