#include "StellaCANRead.h"

#include <mutex>
#include <condition_variable>
#include <string.h>

#include <memory.h>
#include <poll.h>

#include <iostream>
#include <fstream>
#include <filesystem>

#define PRINT_HEADER "[FT_Client] "

// Access the variable that determines whether the thread needs to terminate
extern bool terminate;

// TCP Client
extern std::mutex MUTEX_FT_TCP_CLIENT;
extern std::condition_variable TCP_CLIENT_DISCONNECTED;
extern std::condition_variable TCP_CLIENT_CONNECTED;
extern tcp_client ft_tcp_client;
static std::unique_lock<std::mutex> client_lock(MUTEX_FT_TCP_CLIENT, std::defer_lock);

// File stuff
extern std::string cur_file;      // The file the SD Controller is currently writing to
extern std::mutex MUTEX_CUR_FILE; // Mutex to protect the cur_file variable
std::condition_variable FILE_TO_SEND;
static std::unique_lock<std::mutex> cur_lock(MUTEX_CUR_FILE, std::defer_lock);

// The file we are currently transmitting
static std::fstream msg_file;
// The path of the file we are currently transmitting
static std::string msg_file_path;

// The pointer to the previous line in the current file
static std::streampos prev;

// A file that contains a reference to the last read file and position in that file
// from the previous run
static std::fstream ptr_file;

// Setup socket polling
static struct pollfd pfd;
static int nfds = 1;

static std::string msg;

// Static function declarations
static void open_prev();
static int open_next();
static int send_msg();

// A thread that will upload the CAN data stored on the SD card to the server
void ft_client()
{
    // Continue with the last file, if any otherwise wait for the next file to become availables
    open_prev();

    // Clear the pollfd stuct of garbage
    memset(&pfd, '\0', sizeof(struct pollfd));
    pfd.events = POLLOUT; // Set the event to poll for equal to POLLOUT

    // We keep trying to send files until we are not able to open a new file OR no data can be sent
    int open_result = 0;
    int send_result = 0;
    while (open_result == 0 && send_result >= 0)
    {
        prev = msg_file.tellg(); // Get the read pointer prior to trying to read the next line

        // First select a message from the file to be selected:
        if (!std::getline(msg_file, msg))
        {
            // If no message could be selected we reached EOF, open another file
            open_result = open_next();
            continue; // Verify a file was opened
        }

        // Add a \n to the message
        msg.append("\n");

        // A message was received now send it
        send_result = send_msg(); // Only < 0 if conn == false and terminate == true
    }

    // If we terminate and msg_file is still open (== we were reading it and no conn and terminate)
    if (msg_file.is_open())
    {
        // Save a reference to the file we are currently reading
        // Open the ptr_file as write
        ptr_file.open(MOUNT_PATH "/last.txt", std::ios::out);

        // Set the write pointer to 0
        ptr_file.seekp(0, std::ios::beg);

        std::streamoff offset = msg_file.tellg();

        // Write the name of the file that was open
        ptr_file << msg_file_path << "\n";
        // And write the position at which we were reading
        ptr_file << offset << "\n";

        // Close the msg file
        msg_file.close();

        ptr_file.flush();

        // Close the ptr file
        ptr_file.close();
    }
    else
    {
        // Otherwise clear the file to prevent FT Client to attempt to read from non-existent file on the next run
        ptr_file.open(MOUNT_PATH "/last.txt", std::ios::out | std::ios::trunc);
        ptr_file << "\0";
        ptr_file.close();
    }

    std::cout << PRINT_HEADER "FT CLient terminated succesfully" << std::endl;
}

// Tries to continue to read from the file we were reading last run, if any
static void open_prev()
{
    // Open the file in which the file name and read pointer are stored
    ptr_file.open(MOUNT_PATH "/last.txt", std::ios::in);

    std::string path;
    std::string offset_str;
    std::streamoff offset;

    // Try to get the path of the last opened file
    if (!std::getline(ptr_file, path))
    {
        // If this file does not exist, open a new file instead
        ptr_file.close();
        open_next();
        return;
    }

    // Try to read the offset from the last opened file
    if (!std::getline(ptr_file, offset_str))
    {
        offset = 0;
    }
    else
    {
        try
        {
            offset = std::stol(offset_str);
        }
        catch (const std::invalid_argument &e)
        {
            std::cout << PRINT_HEADER "Could not convert the streamoff offset!\n";
            offset = 0;
        }
    }

    // Now open the last opened file for reading and continue at offset
    msg_file.open(path, std::ios::in);
    msg_file.seekg(offset, std::ios::beg);
    ptr_file.close();
}

