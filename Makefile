# Cross-platform Makefile for cpdd
.POSIX:
.SUFFIXES:
CC = cc
CFLAGS = -std=c99 -Wall -Wextra -O2 -Iinclude -D_POSIX_C_SOURCE=200809L -Wno-deprecated-declarations -Wno-format-truncation

all: cpdd syndir
cpdd: src/cpdd/main.o src/cpdd/copy.o src/cpdd/matching.o src/cpdd/args.o src/common/terminal.o src/common/md5.o
	$(CC) $(CFLAGS) -o $@ main.o copy.o matching.o args.o terminal.o md5.o
syndir: main.o core.o args.o terminal.o
	$(CC) $(CFLAGS) -o $@ main.o core.o args.o terminal.o -lm

cpdd.o: src/cpdd/main.c
copy.o: src/cpdd/copy.c
matching.o: src/cpdd/matching.c
args.o: src/cpdd/args.c
terminal.o: src/common/terminal.c
md5.o: src/common/md5.c
main.o: src/syndir/main.c
core.o: src/syndir/core.c
args.o: src/syndir/args.c

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -rf obj cpdd syndir docs/*.txt *.o

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
