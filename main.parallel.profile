# started on Fri May 16 23:00:50 2025


 Performance counter stats for 'bin/main.parallel 3000 3000 3000 perf':

    11.756.930.180      cpu_atom/cycles/                 #    1,840 GHz                         (59,87%)
    11.168.185.273      cpu_core/cycles/                 #    1,747 GHz                         (65,68%)
     3.900.367.401      cpu_atom/instructions/           #    0,33  insn per cycle              (69,97%)
     7.474.429.876      cpu_core/instructions/           #    0,67  insn per cycle              (74,03%)
          6.391,37 msec task-clock                       #    7,680 CPUs utilized             
          6.390,70 msec cpu-clock                        #    7,679 CPUs utilized             
             1.002      context-switches                 #  156,774 /sec                      
        44.342.634      cpu_atom/cache-misses/           #    6,52% of all cache refs           (69,95%)
        54.565.306      cpu_core/cache-misses/           #    7,04% of all cache refs           (74,12%)
       680.267.524      cpu_atom/cache-references/       #  106,435 M/sec                       (69,87%)
       775.202.163      cpu_core/cache-references/       #  121,289 M/sec                       (74,07%)
       226.858.654      cpu_atom/branches/               #   35,495 M/sec                       (69,97%)
       729.386.666      cpu_core/branches/               #  114,121 M/sec                       (74,09%)
           230.457      cpu_atom/branch-misses/          #    0,10% of all branches             (70,08%)
         2.857.605      cpu_core/branch-misses/          #    0,39% of all branches             (74,12%)
            44.290      cpu_atom/dTLB-load-misses/       #    0,00% of all dTLB cache accesses  (70,37%)
           135.224      cpu_core/dTLB-load-misses/       #    0,00% of all dTLB cache accesses  (74,22%)
     6.307.501.579      cpu_atom/dTLB-loads/             #  986,878 M/sec                       (70,30%)
     4.956.044.289      cpu_core/dTLB-loads/             #  775,427 M/sec                       (74,19%)
           556.662      cpu_atom/LLC-load-misses/        #    0,18% of all LL-cache accesses    (60,02%)
        10.525.296      cpu_core/LLC-load-misses/        #   34,10% of all LL-cache accesses    (74,22%)
       302.923.959      cpu_atom/LLC-loads/              #   47,396 M/sec                       (59,86%)
        30.867.214      cpu_core/LLC-loads/              #    4,830 M/sec                       (74,03%)

       0,832202512 seconds time elapsed

       6,275632000 seconds user
       0,114627000 seconds sys


