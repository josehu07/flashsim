/**
 * Standalone FlashSim simulator throughput benchmarking client.
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
 * == 167772160 bytes.
 */
static const unsigned long FLASH_CAPACITY = 167772160;
static const unsigned long PAGE_SIZE = 4096;


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
 * Throughput benchmark - sequential read.
 * Returns a safe finish time.
 */
static double
bench_seq_read(double begin_time_ms)
{
    double cur_time_ms = begin_time_ms;
    unsigned long cur_addr = 0;
    std::vector<int> intensities;
    std::vector<double> tp_results;

    for (int intensity = 100; intensity < 12000; intensity += 100) {
        /** A round of benchmarking for given intensity. */
        std::default_random_engine rand_gen;
        std::uniform_real_distribution<double> rand_dist(0.8, 1.2);
        double delta_ms = 1000.0 / (double) intensity;
        double round_begin_time_ms = cur_time_ms;
        double last_time_used_ms;
        int num_reqs = 0;

        for (num_reqs = 0; num_reqs < 499; ++num_reqs) {
            issue_read(cur_addr, PAGE_SIZE, cur_time_ms);
            cur_time_ms += delta_ms * rand_dist(rand_gen);
            cur_addr = (cur_addr + PAGE_SIZE) % FLASH_CAPACITY;
        }
        last_time_used_ms = issue_read(cur_addr, PAGE_SIZE, cur_time_ms);

        intensities.push_back(intensity);
        tp_results.push_back((500 * (PAGE_SIZE / 1024) * 1000)
                             / (cur_time_ms - round_begin_time_ms
                                + last_time_used_ms));

        cur_time_ms += 1000.0;
    }

    std::cout << "Throughput Benchmark - Sequential Read:"      << std::endl
              << "  Intensity (#4K-Reqs/s)   Throughput (KB/s)" << std::endl;
    for (size_t i = 0; i < intensities.size(); ++i)
        printf("  %20d     %15.2lf\n", intensities[i], tp_results[i]);

    return cur_time_ms + 1000.0;
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

    bench_seq_read(1000.0);

    return 0;
}
