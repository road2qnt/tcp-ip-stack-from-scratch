CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -I./magi_system
TARGET := magi_system.out

SRCS := \
	magi_system/main.c \
	magi_system/simulator.c \
	magi_system/core/interface.c \
	magi_system/core/link.c \
	magi_system/core/mac.c \
	magi_system/core/packet.c \
	magi_system/dataStructure/map.c \
	magi_system/layer3/ipv4.c \
	magi_system/utils/cli.c

.PHONY: run build clean

run: build
	./$(TARGET)

build:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
