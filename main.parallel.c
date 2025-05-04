#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

#define TILE_SIZE 64
#define ALIGN_UP(x) (((x) + TILE_SIZE - 1) / TILE_SIZE * TILE_SIZE)

void print_mat(float* mat, size_t m, size_t n);
void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p);
void fill_rand(float* arr, size_t size, size_t max);
static inline float* pad_mat(float* src, size_t srcr, size_t srcc, size_t padr, size_t padc);
static inline float* pad_t_mat(float* src, size_t srcr, size_t srcc, size_t padr, size_t padc);
static inline float* unpad_mat(float* src, float* dst, size_t r, size_t c, size_t padr, size_t padc);

// Allows a worker to locate a tile within A, B and C
typedef struct task_t {
    float *A, *B, *C;
    size_t i, j;
    size_t padm, padn, padp;
} task_t;

typedef struct queue_node_t {
    task_t* tile_task;
    struct queue_node_t *next;
} queue_node_t;

typedef struct queue_t {
    queue_node_t *head, *tail;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} queue_t;

void init_task_queue(queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

void enqueue(queue_t *q, task_t *task) {
    queue_node_t *node = malloc(sizeof(queue_node_t));
    node->tile_task = task;
    node->next = NULL;

    pthread_mutex_lock(&q->lock);

    if (q->tail) q->tail->next = node;
    else q->head = node;

    q->tail = node;

    pthread_cond_signal(&q->not_empty); //  Signal waiting threads
    pthread_mutex_unlock(&q->lock);
}


task_t* dequeue(queue_t *q) {
    pthread_mutex_lock(&q->lock);

    while (!q->head) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }

    queue_node_t *node = q->head;
    task_t *task = node->tile_task;

    q->head = node->next;
    if (!q->head) q->tail = NULL;

    pthread_mutex_unlock(&q->lock);
    free(node);
    return task;
}


void destroy_queue(queue_t *q) {
    pthread_mutex_lock(&q->lock);
    queue_node_t *curr = q->head;
    
    while (curr) {
        queue_node_t *tmp = curr;
        curr = curr->next;
        free(tmp->tile_task);
        free(tmp);
    }

    pthread_mutex_unlock(&q->lock);
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
}

int main() {
    srand(314);

    size_t m = 4;
    size_t n = 4;
    size_t p = 4;

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

    print_mat(A, m, n);
    print_mat(B, n, p);

    double avg = 0;
    int iterations = 1;
    for (int i = 0; i < iterations; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        // mm(A, B, C, m, n, p);
        mm(padA, padB, padC, padm, padn, padp);
        clock_gettime(CLOCK_MONOTONIC, &end);

        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        avg += elapsed;
    }

    avg /= iterations;

    printf("Time elapsed: %.9f seconds\n", avg);

    unpad_mat(padC, C, m, p, padm, padp);
    

    free(padA);
    free(padB);
    free(padC);

    print_mat(C, m, p);

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

// Pads a matrix up to the nearest multiple of TILE_SIZE
static inline float* pad_mat(float* src, size_t r, size_t c, size_t padr, size_t padc) {
    float* dst = calloc(padr * padc, sizeof(float));

    for (size_t i = 0; i < r; i++) {
        memcpy(dst + i * padc, src + i * c, c * sizeof(float));
    }

    return dst;
}

// Transposes a matrix and pads it up to the nearest multiple of TILE_SIZE
static inline float* pad_t_mat(float* src, size_t r, size_t c, size_t padr, size_t padc) {
    float* dst = calloc(padr * padc, sizeof(float));

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
