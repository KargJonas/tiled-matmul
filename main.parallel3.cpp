// main.parallel3.cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>
#include <taskflow/taskflow.hpp>  // Taskflow header

#define TILE_SIZE       64
#define BLOCK_SIZE      8
#define MICRO_TILE_SIZE 8
#define MEM_ALIGNMENT   64
#define N_CORES         12

#define ALIGN_UP_64(x) (((x) + TILE_SIZE - 1) / TILE_SIZE * TILE_SIZE)
#define ALIGN_UP_8(x)  (((x) + MICRO_TILE_SIZE - 1) / MICRO_TILE_SIZE * MICRO_TILE_SIZE)
#define ALIGN_UP(x)    ALIGN_UP_64(ALIGN_UP_8(x))

struct task_t {
  float *A, *B, *C;
  size_t stride_a, stride_b, stride_c;
  size_t n_k;
};

//-----------------------------------------------------------------------------
// 64×64 micro‐tile kernel (unchanged)
static inline __attribute__((always_inline))
void mm_tile(const task_t* t) {
  for(size_t i=0; i<TILE_SIZE; i+=MICRO_TILE_SIZE) {
    for(size_t j=0; j<TILE_SIZE; j+=BLOCK_SIZE) {
      __m256 c[8];
      for(int x=0; x<8; x++) c[x] = _mm256_setzero_ps();
      for(size_t k=0; k<t->n_k; k+=BLOCK_SIZE) {
        size_t kend = (k+BLOCK_SIZE <= t->n_k ? k+BLOCK_SIZE : t->n_k);
        for(size_t kk=k; kk<kend; ++kk) {
          __m256 b0 = _mm256_loadu_ps(&t->B[kk * t->stride_b + j]);
          for(int ii=0; ii<8; ++ii) {
            __m256 a = _mm256_set1_ps(t->A[(i+ii)*t->stride_a + kk]);
          #ifdef __FMA__
            c[ii] = _mm256_fmadd_ps(a, b0, c[ii]);
          #else
            c[ii] = _mm256_add_ps(c[ii], _mm256_mul_ps(a, b0));
          #endif
          }
        }
      }
      for(int ii=0; ii<8; ++ii) {
        _mm256_storeu_ps(&t->C[(i+ii)*t->stride_c + j], c[ii]);
      }
    }
  }
}

//-----------------------------------------------------------------------------
// Parallel mm() using explicit Taskflow tasks
void mm(float* A, float* B, float* C, size_t padm, size_t padn, size_t padp) {
  const size_t num_I = padm / TILE_SIZE;
  const size_t num_J = padp / TILE_SIZE;

  tf::Executor executor(N_CORES);
  tf::Taskflow taskflow;

  // One Taskflow task per tile-row I
  for(size_t I = 0; I < num_I; ++I) {
    taskflow.emplace([=](){
      size_t i0 = I * TILE_SIZE;
      for(size_t J = 0; J < num_J; ++J) {
        size_t j0 = J * TILE_SIZE;
        task_t t {
          .A        = A + i0 * padn,
          .B        = B + j0,
          .C        = C + i0 * padp + j0,
          .stride_a = padn,
          .stride_b = padp,
          .stride_c = padp,
          .n_k      = padn
        };
        mm_tile(&t);
      }
    });
  }

  // Run all tasks across N_CORES worker threads
  executor.run(taskflow).wait();
}

//-----------------------------------------------------------------------------
// Helpers
void fill_rand(float* arr, size_t sz, size_t mx) {
  for(size_t i = 0; i < sz; ++i) arr[i] = rand() % mx;
}

static inline float* pad_mat(float* src, size_t r, size_t c, size_t pr, size_t pc) {
  float* dst = (float*)aligned_alloc(MEM_ALIGNMENT, pr * pc * sizeof(float));
  memset(dst, 0, pr * pc * sizeof(float));
  for(size_t i = 0; i < r; ++i) {
    memcpy(dst + i * pc, src + i * c, c * sizeof(float));
  }
  return dst;
}

static inline void unpad_mat(float* src, float* dst,
                             size_t r, size_t c, size_t pr, size_t pc) {
  for(size_t i = 0; i < r; ++i) {
    memcpy(dst + i * c, src + i * pc, c * sizeof(float));
  }
}

int main(int argc, char* argv[]) {
  srand(time(NULL));
  if(argc < 4) {
    fprintf(stderr, "Usage: %s <m> <n> <p>\n", argv[0]);
    return 1;
  }
  size_t m = strtoul(argv[1], NULL, 10);
  size_t n = strtoul(argv[2], NULL, 10);
  size_t p = strtoul(argv[3], NULL, 10);

  const size_t padm = ALIGN_UP(m);
  const size_t padn = ALIGN_UP(n);
  const size_t padp = ALIGN_UP(p);

  float *A = (float*)malloc(m * n * sizeof(float));
  float *B = (float*)malloc(n * p * sizeof(float));
  float *C = (float*)malloc(m * p * sizeof(float));
  fill_rand(A, m*n, 10);
  fill_rand(B, n*p, 10);

  float *padA = pad_mat(A, m, n, padm, padn);
  float *padB = pad_mat(B, n, p, padn, padp);
  float *padC = (float*)aligned_alloc(MEM_ALIGNMENT, padm * padp * sizeof(float));
  memset(padC, 0, padm * padp * sizeof(float));

  double total = 0;
  const int runs = 4;
  for(int i = 0; i < runs; ++i) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    mm(padA, padB, padC, padm, padn, padp);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    total += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
  }
  printf("%.9f", total / runs);

  unpad_mat(padC, C, m, p, padm, padp);

  free(A); free(B); free(C);
  free(padA); free(padB); free(padC);
  return 0;
}
