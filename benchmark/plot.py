import matplotlib
matplotlib.use('Agg')

import matplotlib.pyplot as plt


PAGE_SIZE = 4096


intensities = {}
latencies = {}
throughputs = {}

with open("results.txt") as fres:
    cur_bench = None

    for line in fres.readlines():
        line = line.strip()
        if line.startswith("Latency Benchmark"):
            cur_bench = line[20:line.find(':')]
            intensities[cur_bench] = []
            latencies[cur_bench] = []
            throughputs[cur_bench] = []
        elif cur_bench is not None \
             and len(line) > 0 and line[0].isdecimal():
            line = line.split()
            
            intensity, latency = int(line[0]), float(line[1])
            intensities[cur_bench].append(intensity)
            latencies[cur_bench].append(latency)

            throughput = 1000.0 * (PAGE_SIZE / 1024) \
                         / ((1000.0 / intensity) + latency)
            throughputs[cur_bench].append(throughput)

# print(intensities)
# print(latencies)
# print(throughputs)


fig1, axs1 = plt.subplots(2, 2)
colors1 = ['b', 'g', 'r', 'c']

i = 0
for bench in intensities:
    ax = axs1[i // 2, i % 2]

    ax.plot(intensities[bench], latencies[bench],
            color=colors1[i], label=bench)
    ax.set_xlabel("Intensity (#4K-Reqs/s)")
    ax.set_ylabel("Latency (ms)")
    # ax.tick_params(axis='x', labelrotation=30)
    ax.set_title(bench)

    i += 1

fig1.suptitle("Latency Benchmarking (FTL = BAST)")
fig1.tight_layout(pad=2.0)

plt.savefig("results-la.png")


fig2, axs2 = plt.subplots(2, 2)
colors2 = ['b', 'g', 'r', 'c']

i = 0
for bench in intensities:
    ax = axs2[i // 2, i % 2]

    ax.plot(intensities[bench], throughputs[bench],
            color=colors2[i], label=bench)
    ax.set_xlabel("Intensity (#4K-Reqs/s)")
    ax.set_ylabel("Throughput (KB/s)")
    # ax.tick_params(axis='x', labelrotation=30)
    ax.set_title(bench)

    i += 1

fig2.suptitle("Throughput Benchmarking (FTL = BAST)")
fig2.tight_layout(pad=2.0)

plt.savefig("results-tp.png")
