#ifndef STELLA_CAN_READ_H
#define STELLA_CAN_READ_H

#include <sys/types.h>
#include <netinet/in.h>
#include <string>
#include <chrono>

using namespace std::chrono;

// Which car is sending the data, can be 1, 2 or 3
#define CAR 1

// TCP Server IP and port to which we will send data
#define TCP_IP "131.155.198.63"
#define TCP_PORT 5000

#define QUEUE_SIZE 256
#define MSG_PER_FILE 250000
#define MOUNT_PATH "test"

// Debug symbol that defines whether the status of condition variables is printed
#define DEBUG_COND 0

// Whether the recieved messages should be printed
#define PRINT_MSG 0

// The timeout in ms for certain Condition variables to wait until the condition is rechecked
// This timeout is required in certain cases to allow the program to terminate when terminate is true and avoid deadlock
#define COND_TIMEOUT 1000ms

// The timeout in ms for the CAN Socket poll() to timeout, this timeout ensures the program can terminate properly
// in case the socket module is trying to read data from the CAN bus 
#define POLL_TIMEOUT 1000

// The delay in seconds between reconnection attemtps in case the TCP client is not connected
#define RECON_DELAY 4

// Whether to use a timer to exit the program; If false, the program will never terminate by itself
#define USE_TIMER 1
// The duration after which the timer stops the program
#define PROG_DUR 3000

// Enable TCP No Delay to disable Nagle's algorithm and send messages ASAP instead waiting and grouping them into larger frames
// Could create congestion due to the increase in packets sent
#define USE_TCP_NODELAY 0

class tcp_client
{
    // Properties
private:
    std::string m_host;
    uint16_t m_port;

    struct sockaddr_in m_servaddr;

public:
    int sockfd;
    bool connected;

    //  Functions
private:
public:
    tcp_client(std::string host, uint16_t port)
    {
        m_host = host;
        m_port = port;
        connected = false;
    }

    tcp_client(){}

    ~tcp_client();

    void init();

    int open_con();

    int reconnect();

    void close_con();
};

// Define thread functions

void data_handler();
void socket_can_reader();
void sd_controller();
void ft_client();

#endif