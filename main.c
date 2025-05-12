#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#define TILE_SIZE 64
#define ALIGN_UP(x) (((x) + TILE_SIZE - 1) / TILE_SIZE * TILE_SIZE)

void print_mat(float* mat, size_t m, size_t n);
void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p);
void fill_rand(float* arr, size_t size, size_t max);
static inline float* pad_mat(float* src, size_t srcr, size_t srcc, size_t padr, size_t padc);
static inline float* pad_t_mat(float* src, size_t srcr, size_t srcc, size_t padr, size_t padc);
static inline float* unpad_mat(float* src, float* dst, size_t r, size_t c, size_t padr, size_t padc);

int parse_int(const char *str) {
    char *end;
    long val = strtol(str, &end, 10);
    if (*end || str == end) {
        fprintf(stderr, "Invalid integer: '%s'\n", str);
        exit(1);
    }
    return (int)val;
}

int main(int argc, char* argv[]) {
    srand(314);
    srand(time(NULL));

    if (argc < 4) {
        fprintf(stderr, "Error: Expected 3 arguments.");
        return 1;
    }

    int validate = argc == 4;

    size_t m = parse_int(argv[1]);
    size_t n = parse_int(argv[2]);
    size_t p = parse_int(argv[3]);

    float* A = malloc(m * n * sizeof(float));
    float* B = malloc(n * p * sizeof(float));
    float* C = malloc(m * p * sizeof(float));

    fill_rand(A, m * n, 10);
    fill_rand(B, n * p, 10);

    // The size of the "inner matrix", ie all tiles that do not touch a padded edge 
    const size_t padm = ALIGN_UP(m);
    const size_t padn = ALIGN_UP(n);
    const size_t padp = ALIGN_UP(p);

    // Padding A; Transposing and padding B
    float* padA = pad_mat(A, m, n, padm, padn);
    float* padB = pad_t_mat(B, n, p, padn, padp);
    float* padC = calloc(padm * padp, sizeof(float));

    // print_mat(A, m, n);
    // print_mat(B, n, p);

    if (validate) {
        print_mat(A, m, n);
        print_mat(B, n, p);
    }

    double avg = 0;
    int iterations = 1;
    for (int i = 0; i < iterations; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        mm(padA, padB, padC, padm, padn, padp);
        clock_gettime(CLOCK_MONOTONIC, &end);

        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        avg += elapsed;
    }
 
    avg /= iterations;

    if (!validate) {
        printf("%.9f", avg);
    }

    unpad_mat(padC, C, m, p, padm, padp);
    
    free(padA);
    free(padB);
    free(padC);

    if (validate) {
        print_mat(C, m, p);
    }

    free(A);
    free(B);
    free(C);

    return 0;
}

// Performs matrix multiplication on a single fixed-size tile
// Because we use constants for the loop limits, this entire
// function can be completely unrolled and thoroughly optimized
// A, B and C are references to the beginning of the current tile.
// The strides tell us how many elements to step over to get to
// the next row in that tile.
static inline __attribute__((always_inline)) void mm_tile(
    float* __restrict__ A,
    float* __restrict__ B,
    float* __restrict__ C,
    size_t stride_a, size_t stride_b, size_t stride_c
) {
    for (size_t i = 0; i < TILE_SIZE; i++) {
        size_t ic = i * stride_c;
        size_t ia = i * stride_a;

        for (size_t j = 0; j < TILE_SIZE; j++) {
            size_t ic_j = ic + j;
            size_t ib_j = j * stride_b;

            for (size_t k = 0; k < TILE_SIZE; k++) {
                C[ic_j] += A[ia + k] * B[ib_j + k];
            }
        }
    }
}

void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p) {
    // For testing, the padding stuff is still in mm but it should
    // be moved outside of this function later, because padding and
    // transposition should happen only once.

    // The size of the "inner matrix", ie all tiles that do not touch a padded edge 
    const size_t padm = ALIGN_UP(m);
    const size_t padn = ALIGN_UP(n);
    const size_t padp = ALIGN_UP(p);

    // Iterate over all inner tiles
    for (size_t i = 0; i < padm; i += TILE_SIZE) {
        for (size_t j = 0; j < padp; j += TILE_SIZE) {
            const size_t ic = i * padp + j;

            for (size_t k = 0; k < padn; k += TILE_SIZE) {
                const size_t ia = i * padn + k;
                const size_t ib = j * padn + k;

                // The strides correspond to the number of columns in the respective matrices
                mm_tile(A + ia, B + ib, C + ic, padm, padp, padp);
            }
        }
    }
}

// Using aligned memory improved performance by a factor of 20
void* aligned_calloc(size_t alignment, size_t num, size_t size) {
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, num * size) != 0) return NULL;
    return memset(ptr, 0, num * size);
}

// Pads a matrix up to the nearest multiple of TILE_SIZE
static inline float* pad_mat(float* src, size_t r, size_t c, size_t padr, size_t padc) {
    float* dst = aligned_calloc(64, padr * padc, sizeof(float));
    for (size_t i = 0; i < r; i++) {
        memcpy(dst + i * padc, src + i * c, c * sizeof(float));
    }

    return dst;
}

// Transposes a matrix and pads it up to the nearest multiple of TILE_SIZE
static inline float* pad_t_mat(float* src, size_t r, size_t c, size_t padr, size_t padc) {
    float* dst = aligned_calloc(64, padr * padc, sizeof(float));

    // Hopefully the compiler will do some heavy optimization here
    for (size_t i = 0; i < r; i++) {
        for (size_t j = 0; j < c; j++) {
            dst[j * padc + i] = src[i * c + j];
        }
    }

    return dst;
}

static inline float* unpad_mat(float* src, float* dst, size_t r, size_t c, size_t padr, size_t padc) {
  for (size_t i = 0; i < r; i++) {
    memcpy(dst + i * c, src + i * padc, c * sizeof(float));
  }
}

void fill_rand(float* arr, size_t size, size_t max) {
    for (size_t i = 0; i < size; i++) arr[i] = rand() % max;
}

void print_mat(float* mat, size_t m, size_t n) {
    for (size_t i = 0; i < m; i++) {
        for (size_t j = 0; j < n; j++) {
            printf("%6.2f%s", mat[i * n + j], j == n - 1 ? "" : ", ");
        }
        putchar('\n');
    }
    printf("---\n\n");
}