static int open_next()
{

    // First close the current file, if it is still open
    if (msg_file.is_open())
    {
        msg_file.close();
    }

    // Then delete the current file only if there is a current file
    if (msg_file_path.compare("") != 0)
    {
#if DEBUG_COND
        std::cout << "Removing file..." << std::endl;
#endif
        if (std::remove(msg_file_path.c_str()) < 0)
        {
            perror(PRINT_HEADER "Could not delete a file");
        }
    }

    // Now check before opening the next file, if we have to terminate
    if (terminate)
    {
        return -1; // Return -1, indicating that no new file was openeds
    }

    // Enter critical section
    // Lock the mutex
    cur_lock.lock();

    // Keep iterating until the next file is opened
    while (!msg_file.is_open())
    {

        // Get the name of the file that the SD Controller is currently writing to -> remove MOUNT_PATH + /
        std::string cur_file_name = cur_file.substr(sizeof(MOUNT_PATH));

        // Iterate over all the files in the MOUNT_PATH directory -> the directory in which msg files are written
        for (const auto &entry : std::filesystem::directory_iterator(MOUNT_PATH))
        {
            // Get the name of the file
            std::string filenameStr = entry.path().filename().string();

            // If the file is a regular file and not cur_file or the last.txt file
            if (entry.is_regular_file() && (filenameStr.compare(cur_file_name) != 0) && (filenameStr.compare("last.txt") != 0))
            {
                // Open the file and return
                msg_file_path = MOUNT_PATH "/";
                msg_file_path.append(filenameStr);

                msg_file.open(msg_file_path);

#if DEBUG_COND
                std::cout << PRINT_HEADER << "Started reading from the next file: " << msg_file_path << std::endl;
#endif

                // Unlock the mutex
                cur_lock.unlock();
                // Exit critical section
                return 0; // Indicate that we opened a new file
            }
        }

#if DEBUG_COND
        std::cout << "Waiting for next file..." << std::endl;
#endif

        // Otherwise no such file could be found, wait until there is a new file completed by the SD Controller
        // Or we timeout to prevent deadlock if terminate is true
        FILE_TO_SEND.wait(cur_lock);
    }
}

// Send a message to the server using the FT TCP client
static int send_msg()
{
    // Enter the critical section
    // Lock the mutex
    client_lock.lock();

    // Wait for an internet connection to become available if neccessary
    while (!ft_tcp_client.connected)
    {
        if (terminate)
        {
            // Unlock the mutex
            client_lock.unlock();
            // Exit critical section

            // We were unable to send this particular message
            // So decrease the read pointer by one
            msg_file.seekg(prev);
            return -1; // Indicate that send_msg was cancelled by terminate
        }

        // Wait until the client is reconnected or until the timeout is expired
        TCP_CLIENT_CONNECTED.wait_for(client_lock, COND_TIMEOUT);
    }

    // Update the pfd.fd with the current sockfd, could have changed if we reconnected
    pfd.fd = ft_tcp_client.sockfd;

    // Check if data can be written to the TCP Client fd, timeout = 10ms
    int ready = poll(&pfd, nfds, 10);

    // Check if poll failed or timed out
    if (ready <= 0)
    {
        if (ready < 0)
        {
            // An error occured on poll, safe to assume we are no longer connected
            perror(PRINT_HEADER "Could not poll the TCP Client!");

            // Notify the main thread that we are no longer connected
            ft_tcp_client.connected = false;
            TCP_CLIENT_DISCONNECTED.notify_one();
        }

        // Unlock the mutex
        client_lock.unlock();
        // Exit critical section

        // We were unable to send this particular message
        // So decrease the read pointer by one
        msg_file.seekg(prev);
        return 0; // Indicate that send_msg completed, but no data was sent
    }

    // We determined that we are almost certainly still connected
    // Now send the message to the server
    int totbytes = 0; // # of bytes sent sofar

    // Check the result of the poll to determine whether we can safely write to the TCP socket
    if (pfd.revents & POLLOUT)
    {
        // Ensure the entire message is sent
        while (totbytes < msg.length())
        {
            int nbytes = send(ft_tcp_client.sockfd, (msg.c_str() + totbytes), msg.length() - totbytes, 0);

            // Check if an error occured
            if (nbytes < 0)
            {
                perror(PRINT_HEADER "Could not send data to the server!");
                ft_tcp_client.connected = false; // Assume the connection dropped

                // Signal user_src_main that the client has disconnected
                TCP_CLIENT_DISCONNECTED.notify_one();
                // Unlock the mutex
                client_lock.unlock();
                // Exit critical section

                // We were unable to send this particular message
                // So decrease the read pointer by one line
                msg_file.seekg(prev);
                return 0; // Indicate that send_msg was completed, but no data was sent
            }

            // Otherwise, add nbytes to totbytes
            totbytes += nbytes;
        }
    }
    else
    {
        // Otherwise, we cannot send data to the TCP client without blocking
        // So decrease the readpointer by one line and return
        msg_file.seekg(prev);
        return 0; // Indicate that send_msg was completed, but no data was sent
    }

    // The message was send succesfully
    // Unlock the mutex
    client_lock.unlock();
    // Exit critical section

    return 1; // Indicate that send_msg was completed and data was sent
}
