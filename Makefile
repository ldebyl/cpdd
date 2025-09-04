# Cross-platform Makefile for cpdd
.POSIX:
.OBJDIR: .
CC = cc
CFLAGS = -std=c99 -Wall -Wextra -O2 -Iinclude -D_POSIX_C_SOURCE=200809L -Wno-deprecated-declarations -Wno-format-truncation

# Macro to compile source to object
COMPILE = mkdir -p obj && $(CC) $(CFLAGS) -c

all: cpdd syndir docs

cpdd: obj/cpdd/cpdd.o obj/cpdd/copy.o obj/cpdd/matching.o obj/cpdd/args.o obj/common/terminal.o obj/common/md5.o
	$(CC) $(CFLAGS) -o cpdd obj/cpdd/cpdd.o obj/cpdd/copy.o obj/cpdd/matching.o obj/cpdd/args.o obj/common/terminal.o obj/common/md5.o

syndir: obj/syndir/syndir.o obj/syndir/core.o obj/syndir/args.o obj/common/terminal.o
	$(CC) $(CFLAGS) -o syndir obj/syndir/syndir.o obj/syndir/core.o obj/syndir/args.o obj/common/terminal.o -lm

debug: CFLAGS += -DDEBUG -g -O0 -fsanitize=address -fno-omit-frame-pointer
debug: clean cpdd syndir

static:
	@if uname | grep -q Darwin; then \
		echo "macOS detected - building with dynamic linking (static not supported)"; \
		$(MAKE) static-macos; \
	else \
		echo "Linux detected - building with static linking"; \
		$(MAKE) static-linux; \
	fi

static-linux: CFLAGS += -static
static-linux: clean cpdd syndir

static-macos: clean cpdd syndir

test: cpdd syndir
	@echo "Running cpdd test suite..."
	./test_cpdd.sh

obj/cpdd/cpdd.o: src/cpdd/cpdd.c
	mkdir -p obj/cpdd && $(CC) $(CFLAGS) -c src/cpdd/cpdd.c -o obj/cpdd/cpdd.o
obj/cpdd/copy.o: src/cpdd/copy.c
	mkdir -p obj/cpdd && $(CC) $(CFLAGS) -c src/cpdd/copy.c -o obj/cpdd/copy.o
obj/cpdd/matching.o: src/cpdd/matching.c
	mkdir -p obj/cpdd && $(CC) $(CFLAGS) -c src/cpdd/matching.c -o obj/cpdd/matching.o
obj/cpdd/args.o: src/cpdd/args.c
	mkdir -p obj/cpdd && $(CC) $(CFLAGS) -c src/cpdd/args.c -o obj/cpdd/args.o
obj/common/terminal.o: src/common/terminal.c
	mkdir -p obj/common && $(CC) $(CFLAGS) -c src/common/terminal.c -o obj/common/terminal.o
obj/common/md5.o: src/common/md5.c
	mkdir -p obj/common && $(CC) $(CFLAGS) -c src/common/md5.c -o obj/common/md5.o
obj/syndir/syndir.o: src/syndir/syndir.c
	mkdir -p obj/syndir && $(CC) $(CFLAGS) -c src/syndir/syndir.c -o obj/syndir/syndir.o
obj/syndir/core.o: src/syndir/core.c
	mkdir -p obj/syndir && $(CC) $(CFLAGS) -c src/syndir/core.c -o obj/syndir/core.o
obj/syndir/args.o: src/syndir/args.c
	mkdir -p obj/syndir && $(CC) $(CFLAGS) -c src/syndir/args.c -o obj/syndir/args.o


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

docs/cpdd.txt: man/cpdd.1
	./scripts/man2txt.sh man/cpdd.1 docs/cpdd.txt

docs/syndir.txt: man/syndir.1
	./scripts/man2txt.sh man/syndir.1 docs/syndir.txt

.PHONY: help test debug static static-linux static-macos
help:
	@echo "Available targets:"
	@echo "  all      - Build the main program and syndir (default)"
	@echo "  cpdd     - Build only the main program"
	@echo "  syndir   - Build only the synthetic directory generator"
	@echo "  debug    - Build with debug symbols and AddressSanitizer"
	@echo "  static   - Build statically linked binaries for distribution"
	@echo "  test     - Run the cpdd test suite"
	@echo "  docs     - Generate text versions of man pages for GitHub"
	@echo "  install  - Install to /usr/local/bin and man pages"
	@echo "  uninstall- Remove from /usr/local/bin and man pages"
	@echo "  clean    - Remove build artifacts and generated docs"
	@echo "  help     - Show this help message"
