CC ?= cc
PKG_CONFIG ?= pkg-config
TARGET := lsl
PREFIX ?= /usr/local
CFLAGS ?= -O3 -march=native -flto
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS ?= -flto

LUA_PKG := $(shell for p in luajit lua5.5 lua5.4 lua5.3 lua; do $(PKG_CONFIG) --exists $$p 2>/dev/null && { echo $$p; break; }; done)
ifneq ($(LUA_PKG),)
CPPFLAGS += -DLSL_WITH_LUA $(shell $(PKG_CONFIG) --cflags $(LUA_PKG))
LDLIBS += $(shell $(PKG_CONFIG) --libs $(LUA_PKG))
endif

all: $(TARGET)
$(TARGET): main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

debug: CFLAGS := -O0 -g3 -std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined
debug: LDFLAGS := -fsanitize=address,undefined
debug: clean $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -Dm644 config.lua $(DESTDIR)$(PREFIX)/share/lsl/config.lua
clean:
	rm -f $(TARGET)
.PHONY: all debug install clean
