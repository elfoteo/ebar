CC = gcc
CFLAGS = $(shell pkg-config --cflags gtk+-3.0 gtk-layer-shell-0)
LIBS = $(shell pkg-config --libs gtk+-3.0 gtk-layer-shell-0) -lpthread
TARGET = build/bar
SRC = bar.c

$(TARGET): $(SRC)
	mkdir -p build
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
