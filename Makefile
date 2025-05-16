CC      := gcc
CFLAGS  := -Ofast -funroll-loops -pthread -march=native -mtune=native -mfma -mavx2 -ffast-math -fopt-info-vec-optimized

OUTDIR  := bin

SRCS    := $(wildcard *.c) $(wildcard *.cpp)
BINS    := $(SRCS:%.c=$(OUTDIR)/%) $(SRCS:%.cpp=$(OUTDIR)/%)

.PHONY: all clean

all: | $(OUTDIR) $(BINS)

$(OUTDIR):
	mkdir -p $@

$(OUTDIR)/%: %.c
	$(CC) $(CFLAGS) $< -o $@

$(OUTDIR)/%: %.cpp
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(OUTDIR)

.DEFAULT_GOAL := all
