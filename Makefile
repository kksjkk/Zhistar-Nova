CC      = gcc
CSTD    = -std=c11
WARN    = -Wall -Wextra -Wpedantic 
CFLAGS  = -g -O2 $(CSTD) $(WARN) $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 libsoup-3.0) -DPACKAGE_VERSION=\"1.0.0\"
LDFLAGS = -rdynamic $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1 libsoup-3.0) -lpthread -lm

SRCDIR  = src
OBJDIR  = build
TARGET  = ZhiStar-Nova

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

all: $(TARGET)
	@echo "Build complete: ./$(TARGET)"

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)
	rm -f $(HOME)/.local/bin/$(TARGET)
	@DESKTOP_DIR=$$(xdg-user-dir DESKTOP 2>/dev/null || echo ~/Desktop); \
	rm -f "$$DESKTOP_DIR/$(TARGET).desktop"

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -Dm644 data/logo.png $(DESTDIR)/usr/share/pixmaps/zhistar-nova.png
	install -Dm644 data/com.zhistar.nova.desktop $(DESTDIR)/usr/share/applications/com.zhistar.nova.desktop
	install -Dm644 data/com.zhistar.nova.metainfo.xml $(DESTDIR)/usr/share/metainfo/com.zhistar.nova.metainfo.xml

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/share/pixmaps/zhistar-nova.png
	rm -f $(DESTDIR)/usr/share/applications/com.zhistar.nova.desktop
	rm -f $(DESTDIR)/usr/share/metainfo/com.zhistar.nova.metainfo.xml

run: all
	./$(TARGET)

desktop: $(TARGET)
	@DESKTOP_DIR=$$(xdg-user-dir DESKTOP 2>/dev/null || echo ~/Desktop); \
	mkdir -p $(HOME)/.local/bin; \
	ln -sf "$(shell realpath $(TARGET))" $(HOME)/.local/bin/$(TARGET); \
	sed 's|Exec=.*|Exec=$(HOME)/.local/bin/$(TARGET)|' data/com.zhistar.nova.desktop > "$$DESKTOP_DIR/$(TARGET).desktop"; \
	chmod +x "$$DESKTOP_DIR/$(TARGET).desktop"; \
	gio set "$$DESKTOP_DIR/$(TARGET).desktop" metadata::trusted true 2>/dev/null; \
	echo "Desktop shortcut created: $$DESKTOP_DIR/$(TARGET).desktop"

.PHONY: all clean run install uninstall desktop
