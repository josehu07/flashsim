import matplotlib
matplotlib.use('Agg')

import matplotlib.pyplot as plt


intensities = {}
throughputs = {}

with open("tp-result.txt") as fres:
    cur_bench = None

    for line in fres.readlines():
        line = line.strip()
        if line.startswith("Throughput Benchmark"):
            cur_bench = line[23:line.find(':')]
            intensities[cur_bench] = []
            throughputs[cur_bench] = []
        elif cur_bench is not None \
             and len(line) > 0 and line[0].isdecimal():
            line = line.split()
            intensity, throughput = int(line[0]), float(line[1])
            intensities[cur_bench].append(intensity)
            throughputs[cur_bench].append(throughput)

print(intensities)
print(throughputs)


for bench in intensities:
    plt.plot(intensities[bench], throughputs[bench])
    plt.title("Benchmark - " + bench)
    plt.xlabel("Intensity (#4K-Reqs/s)")
    plt.ylabel("Throughput (KB/s)")
    plt.savefig(bench + ".png")
