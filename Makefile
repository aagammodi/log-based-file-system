
CC = gcc

CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS =

FUSE_CFLAGS := $(shell pkg-config --cflags fuse3 2>/dev/null || echo -D_FILE_OFFSET_BITS=64)
FUSE_LIBS   := $(shell pkg-config --libs fuse3 2>/dev/null || echo -lfuse3)

.PHONY: all clean test-task-a test-crc test-cache test-segment-buffer mount.logfs logfs_stats

all: mkfs.logfs

mkfs.logfs: src/mkfs.o src/disk.o src/crc32.o
	$(CC) $(LDFLAGS) -o $@ $^

mount.logfs: fuse/mount.o $(filter-out src/mkfs.o, $(wildcard src/*.o))
	$(CC) $(LDFLAGS) -o $@ $^ $(FUSE_LIBS)

fuse/mount.o: fuse/mount.c include/lfs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FUSE_CFLAGS) -c -o $@ $<

logfs_stats:
	$(MAKE) -C kernel

tests/test_task_a: tests/test_task_a.c src/disk.o src/crc32.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-task-a: mkfs.logfs tests/test_task_a
	./mkfs.logfs
	./tests/test_task_a

src/%.o: src/%.c include/logfs.h include/cache.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

tests/test_crc_failure: tests/test_crc_failure.c src/crc32.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-crc: mkfs.logfs tests/test_crc_failure
	./mkfs.logfs
	./tests/test_crc_failure

tests/test_cache: tests/test_cache.c src/cache.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-cache: tests/test_cache
	./tests/test_cache

tests/test_segment_buffer: tests/test_segment_buffer.c src/segment_buffer.o src/disk.o src/crc32.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^

test-segment-buffer: mkfs.logfs tests/test_segment_buffer
	./mkfs.logfs
	./tests/test_segment_buffer

clean:
	rm -f src/*.o fuse/*.o mkfs.logfs mount.logfs tests/test_task_a tests/test_crc_failure tests/test_cache tests/test_segment_buffer
	$(MAKE) -C kernel clean 2>/dev/null || true



