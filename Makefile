# O comando `make` produz somente o compilador final.

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c99 -Isrc
BUILD   := build
TARGET  := $(BUILD)/SAL

SOURCES := \
	src/main.c \
	src/parser.c \
	src/generator.c \
	src/diag.c \
	src/log.c \
	src/symtab.c \
	src/types.c \
	src/lex.c

OBJECTS := $(patsubst src/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all clean

all: $(TARGET)

# O link final combina todos os modulos compilados em um unico executavel.
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

# Cada .c e compilado isoladamente. A pasta build/ e criada sob demanda.
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
