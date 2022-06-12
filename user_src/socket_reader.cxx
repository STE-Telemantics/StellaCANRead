#include "StellaCANRead.h"

#include <iostream>
#include <mutex>
#include <condition_variable>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <netpacket/can.h>
#include <nuttx/can.h>
#include <queue>

#define PRINT_HEADER "[SocketCANReader] "

// Access the variable that determines whether the thread needs to terminate
extern bool terminate;
// Currently does not terminate if: blocked on read as there is no external signal that can wake it up outside of a can_frame becoming available
// Other cases: a) thread blocked on QUEUE_FULL cond_variable -> data_handler will always empty queue before terminating -> unblocks -> go to b)
//              b) thread not blocked -> while !terminate loop will catch terminate and terminate.

// CAN FRAME BUFFER
std::mutex MUTEX_CAN_FRAME_BUFFER;
std::condition_variable CAN_FRAME_BUFFER_EMPTY;
std::condition_variable CAN_FRAME_BUFFER_FULL;
std::queue<struct can_frame> frame_buffer;

// Variables related to SocketCAN
static int socket_id;

static int init_socket();

void socket_can_reader()
{
    // Initialize the socket connection
    if(init_socket() < 0){
        // If the connection could not be established, error and stop the thread
        std::cout << PRINT_HEADER "Could not initialize the socket connection!" << std::endl;
        return;
    }

    int nbytes;
    can_frame frame;

    std::unique_lock<std::mutex> m_lock;
    // We keep running until we receive a request to terminate the program
    while (!terminate)
    {
        // Read data from the socket and store it in frame
        nbytes = read(socket_id, &frame, sizeof(struct can_frame));

        if(nbytes < 0){
            perror(PRINT_HEADER "read failed!");
            return;
        }

        // The can_frame is received, now send it to the Data Handler
        // Enter the critical section
        // Lock the mutex
        m_lock.lock();

        // wait until frame_buffer is not full
        while(frame_buffer.size() >= QUEUE_SIZE){
            CAN_FRAME_BUFFER_FULL.wait(m_lock);
        }

        // It is no longer full OR we are terminating, so add (a copy of) the frame to the queue
        frame_buffer.push(frame);
        printf("CAN message received: {id: %03lx, data: %x%x%x%x%x%x%x%x}", frame.can_id, frame.data[0], frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
        // Signal waiters (Data Handler) that the frame_buffer queue is no longer empty
        CAN_FRAME_BUFFER_EMPTY.notify_one();
        // Unlock the mutex
        m_lock.unlock();
        // Exit critical section
    }

    // We have received a request to terminate, hence cleanup any open sockets
    if(close(socket_id) < 0){
        perror(PRINT_HEADER "CAN socket could not be closed!");
    }

    printf(PRINT_HEADER "Terminated SocketCANReader successfully!");
}

// Initialize a socket connection to the CAN interface/driver
// Implementation according to https://www.beyondlogic.org/example-c-socketcan-code/
// Returns 0 if initialization was successful, -1 otherwise
static int init_socket(){
    // Create a socket object for the CAN interface and gain its id
    if((socket_id = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0){
        perror(PRINT_HEADER "CAN socket could not be opened!");
        return -1;
    }

    // Create an interface request object;
    struct ifreq ifr;

    // Get the interface index associated with can1
    strncpy(ifr.ifr_name, "can1", 4);
    ifr.ifr_name[4] = '\0'; // Add terminator
    if(ioctl(socket_id, SIOCGIFINDEX, &ifr) < 0){
        perror(PRINT_HEADER "Could not find the if-index associated with can!");
        return -1;
    }

    // Update the flags to indicate the interface should be started
    ifr.ifr_ifru.ifru_flags |= IFF_UP;

    // Enable the interface
    if(ioctl(socket_id, SIOCSIFFLAGS, &ifr) < 0){
        perror(PRINT_HEADER "Enabling interface failed!");
        return -1;
    }

    // Create a socket address that connects to the can1 interface
    struct sockaddr_can socket_addr;

    socket_addr.can_family = AF_CAN;
    socket_addr.can_ifindex = ifr.ifr_ifindex;


    // Bind a socket address to the socket
    if(bind(socket_id, (struct sockaddr *)&socket_addr, sizeof(socket_addr)) < 0){
        perror(PRINT_HEADER "binding of the CAN socket failed!");
        return -1;
    }

    // Socket initialization was successful
    return 0;
}