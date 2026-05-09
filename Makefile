# -----------------------------------------------------------------------
# Makefile — SCRAWL multithreaded web crawler
# -----------------------------------------------------------------------

.PHONY: all gui clean run run-gui memcheck help

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -pthread -O2 \
           -D_POSIX_C_SOURCE=200809L \
           -Iinclude
LDFLAGS = -pthread -lcurl

# ---- Raylib (installed via sudo make install to /usr/local) ----
RAYLIB_CFLAGS  = -I/usr/local/include
RAYLIB_LDFLAGS = -L/usr/local/lib -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

TARGET     = crawler
GUI_TARGET = crawler-gui

ARGS ?= https://example.com

# -----------------------------------------------------------------------
# CLI sources
# -----------------------------------------------------------------------

SRCS = main.c \
       src/queue.c \
       src/hash.c \
       src/fetch.c \
       src/parse.c \
       src/robots.c \
       src/crawler.c

OBJS = $(SRCS:.c=.o)

# -----------------------------------------------------------------------
# GUI sources
# -----------------------------------------------------------------------

GUI_SRCS = gui/gui_main.c \
           gui/gui.c \
           src/queue.c \
           src/hash.c \
           src/fetch.c \
           src/parse.c \
           src/robots.c \
           src/crawler.c

GUI_OBJS = $(GUI_SRCS:.c=.gui.o)

# -----------------------------------------------------------------------
# Default target — CLI
# -----------------------------------------------------------------------

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "Build successful!  Run: ./$(TARGET) [OPTIONS] <seed-url>"
	@echo "               Or: make run ARGS=\"[OPTIONS] <seed-url>\""

# -----------------------------------------------------------------------
# GUI target
# -----------------------------------------------------------------------

gui: $(GUI_TARGET)

$(GUI_TARGET): $(GUI_OBJS)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $^ $(LDFLAGS) $(RAYLIB_LDFLAGS)
	@echo ""
	@echo "GUI build successful!  Run: ./$(GUI_TARGET)"

# -----------------------------------------------------------------------
# Compilation rules
# -----------------------------------------------------------------------

# GUI objects — compiled with -DGUI_BUILD to gate gui_log_push calls
%.gui.o: %.c
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -DGUI_BUILD -c -o $@ $<

# CLI objects
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# -----------------------------------------------------------------------
# Explicit header dependencies
# -----------------------------------------------------------------------

main.o:              main.c            include/crawler.h include/queue.h include/hash.h include/parse.h
src/queue.o:         src/queue.c       include/queue.h
src/hash.o:          src/hash.c        include/hash.h
src/fetch.o:         src/fetch.c       include/fetch.h
src/parse.o:         src/parse.c       include/parse.h
src/robots.o:        src/robots.c      include/robots.h include/fetch.h
src/crawler.o:       src/crawler.c     include/crawler.h include/fetch.h include/parse.h \
                                       include/queue.h include/hash.h include/robots.h

gui/gui.gui.o:       gui/gui.c         gui/gui.h include/crawler.h
gui/gui_main.gui.o:  gui/gui_main.c    gui/gui.h include/crawler.h \
                                       include/queue.h include/hash.h include/parse.h

src/queue.gui.o:     src/queue.c       include/queue.h
src/hash.gui.o:      src/hash.c        include/hash.h
src/fetch.gui.o:     src/fetch.c       include/fetch.h
src/parse.gui.o:     src/parse.c       include/parse.h
src/robots.gui.o:    src/robots.c      include/robots.h include/fetch.h
src/crawler.gui.o:   src/crawler.c     include/crawler.h include/fetch.h include/parse.h \
                                       include/queue.h include/hash.h include/robots.h gui/gui.h

# -----------------------------------------------------------------------
# Utility targets
# -----------------------------------------------------------------------

clean:
	rm -f $(OBJS) $(GUI_OBJS) $(TARGET) $(GUI_TARGET)

run: $(TARGET)
	./$(TARGET) $(ARGS)

run-gui: $(GUI_TARGET)
	./$(GUI_TARGET)

memcheck: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 \
	    ./$(TARGET) $(ARGS)

help:
	@echo ""
	@echo "Usage:  make [target] [ARGS=\"...\"]"
	@echo ""
	@echo "Targets:"
	@echo "  all        Build CLI crawler (default)"
	@echo "  gui        Build Raylib GUI  (./crawler-gui)"
	@echo "  run        Build + run CLI   (use ARGS= to pass flags)"
	@echo "  run-gui    Build + run GUI"
	@echo "  memcheck   Run CLI under valgrind"
	@echo "  clean      Remove all build artifacts"
	@echo "  help       Show this help"
	@echo ""
	@echo "CLI flags (pass via ARGS=\"...\"):"
	@echo "  -t <N>     Threads       (default: 4)"
	@echo "  -n <N>     Max pages     (default: 1)"
	@echo "  -o <file>  JSONL output file"
	@echo "  -d         Domain scope only"
	@echo "  -v         Verbose output"
	@echo "  -h         Crawler help"
	@echo ""
	@echo "Examples:"
	@echo "  make run ARGS=\"-t 8 -n 500 -d -v https://example.com\""
	@echo "  make gui && make run-gui"
	@echo ""
