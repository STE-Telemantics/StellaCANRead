#include "StellaCANRead.h"

#include <mutex>
#include <iostream>
#include <linux/can.h>
#include <condition_variable>
#include <queue>
#include <memory.h>
#include <poll.h>

using namespace std::chrono;

#define PRINT_HEADER "[DataHandler] "

// Access the variable that determines whether the thread needs to terminate
extern bool terminate;

// TCP Client
extern std::mutex MUTEX_DH_TCP_CLIENT;
extern std::condition_variable TCP_CLIENT_DISCONNECTED;
extern tcp_client data_handler_client;
static std::unique_lock<std::mutex> client_lock(MUTEX_DH_TCP_CLIENT, std::defer_lock);

// CAN FRAME BUFFER (from socket_reader)
extern std::mutex MUTEX_CAN_FRAME_BUFFER;
extern std::condition_variable CAN_FRAME_BUFFER_EMPTY;
extern std::condition_variable CAN_FRAME_BUFFER_FULL;
extern std::queue<struct can_frame> frame_buffer;
static std::unique_lock<std::mutex> can_lock(MUTEX_CAN_FRAME_BUFFER, std::defer_lock);

// MESSAGE BUFFER (from sd_controller)
extern std::mutex MUTEX_MESSAGE_BUFFER;
extern std::condition_variable MESSAGE_BUFFER_EMPTY;
extern std::condition_variable MESSAGE_BUFFER_FULL;
extern std::queue<std::string> message_buffer;
static std::unique_lock<std::mutex> msg_lock(MUTEX_MESSAGE_BUFFER, std::defer_lock);

// Declare static subfunctions

static int get_next_frame();
static void extract_message();
static void handle_message();
static void send_to_sd();

static struct can_frame frame;
static std::string msg;

// Set up a pollfd struct that allows us to poll the status of the socket prior to reading data
// and ensure we only write if we can write
static struct pollfd pfd;
// # of fd's handled by our poll
static int nfds = 1;

// A thread that retrieves can_frames stored in the frame_buffer by the SocketCAN Reader module and converts them into formatted strings.
// These formatted strings are then either sent to the server if an internet connection is available or stored on the SD card using the SD Controller module
void data_handler()
{
    // Clear the pollfd stuct of garbage
    memset(&pfd, '\0', sizeof(struct pollfd));
    pfd.events = POLLOUT; // Set the event to poll for equal to POLLOUT

    // We keep receiving and processing messages until terminate is true and no more frames are buffered
    while (!(terminate && frame_buffer.empty()))
    {
        // Load the next can frame into frame;
        if (get_next_frame() < 0)
        {
            break; // If the return value is 0, terminate is true and the buffer is emtpy, so break the while loop manually
        }

        // Convert the frame into a can message
        extract_message();

        // Then either send the message to the server or store it on the SD Card
        handle_message();
    }

    // There will be no more messages to process and all frames read by the SocketCAN Reader have been processed
    std::cout << PRINT_HEADER "Terminated Data Handler succesfully!" << std::endl;
}

// Retrieves the next can_frame from the frame_buffer and stores it into frame;
// Returns 0 if a frame was loaded, -1 if the buffer is empty and terminate is true
static int get_next_frame()
{
    // Enter critical section
    // Lock the mutex
    can_lock.lock();
    // Wait for the frame_buffer queue to be non-empty
    while (frame_buffer.empty())
    {
        // If we need to terminate and the frame_buffer is empty, signal to stop the thread
        if (terminate)
        {
            // Unlock the mutex
            can_lock.unlock(); // Ensure the mutex is unlocked
            // Exit critical section
            return -1;
        }

#if DEBUG_COND
        std::cout << PRINT_HEADER "Waiting for CAN buffer to be non-empty!" << std::endl;
#endif
        CAN_FRAME_BUFFER_EMPTY.wait_for(can_lock, COND_TIMEOUT); // Recheck condition every COND_TIMEOUT to avoid deadlock
    }

    // Receive the frame at the front of the queue
    frame = frame_buffer.front();
    // Remove the frame in the front from the queue
    frame_buffer.pop();
    // Signal the SocketCAN Reader that the queue is no longer full
    CAN_FRAME_BUFFER_FULL.notify_all();
    // Unlock the mutex
    can_lock.unlock();
    // Exit the critical section

    return 0;
}

