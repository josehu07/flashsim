/**
 * Standalone FlashSim simulator benchmarking client. Passing actual data
 * here, so MUST ensure that `PAGE_ENABLE_DATA` option in conf is set to 1.
 *
 * This is a synchronous benchmarking, not utilizing in-device parallelism.
 *
 * Author: Guanzhou Hu <guanzhou.hu@wisc.edu>, 2020.
 */


#include <string>
#include <vector>
#include <deque>
#include <random>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <numeric>
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
 * Assuming default config, so total flash capacity should be 160MiB
 * == 167772160 bytes. We are using 40% of them - leaving the rest
 * for page redirection or garbage collection tasks.
 */
// static const unsigned long FLASH_SPACE = 67108864;
static const unsigned long FLASH_SPACE = 40263680;
static const unsigned long PAGE_SIZE = 4096;


/** Benchmarking parameters. */
static const int MAX_INTENSITY = 4000;
static const int INTENSITY_TICK = 200;
static const int SECS_PER_ROUND = 5;


static void
error(std::string msg)
{
    std::cerr << "ERROR: " << msg << std::endl;
    exit(1);
}


/**
 * Timing utilities for the experiments.
 */
static struct timeval boot_time;

uint64_t
get_cur_time_us()
{
    struct timeval cur_time;
    uint64_t cur_time_us;

    gettimeofday(&cur_time, NULL);

    cur_time_us = (cur_time.tv_sec - boot_time.tv_sec) * 1000000
                  + (cur_time.tv_usec - boot_time.tv_usec);

    return cur_time_us;
}


/**
 * Submission queue.
 * Submission thread runs separately.
 */
struct req_entry {
    unsigned int direction;
    unsigned long addr;
    unsigned int size;
    uint64_t start_time_us;
};

static std::deque<struct req_entry> submit_queue;

static std::mutex submit_queue_lock;
static std::condition_variable submit_queue_cv;


/**
 * Open a client-side socket and connect to the given sock file.
 */
static std::string sock_name;
static int ssock;

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


/*========== Device log implementation BEGIN ==========*/

/**
 * Device log for throughput measurement. It records the latest IOs
 * through this device.
 */
struct log_entry {
    uint64_t start_time_us;
    uint64_t finish_time_us;
    uint32_t bytes;
};

static const int DEVICE_LOG_LENGTH = 120000;
static std::deque<struct log_entry> device_log;

static std::mutex device_log_lock;

/**
 * Push a new IO entry into the log, possibly erase an oldest one if
 * the log is full. Returns the timestamp for this entry.
 */
void
log_push_entry(uint64_t start_time_us, uint64_t finish_time_us,
                     uint32_t bytes)
{
    struct log_entry entry;
    entry.start_time_us = start_time_us;
    entry.finish_time_us = finish_time_us;
    entry.bytes = bytes;

    std::lock_guard<std::mutex> lk(device_log_lock);

    device_log.push_back(entry);

    if (device_log.size() > DEVICE_LOG_LENGTH)
        device_log.pop_front();
}

/**
 * Query the log for throughput (KB/s) of given time interval.
 */
double
log_query_throughput(uint64_t begin_time_us, uint64_t end_time_us)
{
    double kilobytes = 0.0;

    std::lock_guard<std::mutex> lk(device_log_lock);

    for (auto it = device_log.rbegin(); it != device_log.rend(); ++it) {
        if (it->finish_time_us <= begin_time_us)
            break;

        if (it->finish_time_us <= end_time_us)
            kilobytes += (double) it->bytes / 1024.0;
    }

    return (kilobytes * 1000000) / (end_time_us - begin_time_us);
}

/*========== Device log implementation END ==========*/


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
 * Issuing a write / read request.
 */
