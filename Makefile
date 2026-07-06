CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 \
          $(shell pkg-config --cflags x11 xrender xext xcomposite libpng)
LDFLAGS = $(shell pkg-config --libs   x11 xrender xext xcomposite libpng) \
          -lXext -lm

TARGET  = xteddy-ng

.PHONY: all clean install

all: $(TARGET)

$(TARGET): xteddy-ng.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
