/**
 * Standalone FlashSim simulator.
 *
 * Made this standalone version to enable non-C++ projects to interact with
 * multiple simulated flash SSDs interactively.
 *
 * Author: Guanzhou Hu <guanzhou.hu@wisc.edu>, 2020.
 */


#include <string>
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

#include "ssd.h"

using namespace ssd;


/** Global handle & variables. */
static Ssd *ssd_handle;
static std::string sock_name;
static int ssock;


/**
 * Helper functions.
 */
static void
clean_up(int signal)
{
    std::cout << "Caught signal " << signal << std::endl;

    if (ssock >= 0)
        close(ssock);

    if (!sock_name.empty())
        unlink(sock_name.c_str());

    if (ssd_handle != NULL)
        delete ssd_handle;

    std::cout << "SSD simulator KILLED" << std::endl;
    exit(1);
}

static void
usage()
{
    std::cout << "Usage: ./flashsim SOCK_NAME [CONFIG_FILE]" << std::endl;
    exit(1);
}

static void
error(std::string msg)
{
    std::cerr << "ERROR: " << msg << std::endl;
    clean_up(SIGINT);
}


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
// Reqeust header message should exactly match this size.

static const int DIR_READ  = 0;
static const int DIR_WRITE = 1;


/**
 * Process a write request.
 * MUST ensure that:
 *   - `addr` is aligned to pages
 *   - `size` is a multiple of pages
 *   - `buf` is a buffer of at least that number of pages large,
 *           or NULL if not passing actual data
 */
static double
process_write(ulong addr, uint size, void *buf, double start_time_ms)
{
    double time_used_ms;

    if (PAGE_ENABLE_DATA) {
        time_used_ms = ssd_handle->event_arrive(WRITE, addr / PAGE_SIZE,
                                                size / PAGE_SIZE,
                                                start_time_ms, buf);
    } else {
        time_used_ms = ssd_handle->event_arrive(WRITE, addr / PAGE_SIZE,
                                                size / PAGE_SIZE,
                                                start_time_ms, NULL);
    }

    // printf("WR: addr %lu of size %u @ %.3lf ... %.10lf\n", addr, size,
    //        start_time_ms, time_used_ms);
    return time_used_ms;
}

/**
 * Process a read request.
 * MUST ensure that:
 *   - `addr` is aligned to pages
 *   - `size` is a multiple of pages
 * Result should be reached through `Ssd::get_result_buffer()` if passing
 * actual data.
 */
static double
process_read(ulong addr, uint size, double start_time_ms)
{
    double time_used_ms;

    time_used_ms = ssd_handle->event_arrive(READ, addr / PAGE_SIZE,
                                            size / PAGE_SIZE,
                                            start_time_ms, NULL);

    // printf("RD: addr %lu of size %u @ %.3lf ... %.10lf\n", addr, size,
    //        start_time_ms, time_used_ms);
    return time_used_ms;
}


/**
 * Open a server-side socket for clients to make requests.
 * We only allow one client connection at a time.
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

    ret = bind(ssock, (struct sockaddr *) &saddr, sizeof(saddr));
    if (ret)
        error("bind() failed");

    ret = listen(ssock, 1);
    if (ret)
        error("listen() failed");

    std::cout << "Listening on local socket file `" << sock_name << "`..."
              << std::endl;
}


/**
 * An infinite loop listening on incoming requests through a client
 * connection.
 */
