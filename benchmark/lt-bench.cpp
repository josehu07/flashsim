/**
 * Standalone FlashSim simulator benchmarking client. Passing actual data
 * here, so MUST ensure that `PAGE_ENABLE_DATA` option in conf is set to 1.
 *
 * Author: Guanzhou Hu <guanzhou.hu@wisc.edu>, 2020.
 */


#include <string>
#include <vector>
#include <random>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


/**
 * Helper functions.
 */
static void
error(std::string msg)
{
    std::cerr << "ERROR: " << msg << std::endl;
    exit(1);
}


/** Global handle & variables. */
static std::string sock_name;
static int ssock;


/**
 * Request header (1st message) format.
 * Message size MUST exactly match in bytes!
 */
struct __attribute__((__packed__)) req_header {
    uint32_t direction     : 32;
    uint64_t addr          : 64;
    uint32_t size          : 32;
    uint64_t start_time_us : 64;
};

static const size_t REQ_HEADER_LENGTH = 24;

static const int DIR_READ  = 0;
static const int DIR_WRITE = 1;


/**
 * Assuming default config, so total flash capacity should be 160MiB
 * == 167772160 bytes. We are using 40% of them - leaving the rest
 * for page redirection or garbage collection tasks.
 */
static const unsigned long FLASH_SPACE = 67108864;
// static const unsigned long FLASH_SPACE = 40263680;
static const unsigned long PAGE_SIZE = 4096;


/** Benchmarking parameters. */
static const int MAX_INTENSITY = 3000;
static const int INTENSITY_TICK = 200;
static const int PASSES_PER_ROUND = 2;


/**
 * Open a client-side socket and connect to the given sock file.
 */
static void
prepare_socket()
{
    struct sockaddr_un saddr;
    int ret;

    ssock = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (ssock < 0)
        error("socket() failed");

    memset(&saddr, 0, sizeof(saddr));
    saddr.sun_family = AF_LOCAL;
    strncpy(saddr.sun_path, sock_name.c_str(), sizeof(saddr.sun_path) - 1);

    ret = connect(ssock, (struct sockaddr *) &saddr, sizeof(saddr));
    if (ret)
        error("connect() failed");

    std::cout << "Connected to local socket file `" << sock_name << "`..."
              << std::endl;
}


/**
 * Make a write request.
 */
static void
issue_write(unsigned long addr, unsigned int size)
{
    struct req_header header;
    int rbytes, wbytes;
    uint64_t start_time_us, time_used_us;
    void *data;

    if (addr % PAGE_SIZE != 0 || size <= 0)
        error("invalid issue_write()");

    data = malloc(size);

    // Request header.
    header.direction = DIR_WRITE;
    header.addr = addr;
    header.size = size;
    header.

    wbytes = write(ssock, &header, REQ_HEADER_LENGTH);
    if (wbytes != REQ_HEADER_LENGTH)
        error("write request header send failed");

    // Data to write.
    wbytes = write(ssock, data, header.size);
    if (wbytes != (int) header.size)
        error("write request data send failed");

    // ACK.
    rbytes = read(ssock, &ack_byte, 1);
    if (rbytes != 1)
        error("write request ACK error");

    free(data);
}

/**
 * Make a read request.
 */
static void
issue_read(unsigned long addr, unsigned int size)
{
    struct req_header header;
    int rbytes, wbytes;
    void *data;
    char ack_byte;

    if (addr % PAGE_SIZE != 0 || size <= 0)
        error("invalid issue_read()");

    data = malloc(size);

    // Request header.
    header.direction = DIR_READ;
    header.addr = addr;
    header.size = size;

    wbytes = write(ssock, &header, REQ_HEADER_LENGTH);
    if (wbytes != REQ_HEADER_LENGTH)
        error("read request header send failed");

    // Data read out respond.
    rbytes = read(ssock, data, header.size);
    if (rbytes != (int) header.size)
        error("read request data recv failed");

    // ACK.
    rbytes = read(ssock, &ack_byte, 1);
    if (rbytes != 1)
        error("read request ACK error");

    free(data);
}


/**
 * Benchmark - sequential read.
 */
static void
bench_seq_read()
{
    struct timeval old_time, new_time;
    unsigned long addr = 0;

    std::cout << "Benchmark - Logical Sequential Read:"         << std::endl
              << "  Intensity (#4K-Reqs/s)   Interval (ms)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        double delta_ms = 1000.0 / (double) intensity, avg_interval_ms = 0;
        int num_reqs = 0, total_reqs = PASSES_PER_ROUND * intensity;

        for (num_reqs = 0; num_reqs < total_reqs; ++num_reqs) {
            double time_used_ms;

            gettimeofday(&old_time, NULL);
            issue_read(addr, PAGE_SIZE);
            gettimeofday(&new_time, NULL);

            time_used_ms = (double) (new_time.tv_sec - old_time.tv_sec) * 1000
                         + (double) (new_time.tv_usec - old_time.tv_usec) / 1000;

            if (delta_ms > time_used_ms) {
                avg_interval_ms += delta_ms / total_reqs;
                usleep(1000.0 * (delta_ms - time_used_ms));
            } else
                avg_interval_ms += time_used_ms / total_reqs;

            addr = (addr + PAGE_SIZE) % FLASH_SPACE;
        }

        printf("  %20d     %11.5lf\n", intensity, avg_interval_ms);
        fflush(stdout);
    }
}

