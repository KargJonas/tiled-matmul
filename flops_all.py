import subprocess
import sys
import os

a = sys.argv[1]
make_csv = len(sys.argv) == 3 and sys.argv[2] == "csv"
out_csv = f"{os.path.basename(sys.argv[1])}.flops.csv"

def run_exec(exe, m, n, p):
    """Run the executable with dimensions and capture its output."""
    cmd = [f"./{exe}", str(m), str(n), str(p), "perf"]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return float(cp.stdout)

if make_csv:
    with open(out_csv, "w", buffering=1) as file:
        print(file)
        file.write("n,gflops\n")
        for n in range(100, 4000, 100):
            time = run_exec(a, n, n, n)
            gflops = ((2 * n ** 3) / time) / 10 ** 9
            print(f"{gflops} GFLOPS \t @ n={n}")
            file.write(f"{n},{gflops}\n")
            file.flush()
else:
    for n in range(100, 4000, 100):
        time = run_exec(a, n, n, n)
        gflops = ((2 * n ** 3) / time) / 10 ** 9
        print(f"{gflops} GFLOPS \t @ n={n}")
