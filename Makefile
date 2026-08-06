CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -llua
TARGET = lsl
PREFIX = /usr/local
CONFIG_DIR = $(HOME)/.config/lsl

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET) $(LIBS)

install: $(TARGET)
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@mkdir -p $(CONFIG_DIR)
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	@if [ ! -f $(CONFIG_DIR)/config.lua ]; then \
		cp config.lua $(CONFIG_DIR)/config.lua; \
		echo "Config copied to $(CONFIG_DIR)/config.lua"; \
	fi
	@echo "$(TARGET) installed to $(PREFIX)/bin/$(TARGET)"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	@echo "$(TARGET) removed from $(PREFIX)/bin/"

clean:
	rm -f $(TARGET) *.o

.PHONY: all install uninstall clean
