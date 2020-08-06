import matplotlib
matplotlib.use('Agg')

import matplotlib.pyplot as plt


PAGE_SIZE = 4096


intensities = {}
throughputs = {}

with open("results.txt") as fres:
    cur_bench = None

    for line in fres.readlines():
        line = line.strip()
        if line.startswith("Benchmark"):
            cur_bench = line[12:line.find(':')]
            intensities[cur_bench] = []
            throughputs[cur_bench] = []
        elif cur_bench is not None \
             and len(line) > 0 and line[0].isdecimal():
            line = line.split()
            intensity, throughput = int(line[0]), float(line[1])
            intensities[cur_bench].append(intensity)
            throughputs[cur_bench].append(throughput)

# print(intensities)
# print(throughputs)


fig, axs = plt.subplots(2, 2)
colors = ['b', 'g', 'r', 'c']

i = 0
for bench in intensities:
    ax = axs[i // 2, i % 2]

    ax.plot(intensities[bench], throughputs[bench],
            color=colors[i], label=bench)
    ax.set_xlabel("Intensity (#4K-Reqs/s)")
    ax.set_ylabel("Throughput (KB/s)")
    # ax.tick_params(axis='x', labelrotation=30)
    ax.set_title(bench)

    i += 1

fig.suptitle("Throughput Benchmarking (FTL = BAST)")
fig.tight_layout(pad=2.0)

plt.savefig("results.png")
