/**
 * Standalone FlashSim simulator client example.
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

    /** Send a write request. */
    {
        struct req_header header;
        int rbytes, wbytes;
        char data[17] = "String-of-len-16";
        char time_used_buf[8];
        double time_used;

        // Request header.
        header.direction = DIR_WRITE;
        header.addr = 8192;
        header.size = 17;
        header.start_time = 3200.28;

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

        time_used = *((double *) time_used_buf);
        printf("Written \"%s\" to SSD, took %.10lf ms\n",
               data, time_used);
    }

    /** Send a read request to read that out. */
    {
        struct req_header header;
        int rbytes, wbytes;
        char data[17];
        char time_used_buf[8];
        double time_used;

        // Request header.
        header.direction = DIR_READ;
        header.addr = 8192;
        header.size = 17;
        header.start_time = 3471.32;

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

        time_used = *((double *) time_used_buf);
        printf("Read \"%s\" from SSD, took %.10lf ms\n",
               data, time_used);
    }
}