/**
 * Latency benchmark - sequential write.
 */
static void
bench_seq_write()
{
    struct timeval old_time, new_time;
    unsigned long addr = 0;

    std::cout << "Benchmark - Logical Sequential Write:"    << std::endl
              << "  Intensity (#4K-Reqs/s)   Interval (ms)" << std::endl;    

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        double delta_ms = 1000.0 / (double) intensity, avg_interval_ms = 0;
        int num_reqs = 0, total_reqs = PASSES_PER_ROUND * intensity;

        for (num_reqs = 0; num_reqs < total_reqs; ++num_reqs) {
            double time_used_ms;

            gettimeofday(&old_time, NULL);
            issue_write(addr, PAGE_SIZE);
            gettimeofday(&new_time, NULL);

            time_used_ms = (double) (new_time.tv_sec - old_time.tv_sec) * 1000
                         + (double) (new_time.tv_usec - old_time.tv_usec) / 1000;

            if (delta_ms > time_used_ms) {
                avg_interval_ms += delta_ms / total_reqs;
                usleep(1000.0 * (delta_ms - time_used_ms));
            } else
                avg_interval_ms += time_used_ms / total_reqs;

            addr = (addr + PAGE_SIZE) % FLASH_SPACE;
        }

        printf("  %20d     %11.5lf\n", intensity, avg_interval_ms);
        fflush(stdout);
    }
}

/**
 * Latency benchmark - uniformly random read.
 */
static void
bench_rnd_read()
{
    struct timeval old_time, new_time;
    std::default_random_engine rand_gen;
    std::uniform_int_distribution<unsigned long> addr_dist(0,
                                                           FLASH_SPACE
                                                           / PAGE_SIZE);
    unsigned long addr;

    std::cout << "Benchmark - Uniformly Random Read:"       << std::endl
              << "  Intensity (#4K-Reqs/s)   Interval (ms)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        double delta_ms = 1000.0 / (double) intensity, avg_interval_ms = 0;
        int num_reqs = 0, total_reqs = PASSES_PER_ROUND * intensity;

        for (num_reqs = 0; num_reqs < total_reqs; ++num_reqs) {
            double time_used_ms;

            addr = PAGE_SIZE * addr_dist(rand_gen);
            
            gettimeofday(&old_time, NULL);
            issue_read(addr, PAGE_SIZE);
            gettimeofday(&new_time, NULL);

            time_used_ms = (double) (new_time.tv_sec - old_time.tv_sec) * 1000
                         + (double) (new_time.tv_usec - old_time.tv_usec) / 1000;

            if (delta_ms > time_used_ms) {
                avg_interval_ms += delta_ms / total_reqs;
                usleep(1000.0 * (delta_ms - time_used_ms));
            } else
                avg_interval_ms += time_used_ms / total_reqs;
        }

        printf("  %20d     %11.5lf\n", intensity, avg_interval_ms);
        fflush(stdout);
    }
}

/**
 * Latency benchmark - uniformly random write.
 */
static void
bench_rnd_write()
{
    struct timeval old_time, new_time;
    std::default_random_engine rand_gen;
    std::uniform_int_distribution<unsigned long> addr_dist(0,
                                                           FLASH_SPACE
                                                           / PAGE_SIZE);
    unsigned long addr;

    std::cout << "Benchmark - Uniformly Random Write:"      << std::endl
              << "  Intensity (#4K-Reqs/s)   Interval (ms)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        double delta_ms = 1000.0 / (double) intensity, avg_interval_ms = 0;
        int num_reqs = 0, total_reqs = PASSES_PER_ROUND * intensity;

        for (num_reqs = 0; num_reqs < total_reqs; ++num_reqs) {
            double time_used_ms;

            addr = PAGE_SIZE * addr_dist(rand_gen);

            gettimeofday(&old_time, NULL);
            issue_write(addr, PAGE_SIZE);
            gettimeofday(&new_time, NULL);

            time_used_ms = (double) (new_time.tv_sec - old_time.tv_sec) * 1000
                         + (double) (new_time.tv_usec - old_time.tv_usec) / 1000;

            if (delta_ms > time_used_ms) {
                avg_interval_ms += delta_ms / total_reqs;
                usleep(1000.0 * (delta_ms - time_used_ms));
            } else
                avg_interval_ms += time_used_ms / total_reqs;
        }

        printf("  %20d     %11.5lf\n", intensity, avg_interval_ms);
        fflush(stdout);
    }
}


/**
 * Fill device with randomly written data.
 */
static void
fill_device()
{
    unsigned long addr = 0;
    
    for (size_t i = 0; i < (FLASH_SPACE / PAGE_SIZE); ++i) {
        addr = i * PAGE_SIZE;
        issue_write(addr, PAGE_SIZE);
    }
}


int
main(int argc, char *argv[])
{
    if (argc != 2)
        error("please provide one argument: the socket file path");

    sock_name = argv[1];

    if (sizeof(struct req_header) != REQ_HEADER_LENGTH)
        error("request header length incorrect");

    /** Open client socket & connect. */
    prepare_socket();

    fill_device();

    bench_seq_read();
    bench_rnd_read();
    
    bench_seq_write();
    bench_rnd_write();

    return 0;
}
