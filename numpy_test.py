import numpy as np
import time

a = np.random.rand(512, 512)
b = np.random.rand(512, 512)

# Warm-up
for _ in range(10):
    np.dot(a, b)

# Timing repeated ops
start = time.perf_counter()
for _ in range(1000):
    c = np.dot(a, b)
end = time.perf_counter()

# Use the result to prevent lazy eval
_ = c[0, 0]

print(f"Average time: {(end - start)/1000:.6f} seconds")