static void
_submit_write(unsigned long addr, unsigned int size, uint64_t start_time_us)
{
    struct req_header header;
    int rbytes, wbytes;
    uint64_t time_used_us;
    void *data;

    if (addr % PAGE_SIZE != 0 || size <= 0)
        error("invalid issue_write()");

    data = malloc(size);

    // Request header.
    header.direction = DIR_WRITE;
    header.addr = addr;
    header.size = size;
    header.start_time_us = start_time_us;

    wbytes = write(ssock, &header, REQ_HEADER_LENGTH);
    if (wbytes != REQ_HEADER_LENGTH)
        error("write request header send failed");

    // Data to write.
    wbytes = write(ssock, data, header.size);
    if (wbytes != (int) header.size)
        error("write request data send failed");

    // Processing time respond.
    rbytes = read(ssock, &time_used_us, 8);
    if (rbytes != 8)
        error("write processing time recv failed");

    usleep(time_used_us);

    free(data);
}

static void
_submit_read(unsigned long addr, unsigned int size, uint64_t start_time_us)
{
    struct req_header header;
    int rbytes, wbytes;
    uint64_t time_used_us;
    void *data;

    if (addr % PAGE_SIZE != 0 || size <= 0)
        error("invalid issue_read()");

    data = malloc(size);

    // Request header.
    header.direction = DIR_READ;
    header.addr = addr;
    header.size = size;
    header.start_time_us = start_time_us;

    wbytes = write(ssock, &header, REQ_HEADER_LENGTH);
    if (wbytes != REQ_HEADER_LENGTH)
        error("read request header send failed");

    // Data read out respond.
    rbytes = read(ssock, data, header.size);
    if (rbytes != (int) header.size)
        error("read request data recv failed");

    // Processing time respond.
    rbytes = read(ssock, &time_used_us, 8);
    if (rbytes != 8)
        error("read processing time recv failed");

    usleep(time_used_us);

    free(data);
}

/**
 * Submission thread runs separately.
 */
static void
_submit_thread_func()
{
    unsigned int direction;
    unsigned long addr;
    unsigned int size;
    uint64_t start_time_us;

    while (1) {
        {
            /** Wait when list is empty. */
            std::unique_lock<std::mutex> lk(submit_queue_lock);
            submit_queue_cv.wait(lk, []{ return submit_queue.size() > 0; });

            /** Extract an entry from queue head. */
            direction = submit_queue.front().direction;
            addr = submit_queue.front().addr;
            size = submit_queue.front().size;
            start_time_us = submit_queue.front().start_time_us;

            submit_queue.pop_front();
        }

        /** Process the request. */
        switch (direction) {
        case DIR_WRITE:
            _submit_write(addr, size, start_time_us);
            break;
        case DIR_READ:
            _submit_read(addr, size, start_time_us);
            break;
        }

        log_push_entry(start_time_us, get_cur_time_us(), size);
    }
}


/*========== Benchmarking Implemention BEGIN. ==========*/

/** Clearing after round. */
static inline void
bench_clean_up()
{
    usleep(2000000);

    std::unique_lock<std::mutex> lk1(submit_queue_lock, std::defer_lock);
    std::unique_lock<std::mutex> lk2(device_log_lock, std::defer_lock);
    std::lock(lk1, lk2);

    submit_queue.clear();
    device_log.clear();
}

/**
 * Benchmark - sequential read.
 */
