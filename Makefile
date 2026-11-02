CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L

BUILD := build
LIB   := $(BUILD)/libsimplefs.a
DEMO  := $(BUILD)/fs_demo

.PHONY: all run clean

all: $(DEMO)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/fs.o: src/fs.c include/fs.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(BUILD)/fs.o
	ar rcs $@ $^

$(DEMO): examples/demo.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -L$(BUILD) -lsimplefs -o $@

run: $(DEMO)
	./$(DEMO)

clean:
	rm -rf $(BUILD) demo_disk.img
