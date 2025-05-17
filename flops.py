import subprocess
import sys


a = sys.argv[1]
n = int(sys.argv[2])

def run_exec(exe, m, n, p):
    """Run the executable with dimensions and capture its output."""
    cmd = [f"./{exe}", str(m), str(n), str(p), "perf"]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return float(cp.stdout)

time = run_exec(a, n, n, n)
gflops = ((2 * n ** 3) / time) / 10 ** 9
print(f"{gflops} GFLOPS @ n={n}")