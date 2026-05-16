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
	magi_system/layer2/ethernet.c \
	magi_system/layer3/ipv4.c \
	magi_system/layer3/icmp.c \
	magi_system/layer3/router.c \
	magi_system/layer4/udp.c \
	magi_system/layer4/tcp.c \
	magi_system/layer4/tcp_socket.c \
	magi_system/utils/cli.c \
	magi_system/utils/json.c \
	magi_system/utils/loader.c \
	magi_system/utils/visualizer.c \
	magi_system/utils/debugger.c \
	magi_system/layer7/magi_socket.c \
	magi_system/layer7/dhcp_server.c \
	magi_system/layer7/dns_server.c \
	magi_system/layer7/http_server.c \
	magi_system/layer7/rip.c

.PHONY: run build clean

run: build
	./$(TARGET)

build:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