static void
request_loop(int csock)
{
    while (1) {
        char buf[REQ_HEADER_LENGTH];
        int rbytes, wbytes;

        /** Read request header message. */
        bzero(buf, sizeof(buf));
        rbytes = read(csock, buf, REQ_HEADER_LENGTH);

        if (rbytes == 0) {
            break;
        } else if (rbytes != REQ_HEADER_LENGTH) {
            error("request header wrong length");
        } else {
            struct req_header *header = (struct req_header *) buf;
            void *data = NULL, *resp_data;
            uint remainder, size;
            double start_time_ms, time_used_ms;
            unsigned long time_used_us;

            if (header->size <= 0)
                error("request header invalid size");

            if ((header->addr % PAGE_SIZE) != 0)
                error("request unaligned logical address");

            /**
             * Valid request header received.
             * We create a data buffer of size aligned to pages, since
             * this is required by the SSD device.
             */
            remainder = header->size % PAGE_SIZE;
            size = remainder == 0 ? header->size
                                  : header->size + PAGE_SIZE - remainder;
            start_time_ms = ((double) header->start_time_us) / 1000.0;

            /**
             * If READ, after processing the request, data read from
             * device can be accessed through `Ssd::get_result_buffer()`.
             * We will then send back to client a packet of
             * `header->size` length containing data the client wants,
             * followed a packet of length 8 containing `time_used_ms`
             * as double.
             */
            if (header->direction == DIR_READ) {
                time_used_ms = process_read(header->addr, size,
                                            start_time_ms);

                if (PAGE_ENABLE_DATA) {
                    resp_data = malloc(header->size);
                    memcpy(resp_data, ssd_handle->get_result_buffer(),
                           header->size);

                    wbytes = write(csock, resp_data, header->size);
                    if (wbytes != (int) header->size)
                        error("respond data to read failed");

                    free(resp_data);
                }
            
            /**
             * If WRITE, we expect the next message from client to be a
             * packet of length exactly `header->size` containing the
             * data to write. We will then send back to client a packet
             * of length 8 containing `time_used_ms` as double.
             */
            } else {
                if (PAGE_ENABLE_DATA) {
                    data = malloc(size);
                    bzero(data, sizeof(data));

                    rbytes = read(csock, data, header->size);
                    if (rbytes != (int) header->size)
                        error("client data to write wrong length");
                }

                time_used_ms = process_write(header->addr, size, data,
                                             start_time_ms);

                if (PAGE_ENABLE_DATA)
                    free(data);
            }

            /** Send back processing time response. */
            if (time_used_ms <= 0)
                error("negative processing time");

            time_used_us = (unsigned long) (time_used_ms * 1000);

            wbytes = write(csock, &time_used_us, 8);
            if (wbytes != 8)
                error("send back processing time failed");
        }
    }
}


int
main(int argc, char *argv[])
{
    struct sigaction sigint_handler;

    if (argc != 2 && argc != 3)
        usage();

    sock_name = argv[1];

    if (argc == 2)
        load_config();
    else
        load_config(argv[2]);

    /** Check that request header struct compiles to correct size. */
    if (sizeof(struct req_header) != REQ_HEADER_LENGTH)
        error("request header length incorrectly compiled");

    std::cout << "=== SSD Device Configuration ===" << std::endl;
    print_config(NULL);
    std::cout << "=== SSD Device Configuration ===" << std::endl << std::endl;

    std::cout << "=== Create New SSD Simulator ===" << std::endl;
    ssd_handle = new Ssd();
    std::cout << "=== Create New SSD Simulator ===" << std::endl << std::endl;

    /** Open server socket, bind, & listen. */
    prepare_socket();
    std::cout << "SSD simulator BOOTED" << std::endl;

    /** Register Ctrl+C handler. */
    sigint_handler.sa_handler = clean_up;
    sigemptyset(&sigint_handler.sa_mask);
    sigint_handler.sa_flags = 0;
    sigaction(SIGINT, &sigint_handler, NULL);

    /**
     * Wait for client connection. If running correctly, should only have
     * one client connecting and this connection should never fail.
     */
    while (1) {
        int csock = accept(ssock, NULL, NULL);

        if (csock < 0)
            error("accept() failed");
        else {
            std::cout << "New connection ACCEPTED" << std::endl;
            request_loop(csock);
            std::cout << "Client connection ENDED" << std::endl;
        }

        close(csock);
    }

    // Not reached.
    return 0;
}
