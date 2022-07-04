#include "StellaCANRead.h"

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <cstring>

#define PRINT_HEADER "[SD Controller] "

using namespace std::chrono;

// Access the variable that determines whether the thread needs to terminate
extern bool terminate;

// TCP CLIENT
extern tcp_client ft_tcp_client;
extern std::mutex MUTEX_FT_TCP_CLIENT;
static std::unique_lock<std::mutex> client_lock(MUTEX_FT_TCP_CLIENT, std::defer_lock);

// MESSAGE BUFFER
std::mutex MUTEX_MESSAGE_BUFFER;
std::condition_variable MESSAGE_BUFFER_EMPTY;
std::condition_variable MESSAGE_BUFFER_FULL;
std::queue<std::string> message_buffer;
static std::unique_lock<std::mutex> msg_lock(MUTEX_MESSAGE_BUFFER, std::defer_lock);

// FILE STUFF
// The name of the file we are currently writing messages to
std::string cur_file;
std::mutex MUTEX_CUR_FILE; // A mutex to protect the cur_file variable
extern std::condition_variable FILE_TO_SEND;
static std::unique_lock<std::mutex> cur_lock(MUTEX_CUR_FILE, std::defer_lock);

// The file to which we are currently writing messages
static std::fstream msg_file;

// A counter that keeps track of how many messages have been written into the current file
static uint32_t msg_counter;

// Function declarations
static int create_file();
static int switch_file();
static void write_message();

// A thread that manages the SD filesystem and writes/loads CAN messages to/from the SD
void sd_controller()
{
    // First ensure the destination directory exists
    if (!std::filesystem::exists(MOUNT_PATH) || !std::filesystem::is_directory(MOUNT_PATH))
    {
        if (!std::filesystem::create_directory(MOUNT_PATH))
        {
            std::cout << PRINT_HEADER "Could not create the directory " << MOUNT_PATH << ": " << std::strerror(errno) << std::endl;
            return;
        }
    }

    // Then create a file where we can write messages to
    if (create_file() < 0)
    {
        std::cout << PRINT_HEADER "Could not create and open the file " << cur_file << " : " << std::strerror(errno) << std::endl;
        return;
    }

    // We keep storing messages until terminate is true and no more messages are buffered
    while (!(terminate && message_buffer.empty()))
    {
        // If the desired/maximum number of messages have been written to the file, switch to a new file
        if (msg_counter == MSG_PER_FILE)
        {
            if (switch_file() < 0)
            {
                std::cout << PRINT_HEADER "Could switch to the file " << cur_file << " : " << std::strerror(errno) << std::endl;
                return;
            }

            msg_counter = 0;
        }

        // Write a message to the SD Card
        write_message();
    }

    // Close the current file
    msg_file.close();

    // Mark the current file as released to the FT Client
    // Enter critical section
    // Lock the mutex
    cur_lock.lock();
    cur_file = "test/";
    FILE_TO_SEND.notify_one();
    // Unlock the mutex
    cur_lock.unlock();
    // Exit critical section

    std::cout << PRINT_HEADER "Terminated SD Controller successfully!" << std::endl;
}

// Creates a new file and opens it
static int create_file()
{
    // Open a new unique file based on the current time -> Max 32 Bytes namelength (inc .txt extension)
    uint64_t timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();

    // Enter critical section
    // Lock the mutex
    cur_lock.lock();

    // Modify the cur_file variable to a new file
    char path[32];
    sprintf(path, MOUNT_PATH "/msgs_%llu.txt", timestamp);
    cur_file = path;

    // Create and open the file
    msg_file.open(cur_file, std::ios::out);

    // Notify the ft client that cur_file has changed and hence potentially a new file can be sent to the server
    FILE_TO_SEND.notify_one();

    if (!msg_file)
    {
        // Unlock the mutex
        cur_lock.unlock();
        // Exit the critical section
        return -1;
    }

    // Unlock the mutex
    cur_lock.unlock();
    // Exit the critical section
    return 0;
}

// Switches the current file to a new file
static int switch_file()
{
    // Close current file
    msg_file.flush();
    msg_file.close();

    // Create and open a new file
    return create_file();
}

// Writes a message from the message_buffer onto the SD-card, if such a message is available
static void write_message()
{
    std::string msg;

    // Enter critical section
    // Lock the mutex
    msg_lock.lock();

    // Ensure there is a message to write in the queue
    while (message_buffer.empty())
    {
        if (terminate)
        {
            // Unlock the mutex
            msg_lock.unlock();
            // Exit critical section
            return;
        }

#if DEBUG_COND
        std::cout << PRINT_HEADER "Waiting for message buffer to be non-empty!" << std::endl;
#endif
        // Avoid deadlock if no messages are being written to the SD Controller but terminate is true
        MESSAGE_BUFFER_EMPTY.wait_for(msg_lock, COND_TIMEOUT); // Recheck every CONT_TIMEOUT ms to avoid deadlock
    }

    // If there is a message to write, retrieve it
    msg = message_buffer.front();
    message_buffer.pop();
    // Notify the Data Handler new messages can be written to the SD controller
    MESSAGE_BUFFER_FULL.notify_one();
    // Unlock the mutex
    msg_lock.unlock();
    // Exit critical section

    // We have retrieved a message from the queue that now has to be written to the SD card
    if (!(msg_file << msg))
    {
        perror("Could not write to file!");
        return;
    }

    // Flush the file, could be omitted to improve efficiency
    msg_file.flush();

    // Increase the message counter by 1 as a message has been written to the file
    msg_counter++;
}