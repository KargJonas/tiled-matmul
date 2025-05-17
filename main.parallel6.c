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

#define TILE_SIZE       64
#define BLOCK_SIZE      8
#define MICRO_TILE_SIZE 8
#define K_STEP          1024
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
static inline void unpad_mat(float* src, float* dst, size_t r, size_t c, size_t padr, size_t padc);

// This function runs in each thread, listens for shutdown commands,
// and executes tasks from the task queue.
void* worker_thread(void* arg) {
    threadpool_t *pool = (threadpool_t *)arg;
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

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
            break;
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

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1e6;
    printf("Thread done after %f ms\n", elapsed);

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
    
    pool->threads = (thread_info_t*)malloc(num_threads * sizeof(thread_info_t));
    
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

    pthread_mutex_lock(&pool->lock);
    
    pthread_mutex_lock(&pool->completion_lock);
    pool->tasks_remaining++;
    pthread_mutex_unlock(&pool->completion_lock);

    if (pool->tail) pool->tail->next = node;
    else pool->head = node;

    pool->tail = node;

    // Signal waiting threads
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
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
    
    // Wake up all worker threads
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);
    
    // Join all threads
    for (size_t i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i].thread, NULL);
    }
    
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
    // srand(314);
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

    // Padding A and B, allocating C
    float* padA = pad_mat(A, m, n, padm, padn);
    float* padB = pad_mat(B, n, p, padn, padp);
    float* padC = calloc(padm * padp, sizeof(float));

    if (validate) {
        print_mat(A, m, n);
        print_mat(B, n, p);
    }

    threadpool_t *pool = malloc(sizeof(threadpool_t));
    init_thread_pool(pool, N_CORES);

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

    destroy_thread_pool(pool);
    free(pool);

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

// Multiplies a single 64x64 tile.
// Each 64x64 tile is broken up into 64 individual micro tiles to properly use the L1 cache.
static inline __attribute__((always_inline)) void mm_tile(task_t* task) {

    // Micro-tiling loop
    for (size_t i = 0; i < TILE_SIZE; i += MICRO_TILE_SIZE) {
        for (size_t j = 0; j < TILE_SIZE; j += BLOCK_SIZE) {
            // Initialize accumulators
            __m256 c00 = _mm256_setzero_ps();
            __m256 c10 = _mm256_setzero_ps();
            __m256 c20 = _mm256_setzero_ps();
            __m256 c30 = _mm256_setzero_ps();
            __m256 c40 = _mm256_setzero_ps();
            __m256 c50 = _mm256_setzero_ps();
            __m256 c60 = _mm256_setzero_ps();
            __m256 c70 = _mm256_setzero_ps();
            
            // Chunked processing of k in hopes of improving cache locality
            for (size_t k = 0; k < task->n_k; k += BLOCK_SIZE) {
                size_t k_end = k + BLOCK_SIZE <= task->n_k ? k + BLOCK_SIZE : task->n_k;
                
                // Process one 8x8 micro-tile
                for (size_t kk = k; kk < k_end; kk++) {
                    __m256 b0 = _mm256_loadu_ps(&task->B[kk * task->stride_b + j]);
                    
                    // Load and broadcast A values one by one (scalar to vector)
                    __m256 a0 = _mm256_set1_ps(task->A[(i+0) * task->stride_a + kk]);
                    __m256 a1 = _mm256_set1_ps(task->A[(i+1) * task->stride_a + kk]);
                    __m256 a2 = _mm256_set1_ps(task->A[(i+2) * task->stride_a + kk]);
                    __m256 a3 = _mm256_set1_ps(task->A[(i+3) * task->stride_a + kk]);
                    __m256 a4 = _mm256_set1_ps(task->A[(i+4) * task->stride_a + kk]);
                    __m256 a5 = _mm256_set1_ps(task->A[(i+5) * task->stride_a + kk]);
                    __m256 a6 = _mm256_set1_ps(task->A[(i+6) * task->stride_a + kk]);
                    __m256 a7 = _mm256_set1_ps(task->A[(i+7) * task->stride_a + kk]);
                    
                    // Multiply and accumulate using FMA if available
                    #ifdef __FMA__
                    c00 = _mm256_fmadd_ps(a0, b0, c00);
                    c10 = _mm256_fmadd_ps(a1, b0, c10);
                    c20 = _mm256_fmadd_ps(a2, b0, c20);
                    c30 = _mm256_fmadd_ps(a3, b0, c30);
                    c40 = _mm256_fmadd_ps(a4, b0, c40);
                    c50 = _mm256_fmadd_ps(a5, b0, c50);
                    c60 = _mm256_fmadd_ps(a6, b0, c60);
                    c70 = _mm256_fmadd_ps(a7, b0, c70);
                    #else
                    c00 = _mm256_add_ps(c00, _mm256_mul_ps(a0, b0));
                    c10 = _mm256_add_ps(c10, _mm256_mul_ps(a1, b0));
                    c20 = _mm256_add_ps(c20, _mm256_mul_ps(a2, b0));
                    c30 = _mm256_add_ps(c30, _mm256_mul_ps(a3, b0));
                    c40 = _mm256_add_ps(c40, _mm256_mul_ps(a4, b0));
                    c50 = _mm256_add_ps(c50, _mm256_mul_ps(a5, b0));
                    c60 = _mm256_add_ps(c60, _mm256_mul_ps(a6, b0));
                    c70 = _mm256_add_ps(c70, _mm256_mul_ps(a7, b0));
                    #endif
                }
            }
            
            // Store results back to C
            _mm256_storeu_ps(&task->C[(i+0) * task->stride_c + j], c00);
            _mm256_storeu_ps(&task->C[(i+1) * task->stride_c + j], c10);
            _mm256_storeu_ps(&task->C[(i+2) * task->stride_c + j], c20);
            _mm256_storeu_ps(&task->C[(i+3) * task->stride_c + j], c30);
            _mm256_storeu_ps(&task->C[(i+4) * task->stride_c + j], c40);
            _mm256_storeu_ps(&task->C[(i+5) * task->stride_c + j], c50);
            _mm256_storeu_ps(&task->C[(i+6) * task->stride_c + j], c60);
            _mm256_storeu_ps(&task->C[(i+7) * task->stride_c + j], c70);
        }
    }
}

void mm(float *A, float *B, float *C, size_t m, size_t n, size_t p, threadpool_t *threadpool) {
    const size_t padm = ALIGN_UP(m);
    const size_t padn = ALIGN_UP(n);
    const size_t padp = ALIGN_UP(p);

    for (size_t kk = 0; kk < padn; kk += K_STEP) {
        size_t cur_k = (kk + K_STEP <= padn) ? K_STEP : (padn - kk);

        for (size_t i = 0; i < padm; i += TILE_SIZE) {
            for (size_t j = 0; j < padp; j += TILE_SIZE) {

                task_t *task = malloc(sizeof *task);
                *task = (task_t){
                    .A = A + i * padn + kk,
                    .B = B + kk * padp + j,
                    .C = C + i * padp + j,

                    .stride_a = padn,
                    .stride_b = padp,
                    .stride_c = padp,
                    .n_k      = cur_k
                };

                enqueue(threadpool, task);
            }
        }

        wait_for_completion(threadpool);
    }
}

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
