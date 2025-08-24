# Cross-platform Makefile for cpdd
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -Wno-deprecated-declarations -Wno-format-truncation
INCLUDES = -Iinclude
SRCDIR = src
OBJDIR = obj
TESTDIR = tests
MANDIR = man
TARGET = cpdd

# Platform-specific settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    CFLAGS += -D_GNU_SOURCE
endif

# CPDD source files
CPDD_SOURCES = $(wildcard $(SRCDIR)/cpdd/*.c)
COMMON_SOURCES = $(wildcard $(SRCDIR)/common/*.c)
CPDD_OBJECTS = $(CPDD_SOURCES:$(SRCDIR)/cpdd/%.c=$(OBJDIR)/cpdd/%.o) $(COMMON_SOURCES:$(SRCDIR)/common/%.c=$(OBJDIR)/common/%.o)

# Syndir source files
SYNDIR_SOURCES = $(wildcard $(SRCDIR)/syndir/*.c)
SYNDIR_OBJECTS = $(SYNDIR_SOURCES:$(SRCDIR)/syndir/%.c=$(OBJDIR)/syndir/%.o)
SYNDIR_TARGET = syndir

# Test files
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/%.o)
TEST_TARGETS = $(TEST_SOURCES:$(TESTDIR)/%.c=%)

.PHONY: all clean test install uninstall syndir docs

all: $(TARGET) $(SYNDIR_TARGET)

$(TARGET): $(CPDD_OBJECTS)
	$(CC) $^ -o $@

$(SYNDIR_TARGET): $(SYNDIR_OBJECTS)
	$(CC) $^ -lm -o $@

$(OBJDIR)/cpdd/%.o: $(SRCDIR)/cpdd/%.c | $(OBJDIR)/cpdd
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/syndir/%.o: $(SRCDIR)/syndir/%.c | $(OBJDIR)/syndir
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/common/%.o: $(SRCDIR)/common/%.c | $(OBJDIR)/common
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/cpdd:
	mkdir -p $(OBJDIR)/cpdd

$(OBJDIR)/syndir:
	mkdir -p $(OBJDIR)/syndir

$(OBJDIR)/common:
	mkdir -p $(OBJDIR)/common

test: $(TEST_TARGETS)
	@echo "Running tests..."
	@for test in $(TEST_TARGETS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done
	@echo "All tests passed!"

$(TEST_TARGETS): %: $(OBJDIR)/%.o $(filter-out $(OBJDIR)/cpdd/main.o,$(CPDD_OBJECTS))
	$(CC) $^ -o $@

install: $(TARGET) $(SYNDIR_TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -d $(DESTDIR)/usr/local/share/man/man1
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/
	install -m 755 $(SYNDIR_TARGET) $(DESTDIR)/usr/local/bin/
	install -m 644 $(MANDIR)/$(TARGET).1 $(DESTDIR)/usr/local/share/man/man1/
	install -m 644 $(MANDIR)/$(SYNDIR_TARGET).1 $(DESTDIR)/usr/local/share/man/man1/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/local/bin/$(SYNDIR_TARGET)
	rm -f $(DESTDIR)/usr/local/share/man/man1/$(TARGET).1
	rm -f $(DESTDIR)/usr/local/share/man/man1/$(SYNDIR_TARGET).1

clean:
	rm -rf $(OBJDIR) $(TARGET) $(SYNDIR_TARGET) $(TEST_TARGETS) *.txt

# Generate text versions of man pages for GitHub viewing
docs: $(TARGET).txt $(SYNDIR_TARGET).txt

%.txt: $(MANDIR)/%.1
	@if command -v man >/dev/null 2>&1; then \
		MANWIDTH=80 man -P cat ./$< > $@; \
	elif command -v groff >/dev/null 2>&1; then \
		groff -man -Tascii $< | col -bx > $@; \
	elif command -v nroff >/dev/null 2>&1; then \
		nroff -man $< | col -bx > $@; \
	else \
		echo "Warning: No man page formatter found. Install man, groff, or nroff to generate text documentation."; \
		echo "Fallback: copying raw file with .txt extension"; \
		cp $< $@; \
	fi

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all      - Build the main program and syndir (default)"
	@echo "  cpdd     - Build only the main program"
	@echo "  syndir   - Build only the synthetic directory generator"
	@echo "  test     - Build and run tests"
	@echo "  docs     - Generate text versions of man pages for GitHub"
	@echo "  install  - Install to /usr/local/bin and man pages"
	@echo "  uninstall- Remove from /usr/local/bin and man pages"
	@echo "  debug    - Build with debug symbols"
	@echo "  clean    - Remove build artifacts and generated docs"
	@echo "  help     - Show this help message"