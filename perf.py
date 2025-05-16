import subprocess
import sys


a = sys.argv[1]
b = sys.argv[2]

def run_exec(exe, m, n, p):
    """Run the executable with dimensions and capture its output."""
    cmd = [f"./{exe}", str(m), str(n), str(p), "perf"]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return float(cp.stdout)

for i in range(50, 3000, 50):
    old = run_exec(a, i, i, i,)
    new = run_exec(b, i, i, i)
    print(f"{i}: {old / new}")