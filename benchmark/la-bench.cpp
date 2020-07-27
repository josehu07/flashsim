/**
 * Standalone FlashSim simulator benchmarking client.
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
    int           direction : 32;
    unsigned long addr      : 64;
    unsigned int  size      : 32;
    double        start_time;
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
static const unsigned long PAGE_SIZE = 4096;


/** Benchmarking parameters. */
static const int MAX_INTENSITY = 12000;
static const int INTENSITY_TICK = 200;
static const int REQS_PER_ROUND = 20000;


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
 * Make a write request. Returns the processing time elapsed.
 */
static double
issue_write(unsigned long addr, unsigned int size, double start_time)
{
    struct req_header header;
    int rbytes, wbytes;
    void *data;
    char time_used_buf[8];
    double time_used;

    if (addr % PAGE_SIZE != 0 || size <= 0)
        error("invalid issue_write()");

    data = malloc(size);

    // Request header.
    header.direction = DIR_WRITE;
    header.addr = addr;
    header.size = size;
    header.start_time = start_time;

    wbytes = write(ssock, &header, REQ_HEADER_LENGTH);
    if (wbytes != REQ_HEADER_LENGTH)
        error("write request header send failed");

    // Data to write.
    wbytes = write(ssock, data, header.size);
    if (wbytes != (int) header.size)
        error("write request data send failed");

    // Processing time respond.
    rbytes = read(ssock, time_used_buf, 8);
    if (rbytes != 8)
        error("write processing time recv failed");

    free(data);

    time_used = *((double *) time_used_buf);
    return time_used;
}

/**
 * Make a read request. Returns the processing time elapsed.
 */
static double
issue_read(unsigned long addr, unsigned int size, double start_time)
{
    struct req_header header;
    int rbytes, wbytes;
    void *data;
    char time_used_buf[8];
    double time_used;

    if (addr % PAGE_SIZE != 0 || size <= 0)
        error("invalid issue_write()");

    data = malloc(size);

    // Request header.
    header.direction = DIR_READ;
    header.addr = addr;
    header.size = size;
    header.start_time = start_time;

    wbytes = write(ssock, &header, REQ_HEADER_LENGTH);
    if (wbytes != REQ_HEADER_LENGTH)
        error("read request header send failed");

    // Data read out respond.
    rbytes = read(ssock, data, header.size);
    if (rbytes != (int) header.size)
        error("read request data recv failed");

    // Processing time respond.
    rbytes = read(ssock, time_used_buf, 8);
    if (rbytes != 8)
        error("read processing time recv failed");

    free(data);

    time_used = *((double *) time_used_buf);
    return time_used;
}


/**
 * Latency benchmark - sequential read.
 * Returns a safe finish time.
 */
static double
bench_seq_read(double begin_time_ms)
{
    double cur_time_ms = begin_time_ms;
    unsigned long addr = 0;

    std::cout << "Latency Benchmark - Logical Sequential Read:" << std::endl
              << "  Intensity (#4K-Reqs/s)   Latency (ms)"      << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        std::default_random_engine rand_gen;
        std::uniform_real_distribution<double> rand_dist(0.95, 1.05);
        double delta_ms = 1000.0 / (double) intensity;
        double avg_time_used_ms = 0;
        int num_reqs = 0;

        for (num_reqs = 0; num_reqs < REQS_PER_ROUND; ++num_reqs) {
            double time_used_ms = issue_read(addr, PAGE_SIZE, cur_time_ms);
            avg_time_used_ms += time_used_ms / REQS_PER_ROUND;

            cur_time_ms += delta_ms * rand_dist(rand_gen);
            addr = (addr + PAGE_SIZE) % FLASH_SPACE;
        }

        printf("  %20d     %10.2lf\n", intensity, avg_time_used_ms);
        fflush(stdout);

        cur_time_ms += 50000.0;
    }

    return cur_time_ms + 50000.0;
}

/**
 * Latency benchmark - sequential write.
 * Returns a safe finish time.
 */
static double
bench_seq_write(double begin_time_ms)
{
    double cur_time_ms = begin_time_ms;
    unsigned long addr = 0;

    std::cout << "Latency Benchmark - Logical Sequential Write:" << std::endl
              << "  Intensity (#4K-Reqs/s)   Latency (ms)"       << std::endl;    

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        std::default_random_engine rand_gen;
        std::uniform_real_distribution<double> rand_dist(0.95, 1.05);
        double delta_ms = 1000.0 / (double) intensity;
        double avg_time_used_ms = 0;
        int num_reqs = 0;

        for (num_reqs = 0; num_reqs < REQS_PER_ROUND; ++num_reqs) {
            double time_used_ms = issue_write(addr, PAGE_SIZE, cur_time_ms);
            avg_time_used_ms += time_used_ms / REQS_PER_ROUND;

            cur_time_ms += delta_ms * rand_dist(rand_gen);
            addr = (addr + PAGE_SIZE) % FLASH_SPACE;
        }

        printf("  %20d     %10.2lf\n", intensity, avg_time_used_ms);
        fflush(stdout);

        cur_time_ms += 50000.0;
    }

    return cur_time_ms + 50000.0;
}