// Extract data from the can_frame into a string which contains only the necessary data from the can_frame and adds a timestamp.
static void extract_message()
{
    // Create the approximate timestamp at which this CAN-message was received
    uint64_t timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

    // The actual id of the can_frame, which is either the first 11 or 29 bits based on spec
    uint32_t actual_id;

    // Check the CAN spec/version of this message, which is determined by the last bit of can_id
    if (frame.can_id & (1 << 31))
    { // If the 31st bit is set, then we have spec 2.0B = 29 bit id
        actual_id = frame.can_id & ((1 << 29) - 1);
    }
    else
    { // If the 31st bit is not set, then we have spec 2.0A = 11 bit id
        actual_id = frame.can_id & ((1 << 11) - 1);
    }

    // Convert the extracted data alongside the timestamp into a string
    char extract[48];
    // 10?13? + 1 + 8 + 1 + 16 = 36/9 bytes
    int nbytes = sprintf(extract, "car%i:%llu#%08lx#%02x%02x%02x%02x%02x%02x%02x%02x\n", CAR, timestamp, actual_id, frame.data[0],
                         frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6], frame.data[7]);

    msg = extract;

#if PRINT_MSG
    std::cout << msg;
#endif
}

static void handle_message()
{
    // If we already know we are not connected, just write it to the SD Card
    // Not mutex protected as we don't really care if we send a message to the SD card if we are
    // technically connected and mutex overhead is not worth the trouble
    if (!data_handler_client.connected)
    {
        send_to_sd();
        return;
    }

    // Otherwise we are connected
    // Enter critical section
    // Lock the mutex
    client_lock.lock();

    // Update the pfd.fd with the current sockfd, could have changed if we reconnected
    pfd.fd = data_handler_client.sockfd;

    // Check if data can be written to the TCP Client fd, timeout = 10ms
    int ready = poll(&pfd, nfds, 10);

    // Check the result of the poll to determine whether we are connected or not
    if (ready <= 0)
    {
        if (ready < 0)
        {
            // An error occured on poll, safe to assume we are no longer connected
            perror(PRINT_HEADER "Could not poll the TCP Client!");
        }

        // Otherwise, it timedout, safe to assume we are no longer connected
        data_handler_client.connected = false;

        // Signal user_src_main that the client has disconnected
        TCP_CLIENT_DISCONNECTED.notify_one();
        // Unlock the mutex
        client_lock.unlock();
        // Exit critical section

        // Send the message to the SD controller instead
        send_to_sd();
        return;
    }

    // We determined that we are almost certainly still connected
    // Now send the message to the server
    int totbytes = 0;

    // Ensure the entire message is sent
    while (totbytes < msg.length())
    {
        int nbytes = send(data_handler_client.sockfd, (msg.c_str() + totbytes), msg.length() - totbytes, 0);

        // Check if an error occured
        if (nbytes < 0)
        {
            perror(PRINT_HEADER "Could not send data to the server!");
            data_handler_client.connected = false; // Assume the connection dropped

            // Signal user_src_main that the client has disconnected
            TCP_CLIENT_DISCONNECTED.notify_one();
            // Unlock the mutex
            client_lock.unlock();
            // Exit critical section

            // Send the message to the SD Controller instead
            send_to_sd();
            return;
        }

        // Otherwise, add nbytes to totbytes
        totbytes += nbytes;
    }

    // The message was send succesfully
    // Unlock the mutex
    client_lock.unlock();
    // Exit critical section
}

static void send_to_sd()
{
    // Enter critical section
    // Lock the mutex
    msg_lock.lock();

    // Wait for the queue to be non-full
    while (message_buffer.size() >= QUEUE_SIZE)
    {
#if DEBUG_COND
        std::cout << PRINT_HEADER "Waiting for the msg buffer to be non-full!" << std::endl;
#endif
        MESSAGE_BUFFER_FULL.wait(msg_lock);
    }

    // Queue is no longer full -> Add the next message:
    message_buffer.push(msg);

    // Notify the SD Controller that at a message has become available
    MESSAGE_BUFFER_EMPTY.notify_one();

    // Unlock the mutex
    msg_lock.unlock();
    // Exit critical section
}