CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?= -lsqlite3
PREFIX ?= /usr/local

SRC = src/main.c src/db.c src/vault.c src/recommend.c src/review.c
OBJ = $(SRC:.c=.o)
BIN = taste

.PHONY: all clean test format lint install

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c src/taste.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN) test.sqlite

format:
	clang-format -i src/*.c src/*.h

lint:
	$(CC) $(CFLAGS) -fsyntax-only $(SRC)

test: $(BIN)
	rm -f test.sqlite
	./$(BIN) --db test.sqlite pack add packs/shoegaze
	./$(BIN) --db test.sqlite recommend "Cocteau Twins" "Slowdive" "My Bloody Valentine" --mode deep-cut --limit 5
	./$(BIN) --db test.sqlite explain "Cocteau Twins" "Lush"
	./$(BIN) --db test.sqlite graph inspect "Slowdive" --json

install: $(BIN)
	install -m 0755 $(BIN) $(PREFIX)/bin/$(BIN)