/**
 * Latency benchmark - uniformly random read.
 * Returns a safe finish time.
 */
static double
bench_rnd_read(double begin_time_ms)
{
    double cur_time_ms = begin_time_ms;
    std::default_random_engine rand_gen;
    std::uniform_int_distribution<unsigned long> addr_dist(0,
                                                           FLASH_SPACE
                                                           / PAGE_SIZE);
    unsigned long addr;

    std::cout << "Latency Benchmark - Uniformly Random Read:" << std::endl
              << "  Intensity (#4K-Reqs/s)   Latency (ms)"    << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        std::uniform_real_distribution<double> delta_dist(0.95, 1.05);
        double delta_ms = 1000.0 / (double) intensity;
        double avg_time_used_ms = 0;
        int num_reqs = 0;

        for (num_reqs = 0; num_reqs < REQS_PER_ROUND; ++num_reqs) {
            addr = PAGE_SIZE * addr_dist(rand_gen);
            
            double time_used_ms = issue_read(addr, PAGE_SIZE, cur_time_ms);
            avg_time_used_ms += time_used_ms / REQS_PER_ROUND;

            cur_time_ms += delta_ms * delta_dist(rand_gen);
        }

        printf("  %20d     %10.2lf\n", intensity, avg_time_used_ms);
        fflush(stdout);

        cur_time_ms += 50000.0;
    }

    return cur_time_ms + 50000.0;
}

/**
 * Latency benchmark - uniformly random write.
 * Returns a safe finish time.
 */
static double
bench_rnd_write(double begin_time_ms)
{
    double cur_time_ms = begin_time_ms;
    std::default_random_engine rand_gen;
    std::uniform_int_distribution<unsigned long> addr_dist(0,
                                                           FLASH_SPACE
                                                           / PAGE_SIZE);
    unsigned long addr;

    std::cout << "Latency Benchmark - Uniformly Random Write:" << std::endl
              << "  Intensity (#4K-Reqs/s)   Latency (ms)"     << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {
        /** A round of benchmarking for given intensity. */
        std::uniform_real_distribution<double> delta_dist(0.95, 1.05);
        double delta_ms = 1000.0 / (double) intensity;
        double avg_time_used_ms = 0;
        int num_reqs = 0;

        for (num_reqs = 0; num_reqs < REQS_PER_ROUND; ++num_reqs) {
            addr = PAGE_SIZE * addr_dist(rand_gen);
            
            double time_used_ms = issue_write(addr, PAGE_SIZE, cur_time_ms);
            avg_time_used_ms += time_used_ms / REQS_PER_ROUND;

            cur_time_ms += delta_ms * delta_dist(rand_gen);
        }

        printf("  %20d     %10.2lf\n", intensity, avg_time_used_ms);
        fflush(stdout);

        cur_time_ms += 50000.0;
    }

    return cur_time_ms + 50000.0;
}


/**
 * Fill device with randomly written data.
 */
static double
fill_device(double begin_time_ms)
{
    double cur_time_ms = begin_time_ms, delta_ms = 1.0;
    unsigned long addr = 0;
    
    for (size_t i = 0; i < (FLASH_SPACE / PAGE_SIZE); ++i) {
        addr = i * PAGE_SIZE;
        issue_write(addr, PAGE_SIZE, cur_time_ms);
        cur_time_ms += delta_ms;
    }

    return cur_time_ms + 50000.0;
}


int
main(int argc, char *argv[])
{
    double cur_time_ms = 1000.0;

    if (argc != 2)
        error("please provide one argument: the socket file path");

    sock_name = argv[1];

    if (sizeof(struct req_header) != REQ_HEADER_LENGTH)
        error("request header length incorrect");

    /** Open client socket & connect. */
    prepare_socket();

    cur_time_ms = fill_device(cur_time_ms);

    cur_time_ms = bench_seq_read(cur_time_ms);
    cur_time_ms = bench_rnd_read(cur_time_ms);
    
    cur_time_ms = bench_seq_write(cur_time_ms);
    cur_time_ms = bench_rnd_write(cur_time_ms);

    return 0;
}
