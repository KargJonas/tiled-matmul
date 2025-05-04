import subprocess
import sys


def run_exec(exe, m, n, p):
    """Run the executable with dimensions and capture its output."""
    cmd = [f"./{exe}", str(m), str(n), str(p)]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return float(cp.stdout)

for i in range(50, 3000, 50):
    main = run_exec("main", i, i, i)
    parallel = run_exec("main.parallel", i, i, i)
    print(f"{i}: {main / parallel}")