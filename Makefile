CC	 		= gcc
CFLAGS 		= -Ofast -funroll-loops -march=native -mtune=native -mfma -mavx2 -ffast-math -fopt-info-vec-optimized

VAL_FLAG	= VALIDATE

PARALLEL	= main.parallel
REFERENCE	= main

all: parallel reference
.DEFAULT_GOAL := all

parallel: $(PARALLEL)
reference: $(REFERENCE)

$(PARALLEL): $(PARALLEL).c
	$(CC) $(CFLAGS) $< -o $@

$(REFERENCE): $(REFERENCE).c
	$(CC) $(CFLAGS) $< -o $@

# validate:
# 	$(CC) $(CFLAGS) -D$(VAL_FLAG) $(PARALLEL).c -o $@
# 	python3 validate.py

clean:
	rm -rf $(PARALLEL) $(REFERENCE)

.PHONY: clean validate