CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -I./magi_system
TARGET := magi_system.out
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

# Build with GUI support if SDL2 is available
ifneq ($(SDL_LIBS),)
    CFLAGS += $(SDL_CFLAGS) -DHAS_SDL
    GUI_SRCS := magi_system/gui/app.c
    GUI_OBJS := magi_system/gui/app.o
    LIBS := $(SDL_LIBS) -lm
else
    GUI_SRCS :=
    GUI_OBJS :=
    LIBS :=
endif

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
	magi_system/layer7/rip.c \
	$(GUI_SRCS)

.PHONY: run build clean

run: build
	./$(TARGET)

gui: build
	./$(TARGET) --gui


build:
	$(CC) $(CFLAGS) $(SRCS) $(LIBS) -o $(TARGET)

# Object file for gui (needs separate compilation for potential -MMD use)
magi_system/gui/app.o: magi_system/gui/app.c magi_system/gui/app.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
