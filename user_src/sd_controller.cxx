#include <stdio.h>

#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <chrono>

#include <iostream>
#include <fstream>

#include "StellaCANRead.h"

#define PRINT_HEADER "[SD Controller] "

using namespace std::chrono;

extern bool terminate;
extern bool connected;

// How will it work:
// The SD controller will start writing into files and keep track of which file it is currently writing to
// Whenever a file is full it will continue to the next file
// If all files are filled, we return to the first file
// Whenever a file is opened for writing, ALL data in the file is overwritten -> emtpy file on open
// For the FTP Client we only need to know which files the SD Controller is currently NOT writing to and send those, starting from the file below the current file
// However, we need to ensure that these files actually have new data, so we need to mark files that are safe to be sent

// Considerations: 
// 1) If files are large (~4GB) it takes a long time to upload them -> 16 min, which in shitty places could mean files are never uploaded (fails several times)
//    Solution: smaller files -> problem: naming of files
// 2) Is it realistic that there will not be internet for periods LONGER than 8 hours, i.e. how often will the car be disconnected for over 8 hours
//    If very unlikely and client agrees, we don't need a circular file buffer.
//    Instead, we could make small files (~10MB) each indicated with msg_timestamp or whatever
//    Then when they are full, we indicate to the FTP Client that this file is ready for transfer
//    Meanwhile we create a new file and start writing there
//    The FTP Client then (if internet avail) will send this 10MB file in roughly 5s to the server and then delete the file when done
//    If no files can be sent, ever, then at some point no new file can be created/sd card is full
//    If the SD Card is sufficiently large, this is > 8h and hence no need to circulate

// MESSAGE BUFFER
std::mutex MUTEX_MESSAGE_BUFFER;
std::condition_variable MESSAGE_BUFFER_EMPTY;
std::condition_variable MESSAGE_BUFFER_FULL;
std::queue<std::string> message_buffer;
static std::unique_lock<std::mutex> msg_lock(MUTEX_MESSAGE_BUFFER);

// The file to which we are currently writing messages
static std::fstream msg_file;
// The name of the file we are currently writing messages to
static std::string cur_file;
// A counter that keeps track of how many messages have been written into the current file
static uint32_t msg_counter;

// Method declarations
static void write_message();

// A thread that manages the SD filesystem and writes/loads CAN messages to/from the SD
void sd_controller(){

    // We keep storing messages until terminate is true and no more messages are buffered
    while(!(terminate && message_buffer.empty())){
        // If the desired/maximum number of messages have been written to the file, switch to a new file
        if(msg_counter == MSG_PER_FILE){
            switch_file();
            msg_counter = 0;
        }

        // Write a message to the SD Card
        write_message();
    }
    // Close the current file

    msg_file.close();

    // Do some bookkeeping -> retain references to the files that still have to be sent
    // We don't need to keep track of where we were in the file that we just closed as we can simply
    // just pretend the file is full

    // Write the names/paths of already existing files to a file
    // Preferably this file is NOT located on the SD card but in flash or w/e memory is provided by Nuttx
    // This does mean that SD-cards can probably not be swapped (unless we do some safety magic) if there 
    // are files remaining (i.e. sdcard1 has 3 files ready for transfer, then swapping in sdcard1 which has 0 and running the program
    // might break)

}

// Writes a message from the message_buffer onto the SD-card, if such a message is available
static void write_message(){
    std::string msg;

    // Enter critical section
    // Lock the mutex
    msg_lock.lock();
    // Ensure there is a message to write in the queue
    while(message_buffer.empty()){
        MESSAGE_BUFFER_EMPTY.wait(msg_lock);
    }

    //If there is a message to write, retrieve it
    msg = message_buffer.front();
    message_buffer.pop();
    // Notify the Data Handler new messages can be written to the SD controller
    MESSAGE_BUFFER_FULL.notify_one();
    // Unlock the mutex
    msg_lock.unlock();
    // Exit critical section

    // We have retrieved a message from the queue that now has to be written to the SD card
    if(!(msg_file << msg << "\n")){
        // Something went wrong!
    }

    // Increase the message counter by 1 as a message has been written to the file
    msg_counter++;
}

// Switches the current file to a new file
static void switch_file(){
    // What it will do:
    // Close current file
    msg_file.close();
    // Mark current file as transmittable by the FTP Client == Add the cur_file string to a queue

    // Open a new unique file based on the current time -> Max 32 Bytes namelength (inc .txt)
    uint64_t timestamp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();

    char path[32];
    sprintf(path, MOUNT_PATH "/msgs_%llu.txt", timestamp);
    cur_file = path;
    msg_file.open(cur_file);
    // Return
}