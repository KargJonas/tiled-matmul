CC      	:= gcc
CPP     	:= g++
CFLAGS  	:= -Ofast -funroll-loops -pthread -march=native -mtune=native -mfma -mavx2 -ffast-math -fopt-info-vec-optimized
CPPFLAGS	:= -Ofast -funroll-loops -pthread -march=native -mtune=native -mfma -mavx2 -ffast-math -fopt-info-vec-optimized -I taskflow/

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
	$(CPP) $(CPPFLAGS) $< -o $@

clean:
	rm -rf $(OUTDIR)

.DEFAULT_GOAL := all
