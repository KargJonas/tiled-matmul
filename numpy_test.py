import os
os.environ["OMP_NUM_THREADS"] = "10"

import numpy as np
import time
import sys


i = 16
n = int(sys.argv[1])
a = np.random.rand(n, n)
b = np.random.rand(n, n)

# Timing repeated ops
start = time.perf_counter()

for _ in range(i):
    c = np.dot(a, b)

end = time.perf_counter()

# Use the result to prevent lazy eval
_ = c[0, 0]

t = (end - start) / i
gflops = ((2 * n**3) / t) / 10**9

print(f"Average time: {t:.6f} seconds")
print(f"{gflops} GFLOPS @ n={n}")