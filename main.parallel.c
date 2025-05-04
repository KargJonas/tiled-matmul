#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sched.h>
#include <immintrin.h>

#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

#define VALIDATE

#define TILE_SIZE       64
#define MICRO_TILE_SIZE 8
#define MEM_ALIGNMENT   64
#define N_CORES         12

#define ALIGN_UP_64(x) (((x) + TILE_SIZE - 1) / TILE_SIZE * TILE_SIZE)
#define ALIGN_UP_8(x)  (((x) + MICRO_TILE_SIZE - 1) / MICRO_TILE_SIZE * MICRO_TILE_SIZE)
#define ALIGN_UP(x)    ALIGN_UP_64(ALIGN_UP_8(x))

// Allows a worker to locate a tile within A, B and C
typedef struct task_t {
    float *A, *B, *C;
    size_t stride_a, stride_b, stride_c;
    size_t n_k; // k size in innermost loop
} task_t;

typedef struct queue_node_t {
    task_t* tile_task;
    struct queue_node_t *next;
} queue_node_t;

typedef struct {
    void *(*function)(void *);  // Worker thread function
    void *arg;                  // Argument to the function
    int active;                 // Is this thread active?
    pthread_t thread;           // Thread ID
} thread_info_t;

typedef struct threadpool_t {
    queue_node_t *head, *tail;          // Task queue
    pthread_mutex_t lock;               // Mutex for queue access
    pthread_cond_t not_empty;           // Condition variable for queue not empty
    pthread_cond_t not_full;            // Condition variable for queue not full
    thread_info_t *threads;             // Array of thread information
    size_t num_threads;                 // Number of threads in the pool
    int shutdown;                       // Flag to indicate shutdown
    int tasks_remaining;                // Counter for remaining tasks
    pthread_mutex_t completion_lock;    // Lock for completion status
    pthread_cond_t all_tasks_done;      // Condition variable for task completion
} threadpool_t;

void print_mat(float* mat, size_t m, size_t n);
void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p, threadpool_t* threadpool);
static inline __attribute__((always_inline)) void mm_tile(task_t* task);
void fill_rand(float* arr, size_t size, size_t max);
static inline float* pad_mat(float* src, size_t srcr, size_t srcc, size_t padr, size_t padc);
static inline float* pad_t_mat(float* src, size_t srcr, size_t srcc, size_t padr, size_t padc);
static inline void unpad_mat(float* src, float* dst, size_t r, size_t c, size_t padr, size_t padc);

// This function runs in each thread, listens for shutdown commands,
// and executes tasks from the task queue.
void* worker_thread(void* arg) {
    threadpool_t *pool = (threadpool_t *)arg;
    
    while (1) {
        task_t *task = NULL;
        
        pthread_mutex_lock(&pool->lock);
        
        // Wait until there's a task or shutdown is signaled
        while (!pool->head && !pool->shutdown) {
            pthread_cond_wait(&pool->not_empty, &pool->lock);
        }
        
        // Check if we need to shutdown
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }
        
        // Dequeue a task
        queue_node_t *node = pool->head;
        task = node->tile_task;
        pool->head = node->next;
        if (!pool->head) pool->tail = NULL;
        
        // Signal that the queue is not full
        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->lock);
        free(node);
        
        // Execute the task
        mm_tile(task);
        free(task);        
        // Decrement the task counter and signal if all tasks are done
        pthread_mutex_lock(&pool->completion_lock);
        pool->tasks_remaining--;
        
        if (pool->tasks_remaining == 0) {
            pthread_cond_broadcast(&pool->all_tasks_done);
        }
        pthread_mutex_unlock(&pool->completion_lock);
    }
    
    return NULL;
}

void wait_for_completion(threadpool_t *pool) {
    pthread_mutex_lock(&pool->completion_lock);
    while (pool->tasks_remaining > 0) {
        pthread_cond_wait(&pool->all_tasks_done, &pool->completion_lock);
    }
    pthread_mutex_unlock(&pool->completion_lock);
}

void init_thread_pool(threadpool_t *pool, size_t num_threads) {
    pool->head = NULL;
    pool->tail = NULL;
    pool->shutdown = 0;
    pool->num_threads = num_threads;
    pool->tasks_remaining = 0;
    
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->not_empty, NULL);
    pthread_cond_init(&pool->not_full, NULL);
    pthread_mutex_init(&pool->completion_lock, NULL);
    pthread_cond_init(&pool->all_tasks_done, NULL);
    
    pool->threads = (thread_info_t *)malloc(num_threads * sizeof(thread_info_t));
    
    // Create threads and pin each one to a specific core.
    for (size_t i = 0; i < num_threads; i++) {
        pool->threads[i].active = 1;
        pthread_create(&pool->threads[i].thread, NULL, worker_thread, pool);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        int err = pthread_setaffinity_np(
            pool->threads[i].thread,
            sizeof(cpu_set_t),
            &cpuset
        );
        if (err != 0) {
            fprintf(stderr, "Warning: failed to pin thread %zu to core %zu: %s\n", i, i, strerror(err));
        }
    }
}

void enqueue(threadpool_t *pool, task_t *task) {
    queue_node_t *node = malloc(sizeof(queue_node_t));
    node->tile_task = task;
    node->next = NULL;

    pthread_mutex_lock(&pool->completion_lock);
    
    pool->tasks_remaining++;

    if (pool->tail) pool->tail->next = node;
    else pool->head = node;

    pool->tail = node;

    pthread_cond_signal(&pool->not_empty); // Signal waiting threads
    pthread_mutex_unlock(&pool->completion_lock);
}

