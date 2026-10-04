CC ?= clang
PKG_CFLAGS = $(shell pkg-config --cflags xkbcommon wayland-client 2>/dev/null)
PKG_LIBS = $(shell pkg-config --libs xkbcommon wayland-client 2>/dev/null)

CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(PKG_CFLAGS) -I.
LDFLAGS ?= $(PKG_LIBS) -lwayland-client -lxkbcommon

WAYLAND_PROTOCOLS_DIR ?= /usr/share/wayland-protocols
WLROOTS_PROTOCOLS_DIR ?= ../../../subprojects/wlroots/protocol

PROTOCOLS = \
	ext-session-lock-v1 \
	idle-inhibit-unstable-v1 \
	virtual-keyboard-unstable-v1 \
	ext-idle-notify-v1 \
	xdg-shell

PROTO_HEADERS = $(patsubst %,%-client-protocol.h,$(PROTOCOLS))
PROTO_SOURCES = $(patsubst %,%-protocol.c,$(PROTOCOLS))
PROTO_OBJS = $(patsubst %,%-protocol.o,$(PROTOCOLS))

BINS = \
	test_session_lock \
	test_session_lock_focus \
	test_redraw_idle \
	test_cursor_idle

all: $(BINS)

# Protocol generation rules
ext-session-lock-v1-client-protocol.h:
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_DIR)/staging/ext-session-lock/ext-session-lock-v1.xml $@

ext-session-lock-v1-protocol.c:
	wayland-scanner private-code $(WAYLAND_PROTOCOLS_DIR)/staging/ext-session-lock/ext-session-lock-v1.xml $@

idle-inhibit-unstable-v1-client-protocol.h:
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_DIR)/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml $@

idle-inhibit-unstable-v1-protocol.c:
	wayland-scanner private-code $(WAYLAND_PROTOCOLS_DIR)/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml $@

virtual-keyboard-unstable-v1-client-protocol.h:
	wayland-scanner client-header $(WLROOTS_PROTOCOLS_DIR)/virtual-keyboard-unstable-v1.xml $@

virtual-keyboard-unstable-v1-protocol.c:
	wayland-scanner private-code $(WLROOTS_PROTOCOLS_DIR)/virtual-keyboard-unstable-v1.xml $@

ext-idle-notify-v1-client-protocol.h:
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_DIR)/staging/ext-idle-notify/ext-idle-notify-v1.xml $@

ext-idle-notify-v1-protocol.c:
	wayland-scanner private-code $(WAYLAND_PROTOCOLS_DIR)/staging/ext-idle-notify/ext-idle-notify-v1.xml $@

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c:
	wayland-scanner private-code $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml $@

%-protocol.o: %-protocol.c %-client-protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

test_session_lock: test_session_lock.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

test_session_lock_focus: test_session_lock_focus.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

test_redraw_idle: test_redraw_idle.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

test_cursor_idle: test_cursor_idle.c $(PROTO_OBJS) $(PROTO_HEADERS)
	$(CC) $(CFLAGS) $< $(PROTO_OBJS) $(LDFLAGS) -o $@

clean:
	rm -f $(BINS) $(PROTO_HEADERS) $(PROTO_SOURCES) $(PROTO_OBJS)

.PHONY: all clean
