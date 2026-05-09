# -----------------------------------------------------------------------
# Makefile — multithreaded web crawler
# -----------------------------------------------------------------------

.PHONY: all clean run memcheck help

CC      = gcc
# _POSIX_C_SOURCE=200809L exposes pthread_rwlock_t and friends when
# compiling in strict C11 mode (which otherwise hides POSIX extensions).
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -pthread -O2 \
           -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread -lcurl

TARGET  = crawler

SRCS    = main.c \
          queue.c \
          hash.c \
          fetch.c \
          parse.c \
          robots.c \
          crawler.c

OBJS    = $(SRCS:.c=.o)

# Default seed URL; override via: make run ARGS="..."
ARGS   ?= https://example.com

# -----------------------------------------------------------------------
# Default target
# -----------------------------------------------------------------------

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "Build successful!  Run with:  ./$(TARGET) [OPTIONS] <seed-url>"
	@echo "                  Or:         make run ARGS=\"[OPTIONS] <seed-url>\""

# -----------------------------------------------------------------------
# Compile each .c to a .o  (automatic dependency on its own .h)
# -----------------------------------------------------------------------

main.o:    main.c    crawler.h queue.h hash.h parse.h
queue.o:   queue.c   queue.h
hash.o:    hash.c    hash.h
fetch.o:   fetch.c   fetch.h
parse.o:   parse.c   parse.h
robots.o:  robots.c  robots.h fetch.h
crawler.o: crawler.c crawler.h fetch.h parse.h queue.h hash.h robots.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# -----------------------------------------------------------------------
# Utility targets
# -----------------------------------------------------------------------

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET) $(ARGS)

# Optionally check for memory leaks with valgrind
memcheck: $(TARGET)
	valgrind --leak-check=full --error-exitcode=1 \
	    ./$(TARGET) $(ARGS)

help:
	@echo ""
	@echo "Usage:"
	@echo "  make [target] [ARGS=\"...\"]"
	@echo ""
	@echo "Targets:"
	@echo "  all           Build the crawler (default)"
	@echo "  run           Build and run crawler"
	@echo "  memcheck      Run under valgrind for memory leak checking"
	@echo "  clean         Remove all build artifacts"
	@echo "  help          Show this help"
	@echo ""
	@echo "Crawler flags (pass via ARGS=\"...\"):"
	@echo "  -t <N>        Number of worker threads  (default: 4)"
	@echo "  -n <N>        Max pages to fetch        (default: 200)"
	@echo "  -o <file>     Write JSONL results to file"
	@echo "  -d            Restrict crawl to seed domain only"
	@echo "  -v            Verbose output (print each URL as fetched)"
	@echo "  -h            Show crawler help"
	@echo ""
	@echo "Examples:"
	@echo "  make run"
	@echo "  make run ARGS=\"https://example.com\""
	@echo "  make run ARGS=\"-t 8 -n 500 -d -v https://example.com\""
	@echo "  make run ARGS=\"-t 4 -o results.jsonl https://example.com\""
	@echo ""
