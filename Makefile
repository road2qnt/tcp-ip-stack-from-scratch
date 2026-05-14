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
	magi_system/layer2/host.c \
	magi_system/layer2/switch.c \
	magi_system/layer2/arp.c \
	magi_system/layer3/ipv4.c \
	magi_system/layer3/router.c \
	magi_system/utils/cli.c \
	magi_system/utils/json.c \
	magi_system/utils/loader.c \
	magi_system/utils/visualizer.c \
	magi_system/utils/debugger.c

.PHONY: run build clean

run: build
	./$(TARGET)

build:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