static void
bench_seq_read()
{
    unsigned long addr = 0;

    std::cout << "Benchmark - Logical Sequential Read:"         << std::endl
              << "  Intensity (#4K-Reqs/s)   Throughput (KB/s)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {

        /** A round of benchmarking for given intensity. */
        uint64_t delta_us = 1000000 / intensity;
        uint64_t base_time_us, cur_time_us, log_interval_us = 0;
        std::vector<double> throughputs;

        base_time_us = get_cur_time_us();
        cur_time_us = base_time_us;
        log_interval_us = 0;

        do {
            struct req_entry entry;
            uint64_t new_time_us = get_cur_time_us();

            entry.direction = DIR_READ;
            entry.addr = addr;
            entry.size = PAGE_SIZE;
            entry.start_time_us = new_time_us;

            if (new_time_us - base_time_us >= 1000000)
                log_interval_us += new_time_us - cur_time_us;
            cur_time_us = new_time_us;

            if (log_interval_us > 100000) {
                throughputs.push_back(log_query_throughput(cur_time_us
                                                           - log_interval_us,
                                                           cur_time_us));
                log_interval_us = 0;
            }

            {
                std::lock_guard<std::mutex> lk(submit_queue_lock);

                submit_queue.push_back(entry);
                submit_queue_cv.notify_all();
            }

            usleep(delta_us);

            addr = (addr + PAGE_SIZE) % FLASH_SPACE;
        } while (cur_time_us < base_time_us + 1000000 * SECS_PER_ROUND);

        double avg_throughput = std::accumulate(throughputs.begin(),
                                                throughputs.end(),
                                                0.0) / throughputs.size();
        printf("  %20d     %15.5lf\n", intensity, avg_throughput);
        fflush(stdout);

        bench_clean_up();
    }
}

/**
 * Benchmark - sequential write.
 */
static void
bench_seq_write()
{
    unsigned long addr = 0;

    std::cout << "Benchmark - Logical Sequential Write:"        << std::endl
              << "  Intensity (#4K-Reqs/s)   Throughput (KB/s)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {

        /** A round of benchmarking for given intensity. */
        uint64_t delta_us = 1000000 / intensity;
        uint64_t base_time_us, cur_time_us, log_interval_us = 0;
        std::vector<double> throughputs;

        base_time_us = get_cur_time_us();
        cur_time_us = base_time_us;
        log_interval_us = 0;

        do {
            struct req_entry entry;
            uint64_t new_time_us = get_cur_time_us();

            entry.direction = DIR_WRITE;
            entry.addr = addr;
            entry.size = PAGE_SIZE;
            entry.start_time_us = new_time_us;

            if (new_time_us - base_time_us >= 1000000)
                log_interval_us += new_time_us - cur_time_us;
            cur_time_us = new_time_us;

            if (log_interval_us > 100000) {
                throughputs.push_back(log_query_throughput(cur_time_us
                                                           - log_interval_us,
                                                           cur_time_us));
                log_interval_us = 0;
            }

            {
                std::lock_guard<std::mutex> lk(submit_queue_lock);

                submit_queue.push_back(entry);
                submit_queue_cv.notify_all();
            }

            usleep(delta_us);

            addr = (addr + PAGE_SIZE) % FLASH_SPACE;
        } while (cur_time_us < base_time_us + 1000000 * SECS_PER_ROUND);

        double avg_throughput = std::accumulate(throughputs.begin(),
                                                throughputs.end(),
                                                0.0) / throughputs.size();
        printf("  %20d     %15.5lf\n", intensity, avg_throughput);
        fflush(stdout);

        bench_clean_up();
    }
}

/**
 * Benchmark - uniformly random read.
 */
static void
bench_rnd_read()
{
    std::default_random_engine rand_gen;
    std::uniform_int_distribution<unsigned long> addr_dist(0,
                                                           FLASH_SPACE
                                                           / PAGE_SIZE);

    std::cout << "Benchmark - Uniformly Random Read:"           << std::endl
              << "  Intensity (#4K-Reqs/s)   Throughput (KB/s)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {

        /** A round of benchmarking for given intensity. */
        uint64_t delta_us = 1000000 / intensity;
        uint64_t base_time_us, cur_time_us, log_interval_us = 0;
        std::vector<double> throughputs;

        base_time_us = get_cur_time_us();
        cur_time_us = base_time_us;
        log_interval_us = 0;

        do {
            struct req_entry entry;
            uint64_t new_time_us = get_cur_time_us();

            entry.direction = DIR_READ;
            entry.addr = PAGE_SIZE * addr_dist(rand_gen);;
            entry.size = PAGE_SIZE;
            entry.start_time_us = new_time_us;

            if (new_time_us - base_time_us >= 1000000)
                log_interval_us += new_time_us - cur_time_us;
            cur_time_us = new_time_us;

            if (log_interval_us > 100000) {
                throughputs.push_back(log_query_throughput(cur_time_us
                                                           - log_interval_us,
                                                           cur_time_us));
                log_interval_us = 0;
            }

            {
                std::lock_guard<std::mutex> lk(submit_queue_lock);

                submit_queue.push_back(entry);
                submit_queue_cv.notify_all();
            }

            usleep(delta_us);
        } while (cur_time_us < base_time_us + 1000000 * SECS_PER_ROUND);

        double avg_throughput = std::accumulate(throughputs.begin(),
                                                throughputs.end(),
                                                0.0) / throughputs.size();
        printf("  %20d     %15.5lf\n", intensity, avg_throughput);
        fflush(stdout);

        bench_clean_up();
    }
}

