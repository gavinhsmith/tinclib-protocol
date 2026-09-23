CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -Werror -O1
SRC = crc16.c tinc_frame.c

.PHONY: test vectors fuzz clean

test: build/test_frame
	./build/test_frame

build/test_frame: tests/test_frame.c $(SRC) protocol.h crc16.h tinc_frame.h test_vectors/vectors.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/test_frame.c $(SRC)

vectors:
	python tools/gen_vectors.py

fuzz:
	@mkdir -p build
	clang -g -fsanitize=fuzzer,address -o build/fuzz_frame tests/fuzz_frame.c $(SRC)
	./build/fuzz_frame -max_total_time=60

clean:
	rm -rf build
