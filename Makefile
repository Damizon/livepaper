CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lX11 -lXrandr

SRC = src/main.c
OUT = livepaper

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

clean:
	rm -f $(OUT)
	rm -f build/*.o

run: all
	./$(OUT)

.PHONY: all clean run
