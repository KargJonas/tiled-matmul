CC       = gcc
CFLAGS   = -Ofast -funroll-loops -march=native -mtune=native -mfma -mavx2 -ffast-math -fopt-info-vec-optimized

SRCS     = $(wildcard *.c)
BINS     = $(SRCS:.c=)

all: $(BINS)
.DEFAULT_GOAL := all

%: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(BINS)

.PHONY: clean
