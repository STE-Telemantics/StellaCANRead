#include <mutex>
#include <iostream>
#include <nuttx/can.h>
#include <condition_variable>
#include <chrono>
#include <queue>

#include "StellaCANRead.h"

using namespace std::chrono;

#define PRINT_HEADER "[DataHandler] "

// Access the variable that determines whether the thread needs to terminate
extern bool terminate;
// If the thread is blocked on 

// SIO Client
extern bool connected;

// SOCKETIO
extern std::mutex MUTEX_SOCKET_IO;
extern std::condition_variable SOCKET_IO_CONNECTION;

// CAN FRAME BUFFER (from socket_reader)
extern std::mutex MUTEX_CAN_FRAME_BUFFER;
extern std::condition_variable CAN_FRAME_BUFFER_EMPTY;
extern std::condition_variable CAN_FRAME_BUFFER_FULL;
extern std::queue<struct can_frame> frame_buffer;
static std::unique_lock<std::mutex> can_lock(MUTEX_CAN_FRAME_BUFFER);

// MESSAGE BUFFER (from sd_controller)
extern std::mutex MUTEX_MESSAGE_BUFFER;
extern std::condition_variable MESSAGE_BUFFER_EMPTY;
extern std::condition_variable MESSAGE_BUFFER_FULL;
extern std::queue<std::string> message_buffer;
static std::unique_lock<std::mutex> msg_lock(MUTEX_MESSAGE_BUFFER);

// Declare static subfunctions

static void get_next_frame();
static void extract_message();
static void handle_message();

static struct can_frame frame;
static std::string msg;

// A thread that retrieves can_frames stored in the frame_buffer by the SocketCAN Reader module and converts them into formatted strings.
// These formatted strings are then either sent to the server if an internet connection is available or stored on the SD card using the SD Controller module
void data_handler()
{
    // We keep receiving and processing messages until terminate is true and no more frames are buffered
    while (!(terminate && frame_buffer.empty()))
    {
        // Load the next can frame into frame;
        get_next_frame();

        // Convert the frame into a can message
        extract_message();

        // Then either send the message to the server or store it on the SD Card
        handle_message();
    }

    // There will be no more messages to process and all frames read by the SocketCAN Reader have been processed

    printf(PRINT_HEADER "Terminated DataHandler successfully!");
}

// Retrieves the next can_frame from the frame_buffer and stores it into frame
static void get_next_frame()
{
    // Enter critical section
    // Lock the mutex
    can_lock.lock();
    // Wait for the frame_buffer queue to be non-empty
    while (frame_buffer.empty())
    {
        if(terminate){ // If we need to terminate and the frame_buffer is empty, stop the thread
            return;
        }
        CAN_FRAME_BUFFER_EMPTY.wait(can_lock); // What if we were already waiting and then terminate becomes true and no more frames come in.
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
}

// Extract data from the can_frame into a string which contains only the necessary data from the can_frame and adds a timestamp.
static void extract_message()
{
    // Create the approximate timestamp at which this CAN-message was received
    uint64_t timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();

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

    char test[] = "%lx"; // ??

    // Convert the extracted data alongside the timestamp into a string
    char extract[31];
    // 10?13? + 1 + 8 + 1 + 16 = 36/9 bytes
    int nbytes = sprintf(extract, "%llu#%08lx#%x%x%x%x%x%x%x%x", timestamp, frame.can_id, frame.data[0], 
    frame.data[1], frame.data[2], frame.data[3], frame.data[4], frame.data[5], frame.data[6], frame.data[7]);

    if(nbytes != 40){
        printf(PRINT_HEADER "Warning: Extracted can_frame not equal to the expected 40 bytes!");
    }

    msg = extract;
}

static void handle_message()
{
    // Now send the message to the server if there is a connection
    if(connected){
        std::cout << msg << std::endl; // for now just print to the console
    }else{ // Otherwise, send it to the SD Controller, which will write it on SD Card
        // Enter critical section
        // Lock the mutex
        msg_lock.lock();

        // Wait for the queue to be non-full
        while(message_buffer.size() >= QUEUE_SIZE){
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
}