CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk+-3.0 gtk-layer-shell-0 upower-glib librsvg-2.0 libnm libpulse libpulse-mainloop-glib) -Isrc -Wall -Wextra -MMD -MP
LIBS = $(shell pkg-config --libs gtk+-3.0 gtk-layer-shell-0 upower-glib librsvg-2.0 libnm libpulse libpulse-mainloop-glib) -lpthread -lm
TARGET = build/ebar
SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)
DEP = $(OBJ:.o=.d)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -rf build

-include $(DEP)

.PHONY: clean
