CC=gcc
CFLAGS=-std=c11 -O2 -Wall
SRC=$(wildcard src/*.c)
BIN=bin/csimplified

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p bin
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) -lm

clean:
	rm -f bin/csimplified
