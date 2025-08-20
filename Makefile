# Cross-platform Makefile for cpdd
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -Wno-deprecated-declarations -Wno-format-truncation
INCLUDES = -Iinclude
SRCDIR = src
OBJDIR = obj
TESTDIR = tests
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

# Test generator source files
TESTGEN_SOURCES = $(wildcard $(SRCDIR)/testgen/*.c)
TESTGEN_OBJECTS = $(TESTGEN_SOURCES:$(SRCDIR)/testgen/%.c=$(OBJDIR)/testgen/%.o)
TESTGEN_TARGET = testgen

# Test files
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/%.o)
TEST_TARGETS = $(TEST_SOURCES:$(TESTDIR)/%.c=%)

.PHONY: all clean test install uninstall testgen

all: $(TARGET) $(TESTGEN_TARGET)

$(TARGET): $(CPDD_OBJECTS)
	$(CC) $^ -o $@

$(TESTGEN_TARGET): $(TESTGEN_OBJECTS)
	$(CC) $^ -o $@

$(OBJDIR)/cpdd/%.o: $(SRCDIR)/cpdd/%.c | $(OBJDIR)/cpdd
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/testgen/%.o: $(SRCDIR)/testgen/%.c | $(OBJDIR)/testgen
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/common/%.o: $(SRCDIR)/common/%.c | $(OBJDIR)/common
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/cpdd:
	mkdir -p $(OBJDIR)/cpdd

$(OBJDIR)/testgen:
	mkdir -p $(OBJDIR)/testgen

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

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TESTGEN_TARGET) $(TEST_TARGETS)

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

testgen: $(TESTGEN_TARGET)

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all      - Build the main program and testgen (default)"
	@echo "  cpdd     - Build only the main program"
	@echo "  testgen  - Build only the test data generator"
	@echo "  test     - Build and run tests"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  uninstall- Remove from /usr/local/bin"
	@echo "  debug    - Build with debug symbols"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this help message"