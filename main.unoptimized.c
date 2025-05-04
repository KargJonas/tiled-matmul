#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TILE_SIZE 64

void print_mat(float* mat, size_t m, size_t n);
void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p);
void fill_rand(float* arr, size_t size, size_t max);

int main() {
    srand(314);

    size_t m = 3000;
    size_t n = 3000;
    size_t p = 3000;

    float* A = malloc(m * n * sizeof(float));
    float* B = malloc(n * p * sizeof(float));
    float* C = malloc(m * p * sizeof(float));

    fill_rand(A, m * n, 10);
    fill_rand(B, n * p, 10);

    // print_mat(A, m, n);
    // print_mat(B, n, p);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    mm(A, B, C, m, n, p);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("Time elapsed: %.9f seconds\n", elapsed);

    // print_mat(C, m, p);

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
static inline void mm_tile(
    float* A,
    float* B,
    float* C,
    size_t stride_a, size_t stride_b, size_t stride_c
) {
    for (size_t i = 0; i < TILE_SIZE; i++) {
        for (size_t j = 0; j < TILE_SIZE; j++) {
            const size_t ic = i * stride_c + j;
            for (size_t k = 0; k < TILE_SIZE; k++) {
                const size_t ia = i * stride_a + k;
                const size_t ib = k * stride_b + j;

                C[ic] += A[ia] * B[ib];
            }
        }
    }
}

// Handles edge cases where tile dimensions might be smaller than TILE_SIZE
static inline void mm_edge(
    float* A,
    float* B,
    float* C,
    size_t stride_a, size_t stride_b, size_t stride_c,
    size_t tile_m, size_t tile_n, size_t tile_p
) {
    for (size_t i = 0; i < tile_m; i++) {
        for (size_t j = 0; j < tile_p; j++) {
            const size_t ic = i * stride_c + j;
            for (size_t k = 0; k < tile_n; k++) {
                const size_t ia = i * stride_a + k;
                const size_t ib = k * stride_b + j;
                C[ic] += A[ia] * B[ib];
            }
        }
    }
}

void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p) {
    // The size of the "inner matrix", ie all tiles that do not touch a padded edge 
    const size_t inm = m - m % TILE_SIZE;
    const size_t inn = n - n % TILE_SIZE;
    const size_t inp = p - p % TILE_SIZE;

    // Fill C with zeros
    memset(C, 0, m * p * sizeof(float));

    // The goal here is to *implicitly* pad the matrices with zeros
    // while avoiding branching and while avoiding unnecessary data movement.

    // The inner tile multiplications are done in a separate function from
    // the edge tiles because that allows us to optimize for the (majority)
    // case where the size of the tile is exactly 8x8 with loop unrolling etc

    // Iterate over all inner tiles
    for (size_t i = 0; i < m; i += TILE_SIZE) {
        for (size_t j = 0; j < p; j += TILE_SIZE) {
            const size_t ic = i * p + j;

            for (size_t k = 0; k < n; k += TILE_SIZE) {
                const size_t ia = i * n + k;
                const size_t ib = k * p + j;

                // The strides correspond to the number of columns in the
                // respective matrices
                if (i < inm && j < inp) mm_tile(A + ia, B + ib, C + ic, n, p, p);
                else {
                    // Edge case - use mm_edge with actual tile dimensions
                    const size_t tile_m = (i + TILE_SIZE > m) ? (m - i) : TILE_SIZE;
                    const size_t tile_n = (k + TILE_SIZE > n) ? (n - k) : TILE_SIZE;
                    const size_t tile_p = (j + TILE_SIZE > p) ? (p - j) : TILE_SIZE;
                    mm_edge(A + ia, B + ib, C + ic, n, p, p, tile_m, tile_n, tile_p);
                }
            }
        }
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
