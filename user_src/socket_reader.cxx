#include "StellaCANRead.h"

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <net/if.h>
#include <memory.h>
#include <poll.h>
#include <linux/can.h>
#include <queue>
#include <arpa/inet.h>

#define PRINT_HEADER "[SocketCANReader] "

using namespace std::chrono_literals;

// Access the variable that determines whether the thread needs to terminate
extern bool terminate;

// CAN FRAME BUFFER
std::mutex MUTEX_CAN_FRAME_BUFFER;
std::condition_variable CAN_FRAME_BUFFER_EMPTY;
std::condition_variable CAN_FRAME_BUFFER_FULL;
std::queue<struct can_frame> frame_buffer;

// Variables related to SocketCAN
static int socket_id;

// Declaration of static functions
static int init_socket();

void socket_can_reader()
{
    // Initialize the socket connection
    if (init_socket() < 0)
    {
        // If the connection could not be established, error and stop the thread
        std::cout << PRINT_HEADER "Could not initialize the socket connection!" << std::endl;
        return;
    }

    int nbytes;
    can_frame frame;

    // Set up a pollfd struct that allows us to poll the status of the socket prior to reading data
    // and ensure we only try to read if there is data to read
    struct pollfd pfd;
    memset(&pfd, '\0', sizeof(struct pollfd)); // Clear it of garbage
    int nfds = 1;                              // # of fd's handled by our poll
    pfd.fd = socket_id;                        // Set the fd to be polled equal to our socket fd
    pfd.events = POLLIN;                       // Set the event to poll for equal to POLLIN

    // Create the lock, but don't lock immediately
    std::unique_lock<std::mutex> m_lock(MUTEX_CAN_FRAME_BUFFER, std::defer_lock);

    // We keep running until we receive a request to terminate the program
    while (!terminate)
    {
        int ready = poll(&pfd, nfds, 10);

        if (ready < 0)
        {
            // An error occured
            perror(PRINT_HEADER "Could not poll the CAN bus!");
            continue;
        }
        else if (ready == 0)
        {
            // The poll timed out, recheck the condition of the while loop
            continue;
        }

        ssize_t totbytes = 0;
        // Verify that the POLLIN event happened for this fd
        if (pfd.revents & POLLIN)
        {
            // Read until the entire can_frame was filled
            while (totbytes < sizeof(struct can_frame))
            {
                // If so, read data from the socket and store it in frame
                nbytes = read(socket_id, &frame + totbytes, sizeof(struct can_frame) - totbytes); // Can block here if no data is received
                if (nbytes < 0)
                {
                    perror(PRINT_HEADER "read failed!");
                    return;
                }
                totbytes += nbytes;
            }
        }
        else
        {
            // Otherwise, no data was read from the CAN bus, so no data has to be added to the buffer
            continue;
        }

        // The can_frame is received, now send it to the Data Handler
        // Enter the critical section
        // Lock the mutex
        m_lock.lock();

        // wait until frame_buffer is not full OR we are terminating
        while (frame_buffer.size() >= QUEUE_SIZE && !terminate)
        {
#if DEBUG_COND
            std::cout << PRINT_HEADER "Waiting for the CAN buffer to be non-full" << std::endl;
#endif
            CAN_FRAME_BUFFER_FULL.wait_for(m_lock, COND_TIMEOUT); // Recheck condition every CONT_TIMEOUT to avoid deadlock
        }

        // It is no longer full OR we are terminating, so add (a copy of) the frame to the queue
        frame_buffer.push(frame);
        // Signal waiters (Data Handler) that the frame_buffer queue is no longer empty
        CAN_FRAME_BUFFER_EMPTY.notify_one();
        // Unlock the mutex
        m_lock.unlock();
        // Exit critical section
    }

    // We have received a request to terminate, hence cleanup any open sockets
    if (close(socket_id) < 0)
    {
        perror(PRINT_HEADER "CAN socket could not be closed!");
    }

    std::cout << PRINT_HEADER "Terminated SocketCANReader successfully!" << std::endl;
}

// Initialize a socket connection to the CAN interface/driver
// Implementation according to https://www.beyondlogic.org/example-c-socketcan-code/
// Returns 0 if initialization was successful, -1 otherwise
static int init_socket()
{
    // Create a socket object for the CAN interface and gain its id
    if ((socket_id = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
    {
        perror(PRINT_HEADER "CAN socket could not be opened!");
        return -1;
    }

    // Create an interface request object;
    struct ifreq ifr;

    memset(&ifr, 0, sizeof(ifreq)); // Remove all `garbage' from the struct

    // Get the interface index associated with CAN_IF
    strncpy(ifr.ifr_name, CAN_IF, sizeof(CAN_IF));
    if (ioctl(socket_id, SIOCGIFINDEX, &ifr) < 0)
    {
        perror(PRINT_HEADER "Could not find the if-index associated with can!");
        return -1;
    }

    // Create a socket address that connects to the can interface
    struct sockaddr_can socket_addr;

    memset(&socket_addr, 0, sizeof(sockaddr_can)); // Empty the struct of garbage

    socket_addr.can_family = AF_CAN;
    socket_addr.can_ifindex = ifr.ifr_ifindex;

    // Bind a socket address to the socket
    if (bind(socket_id, (struct sockaddr *)&socket_addr, sizeof(socket_addr)) < 0)
    {
        perror(PRINT_HEADER "binding of the CAN socket failed!");
        return -1;
    }

    // Socket initialization was successful
    return 0;
}