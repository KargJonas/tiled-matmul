# started on Sat May 17 14:32:18 2025


 Performance counter stats for 'bin/main.parallel4 3000 3000 3000 perf':

     7.309.174.663      cpu_atom/cycles/                 #    1,966 GHz                         (53,47%)
     5.549.137.767      cpu_core/cycles/                 #    1,492 GHz                         (79,10%)
     6.144.632.652      cpu_atom/instructions/           #    0,84  insn per cycle              (62,22%)
     5.599.966.975      cpu_core/instructions/           #    1,01  insn per cycle              (89,26%)
          3.718,04 msec task-clock                       #    6,383 CPUs utilized             
          3.717,43 msec cpu-clock                        #    6,382 CPUs utilized             
               716      context-switches                 #  192,574 /sec                      
        46.393.951      cpu_atom/cache-misses/           #   14,15% of all cache refs           (62,25%)
        35.797.674      cpu_core/cache-misses/           #   18,52% of all cache refs           (89,30%)
       327.888.622      cpu_atom/cache-references/       #   88,189 M/sec                       (62,36%)
       193.337.240      cpu_core/cache-references/       #   52,000 M/sec                       (89,35%)
        89.458.537      cpu_atom/branches/               #   24,061 M/sec                       (62,54%)
       393.938.443      cpu_core/branches/               #  105,953 M/sec                       (89,52%)
           201.974      cpu_atom/branch-misses/          #    0,23% of all branches             (62,59%)
           702.157      cpu_core/branch-misses/          #    0,18% of all branches             (89,46%)
        42.815.660      cpu_atom/dTLB-load-misses/       #    0,90% of all dTLB cache accesses  (62,53%)
        11.192.305      cpu_core/dTLB-load-misses/       #    0,45% of all dTLB cache accesses  (89,43%)
     4.748.713.813      cpu_atom/dTLB-loads/             #    1,277 G/sec                       (62,64%)
     2.487.949.230      cpu_core/dTLB-loads/             #  669,156 M/sec                       (89,46%)
                16      cpu_atom/LLC-load-misses/        #    0,00% of all LL-cache accesses    (53,75%)
         3.042.769      cpu_core/LLC-load-misses/        #   18,18% of all LL-cache accesses    (89,46%)
        42.064.582      cpu_atom/LLC-loads/              #   11,314 M/sec                       (53,79%)
        16.736.377      cpu_core/LLC-loads/              #    4,501 M/sec                       (89,23%)

       0,582454786 seconds time elapsed

       3,622150000 seconds user
       0,095476000 seconds sys


