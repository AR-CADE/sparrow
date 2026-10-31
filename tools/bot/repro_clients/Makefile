CC ?= clang
PKG_CFLAGS = $(shell pkg-config --cflags xkbcommon wayland-client 2>/dev/null)
PKG_LIBS = $(shell pkg-config --libs xkbcommon wayland-client 2>/dev/null)

CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(PKG_CFLAGS) -I.
LDFLAGS ?= $(PKG_LIBS) -lwayland-client -lxkbcommon -lrt -lm

WAYLAND_PROTOCOLS_DIR ?= /usr/share/wayland-protocols
WLROOTS_PROTOCOLS_DIR ?= ../../../subprojects/wlroots/protocol

PROTOCOLS = xdg-shell

PROTO_HEADERS = $(patsubst %,%-client-protocol.h,$(PROTOCOLS))
PROTO_SOURCES = $(patsubst %,%-protocol.c,$(PROTOCOLS))
PROTO_OBJS = $(patsubst %,%-protocol.o,$(PROTOCOLS))

BINS = \
	repro_tooltip_argb \
	repro_subsurface_popup \
	pointer_calibrator \
	jank_monitor \
	sparrow_key \
	sparrow_touch \
	fixed_size_client \
	fullscreen_shm_client

all: $(BINS)

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c:
	wayland-scanner private-code $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml $@

virtual-keyboard-unstable-v1-client-protocol.h:
	wayland-scanner client-header $(WLROOTS_PROTOCOLS_DIR)/virtual-keyboard-unstable-v1.xml $@

virtual-keyboard-unstable-v1-protocol.c:
	wayland-scanner private-code $(WLROOTS_PROTOCOLS_DIR)/virtual-keyboard-unstable-v1.xml $@

virtual-keyboard-unstable-v1-protocol.o: virtual-keyboard-unstable-v1-protocol.c virtual-keyboard-unstable-v1-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

wlr-virtual-pointer-unstable-v1-client-protocol.h:
	wayland-scanner client-header $(WLROOTS_PROTOCOLS_DIR)/wlr-virtual-pointer-unstable-v1.xml $@

wlr-virtual-pointer-unstable-v1-protocol.c:
	wayland-scanner private-code $(WLROOTS_PROTOCOLS_DIR)/wlr-virtual-pointer-unstable-v1.xml $@

wlr-virtual-pointer-unstable-v1-protocol.o: wlr-virtual-pointer-unstable-v1-protocol.c wlr-virtual-pointer-unstable-v1-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

%-protocol.o: %-protocol.c %-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

repro_tooltip_argb: repro_tooltip_argb.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

repro_subsurface_popup: repro_subsurface_popup.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

pointer_calibrator: pointer_calibrator.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

jank_monitor: jank_monitor.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

sparrow_key: sparrow_key.c virtual-keyboard-unstable-v1-protocol.o virtual-keyboard-unstable-v1-client-protocol.h
	$(CC) $(CFLAGS) $< virtual-keyboard-unstable-v1-protocol.o $(LDFLAGS) -o $@

sparrow_touch: sparrow_touch.c wlr-virtual-pointer-unstable-v1-protocol.o wlr-virtual-pointer-unstable-v1-client-protocol.h
	$(CC) $(CFLAGS) $< wlr-virtual-pointer-unstable-v1-protocol.o $(LDFLAGS) -o $@

fixed_size_client: fixed_size_client.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

fullscreen_shm_client: fullscreen_shm_client.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

clean:
	rm -f $(BINS) *-protocol.o *-protocol.c *-client-protocol.h

tidy: $(PROTO_HEADERS) wlr-virtual-pointer-unstable-v1-client-protocol.h virtual-keyboard-unstable-v1-client-protocol.h
	clang-tidy -checks='-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling,-bugprone-reserved-identifier' $(patsubst %,%.c,$(BINS)) -- $(CFLAGS)

.PHONY: all clean tidy