task_t* dequeue(threadpool_t *pool) {
    pthread_mutex_lock(&pool->lock);

    while (!pool->head) {
        pthread_cond_wait(&pool->not_empty, &pool->lock);
    }

    queue_node_t *node = pool->head;
    task_t *task = node->tile_task;

    pool->head = node->next;
    if (!pool->head) pool->tail = NULL;

    pthread_mutex_unlock(&pool->lock);
    free(node);
    return task;
}

void destroy_thread_pool(threadpool_t *pool) {
    // Signal shutdown
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty); // Wake up all worker threads
    pthread_mutex_unlock(&pool->lock);
    
    // Join all threads
    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i].thread, NULL);
    }
    
    // Free thread info array
    free(pool->threads);
    
    // Clean up any remaining nodes in the queue
    pthread_mutex_lock(&pool->lock);
    queue_node_t *curr = pool->head;

    while (curr) {
        queue_node_t *tmp = curr;
        curr = curr->next;
        free(tmp->tile_task);
        free(tmp);
    }

    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    pthread_mutex_destroy(&pool->completion_lock);
    pthread_cond_destroy(&pool->all_tasks_done);
}

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
    // srand(time(NULL));

    if (argc != 4) {
        fprintf(stderr, "Error: Expected 3 arguments.");
        return 1;
    }

    size_t m = parse_int(argv[1]);
    size_t n = parse_int(argv[2]);
    size_t p = parse_int(argv[3]);

    threadpool_t *pool = malloc(sizeof(threadpool_t));
    init_thread_pool(pool, N_CORES);

    float* A = malloc(m * n * sizeof(float));
    float* B = malloc(n * p * sizeof(float));
    float* C = malloc(m * p * sizeof(float));

    fill_rand(A, m * n, 10);
    fill_rand(B, n * p, 10);

    // The size of the "inner matrix", ie all tiles that do not touch a padded edge 
    const size_t padm = ALIGN_UP(m);
    const size_t padn = ALIGN_UP(n);
    const size_t padp = ALIGN_UP(p);

    // int needs_padding = padm != m || padn != n || padp != p;

    // Padding A; Transposing and padding B
    float* padA = pad_mat(A, m, n, padm, padn);
    float* padB = pad_t_mat(B, n, p, padn, padp);
    float* padC = calloc(padm * padp, sizeof(float));

    #ifdef VALIDATE
    print_mat(A, m, n);
    print_mat(B, n, p);
    #endif

    double avg = 0;
    int iterations = 1;
    for (int i = 0; i < iterations; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        mm(padA, padB, padC, padm, padn, padp, pool);
        clock_gettime(CLOCK_MONOTONIC, &end);

        double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        avg += elapsed;
    }

    avg /= iterations;

    #ifndef VALIDATE
    // printf("Time elapsed: %.9f seconds\n", avg);
    printf("%.9f", avg);
    #endif

    unpad_mat(padC, C, m, p, padm, padp);
    
    free(padA);
    free(padB);
    free(padC);

    #ifdef VALIDATE
    print_mat(C, m, p);
    #endif

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
static inline __attribute__((always_inline)) void mm_tile(task_t* task) {
    for (size_t ti = 0; ti < TILE_SIZE; ++ti) {
        size_t offA = ti * task->stride_a;
        size_t offC = ti * task->stride_c;

        for (size_t tj = 0; tj < TILE_SIZE; ++tj) {
            float sum = 0.0f;

            for (size_t k = 0; k < task->n_k; ++k) {
                sum += task->A[offA + k]
                     * task->B[tj * task->stride_b + k];
            }

            task->C[offC + tj] = sum;
        }
    }
}

void mm(float* A, float* B, float* C, size_t m, size_t n, size_t p, threadpool_t* threadpool) {
    // For testing, the padding stuff is still in mm but it should
    // be moved outside of this function later, because padding and
    // transposition should happen only once.

    // The size of the "inner matrix", ie all tiles that do not touch a padded edge 
    const size_t padm = ALIGN_UP(m);
    const size_t padn = ALIGN_UP(n);
    const size_t padp = ALIGN_UP(p);

    // Enqueue one task per (i,j) tile
    for (size_t i = 0; i < padm; i += TILE_SIZE) {
        for (size_t j = 0; j < padp; j += TILE_SIZE) {
            task_t *task = malloc(sizeof *task);

            *task = (task_t){
                .A = A + i * padn,
                .B = B + j * padn,
                .C = C + i * padp + j,
                .stride_a = padn,
                .stride_b = padn,
                .stride_c = padp,
                .n_k      = padn
            };

            enqueue(threadpool, task);
        }
    }

    wait_for_completion(threadpool);
}

// Using aligned memory improved performance by a factor of 20
void* aligned_calloc(size_t alignment, size_t num, size_t size) {
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, num * size) != 0) return NULL;
    return memset(ptr, 0, num * size);
}

// Pads a matrix up to the nearest multiple of TILE_SIZE
static inline float* pad_mat(float* src, size_t r, size_t c, size_t padr, size_t padc) {
    float* dst = aligned_calloc(MEM_ALIGNMENT, padr * padc, sizeof(float));
    for (size_t i = 0; i < r; i++) {
        memcpy(dst + i * padc, src + i * c, c * sizeof(float));
    }

    return dst;
}

// Transposes a matrix and pads it up to the nearest multiple of TILE_SIZE
static inline float* pad_t_mat(float* src, size_t r, size_t c, size_t padr, size_t padc) {
    float* dst = aligned_calloc(MEM_ALIGNMENT, padr * padc, sizeof(float));

    // Hopefully the compiler will do some heavy optimization here
    for (size_t i = 0; i < r; i++) {
        for (size_t j = 0; j < c; j++) {
            dst[j * padc + i] = src[i * c + j];
        }
    }

    return dst;
}

static inline void unpad_mat(float* src, float* dst, size_t r, size_t c, size_t padr, size_t padc) {
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
