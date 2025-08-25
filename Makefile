# Cross-platform Makefile for cpdd
CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 -Iinclude

# Platform-specific settings
UNAME_S := $(shell uname -s)
#ifeq ($(UNAME_S),Linux)
#CFLAGS += -D_GNU_SOURCE
#endif

# Source files
CPDD_SRCS = src/cpdd/args.c src/cpdd/copy.c src/cpdd/main.c src/cpdd/matching.c src/common/md5.c src/common/terminal.c
SYNDIR_SRCS = src/syndir/args.c src/syndir/core.c src/syndir/main.c

# Object files
CPDD_OBJS = obj/cpdd/args.o obj/cpdd/copy.o obj/cpdd/main.o obj/cpdd/matching.o obj/common/md5.o obj/common/terminal.o
SYNDIR_OBJS = obj/syndir/args.o obj/syndir/core.o obj/syndir/main.o

.PHONY: all clean install uninstall docs help

all: cpdd syndir

cpdd: $(CPDD_OBJS)
	$(CC) $(CPDD_OBJS) -o cpdd

syndir: $(SYNDIR_OBJS)
	$(CC) $(SYNDIR_OBJS) -lm -o syndir

obj/cpdd/%.o: src/cpdd/%.c | obj/cpdd
	$(CC) $(CFLAGS) -c $< -o $@

obj/syndir/%.o: src/syndir/%.c | obj/syndir
	$(CC) $(CFLAGS) -c $< -o $@

obj/common/%.o: src/common/%.c | obj/common
	$(CC) $(CFLAGS) -c $< -o $@

obj/cpdd obj/syndir obj/common:
	mkdir -p $@

clean:
	rm -rf obj cpdd syndir docs/*.txt

install: cpdd syndir
	install -d $(DESTDIR)/usr/local/bin
	install -d $(DESTDIR)/usr/local/share/man/man1
	install -m 755 cpdd $(DESTDIR)/usr/local/bin/
	install -m 755 syndir $(DESTDIR)/usr/local/bin/
	install -m 644 man/cpdd.1 $(DESTDIR)/usr/local/share/man/man1/
	install -m 644 man/syndir.1 $(DESTDIR)/usr/local/share/man/man1/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/cpdd
	rm -f $(DESTDIR)/usr/local/bin/syndir
	rm -f $(DESTDIR)/usr/local/share/man/man1/cpdd.1
	rm -f $(DESTDIR)/usr/local/share/man/man1/syndir.1

# Generate text versions of man pages for GitHub viewing
docs: docs/cpdd.txt docs/syndir.txt

docs/%.txt: man/%.1
	@mkdir -p docs
	@if command -v man >/dev/null 2>&1; then \
		MANWIDTH=80 man -P cat ./$< | col -bx > $@; \
	elif command -v groff >/dev/null 2>&1; then \
		groff -man -Tascii $< | col -bx > $@; \
	elif command -v nroff >/dev/null 2>&1; then \
		nroff -man $< | col -bx > $@; \
	else \
		echo "Warning: No man page formatter found. Install man, groff, or nroff to generate text documentation."; \
		echo "Fallback: copying raw file with .txt extension"; \
		cp $< $@; \
	fi

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all      - Build the main program and syndir (default)"
	@echo "  cpdd     - Build only the main program"
	@echo "  syndir   - Build only the synthetic directory generator"
	@echo "  docs     - Generate text versions of man pages for GitHub"
	@echo "  install  - Install to /usr/local/bin and man pages"
	@echo "  uninstall- Remove from /usr/local/bin and man pages"
	@echo "  clean    - Remove build artifacts and generated docs"
	@echo "  help     - Show this help message"