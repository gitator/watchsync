CC = gcc
CFLAGS = -O2 -Wall -Wextra
TARGET = bin/watchsync
SRC = src/watchsync.c
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -rf bin

install: $(TARGET)
	install -D -m 755 $(TARGET) $(PREFIX)/bin/watchsync
	install -D -m 644 etc/watchsync.conf /etc/watchsync.conf
	mkdir -p /etc/watchsync.d
	@echo "Remember to install etc/watchsync.service to /etc/systemd/system/ if needed."

uninstall:
	rm -f $(PREFIX)/bin/watchsync
	rm -f /etc/watchsync.conf
	rm -rf /etc/watchsync.d

.PHONY: all clean install uninstall
