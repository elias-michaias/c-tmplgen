CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic
LDFLAGS ?=
LDLIBS ?= -lclang -pthread

SOURCES := $(sort $(wildcard src/*.c))
OBJECTS := $(patsubst src/%.c,build/tmplgen/%.o,$(SOURCES))

.PHONY: all clean test

all: tmplgen

tmplgen: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tmplgen/%.o: src/%.c src/internal.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: tmplgen
	sh test/run.sh

clean:
	rm -rf build/tmplgen tmplgen
