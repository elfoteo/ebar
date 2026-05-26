CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk+-3.0 gtk-layer-shell-0 upower-glib librsvg-2.0 libnm) -Isrc -Wall -Wextra
LIBS = $(shell pkg-config --libs gtk+-3.0 gtk-layer-shell-0 upower-glib librsvg-2.0 libnm) -lpthread -lm
TARGET = build/ebar
SRC = $(wildcard src/*.c)

$(TARGET): $(SRC)
	mkdir -p build
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS) $(LIBS)

clean:
	rm -rf build

.PHONY: clean
