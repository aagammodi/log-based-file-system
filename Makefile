
CC = gcc

CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS =

.PHONY: all clean test-task-a test-crc

all: mkfs.logfs

mkfs.logfs: src/mkfs.o src/disk.o src/crc32.o
	$(CC) $(LDFLAGS) -o $@ $^

tests/test_task_a: tests/test_task_a.c src/disk.o src/crc32.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-task-a: mkfs.logfs tests/test_task_a
	./mkfs.logfs
	./tests/test_task_a

src/%.o: src/%.c include/logfs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

tests/test_crc_failure: tests/test_crc_failure.c src/crc32.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-crc: mkfs.logfs tests/test_crc_failure
	./mkfs.logfs
	./tests/test_crc_failure

clean:
	rm -f src/*.o mkfs.logfs tests/test_task_a tests/test_crc