/**
 * Benchmark - uniformly random read.
 */
static void
bench_rnd_write()
{
    std::default_random_engine rand_gen;
    std::uniform_int_distribution<unsigned long> addr_dist(0,
                                                           FLASH_SPACE
                                                           / PAGE_SIZE);

    std::cout << "Benchmark - Uniformly Random Write:"          << std::endl
              << "  Intensity (#4K-Reqs/s)   Throughput (KB/s)" << std::endl;

    for (int intensity = INTENSITY_TICK; intensity <= MAX_INTENSITY;
         intensity += INTENSITY_TICK) {

        /** A round of benchmarking for given intensity. */
        uint64_t delta_us = 1000000 / intensity;
        uint64_t base_time_us, cur_time_us, log_interval_us = 0;
        std::vector<double> throughputs;

        base_time_us = get_cur_time_us();
        cur_time_us = base_time_us;
        log_interval_us = 0;

        do {
            struct req_entry entry;
            uint64_t new_time_us = get_cur_time_us();

            entry.direction = DIR_WRITE;
            entry.addr = PAGE_SIZE * addr_dist(rand_gen);;
            entry.size = PAGE_SIZE;
            entry.start_time_us = new_time_us;

            if (new_time_us - base_time_us >= 1000000)
                log_interval_us += new_time_us - cur_time_us;
            cur_time_us = new_time_us;

            if (log_interval_us > 100000) {
                throughputs.push_back(log_query_throughput(cur_time_us
                                                           - log_interval_us,
                                                           cur_time_us));
                log_interval_us = 0;
            }

            {
                std::lock_guard<std::mutex> lk(submit_queue_lock);

                submit_queue.push_back(entry);
                submit_queue_cv.notify_all();
            }

            usleep(delta_us);
        } while (cur_time_us < base_time_us + 1000000 * SECS_PER_ROUND);

        double avg_throughput = std::accumulate(throughputs.begin(),
                                                throughputs.end(),
                                                0.0) / throughputs.size();
        printf("  %20d     %15.5lf\n", intensity, avg_throughput);
        fflush(stdout);

        bench_clean_up();
    }
}


/**
 * Fill device with randomly written data.
 */
static void
bench_fill_device()
{
    unsigned long addr = 0;
    
    for (size_t i = 0; i < (FLASH_SPACE / PAGE_SIZE); ++i) {
        addr = i * PAGE_SIZE;
        _submit_write(addr, PAGE_SIZE, get_cur_time_us());
    }

    bench_clean_up();
}

/*========== Benchmarking Implemention BEGIN. ==========*/


int
main(int argc, char *argv[])
{
    if (argc != 2)
        error("please provide one argument: the socket file path");

    sock_name = argv[1];

    if (sizeof(struct req_header) != REQ_HEADER_LENGTH)
        error("request header length incorrect");

    gettimeofday(&boot_time, NULL);

    /** Open client socket & connect. */
    prepare_socket();

    bench_fill_device();

    /** Create the separate submission thread. */
    std::thread submit_thread(_submit_thread_func);
    submit_thread.detach();

    bench_seq_read();
    bench_rnd_read();
    bench_seq_write();
    bench_rnd_write();

    return 0;
}
