#include <nuttx/config.h>
#include <sys/mount.h>

#include <cstdio>
#include <iostream>
#include <debug.h>
#include <string>

#include <thread>
#include <mutex>
#include <condition_variable>
#include "StellaCANRead.h"

#include <nuttx/can.h>
//#include "libs/include/sio_client.h"

#define PRINT_HEADER "[Main] " 
#define IP "ws://84.27.195.174:3000"
//***************************************************************************
// Definitions
//***************************************************************************
// Configuration ************************************************************

// Debug ********************************************************************
// Non-standard debug that may be enabled just for testing the constructors

#ifndef CONFIG_DEBUG_FEATURES
#undef CONFIG_DEBUG_CXX
#endif

#ifdef CONFIG_DEBUG_CXX
#define cxxinfo _info
#else
#define cxxinfo(x...)
#endif

//using namespace sio;
using namespace std::chrono;

bool terminate;

// SIO Client
//sio::client active_client;
bool connected;

// SOCKETIO 
std::mutex MUTEX_SOCKET_IO;
std::condition_variable SOCKET_IO_CONNECTION;

static  std::unique_lock<std::mutex> m_lock(MUTEX_SOCKET_IO);

/*class sio_listener {
        sio::client &handler;
public:
    
    sio_listener(sio::client& h):
    handler(h)
    {
    }
    
    void on_connected()
    {
        m_lock.lock();
        SOCKET_IO_CONNECTION.notify_all();
        printf(PRINT_HEADER "Succesfully connected to the server!");
        connected = true;
        m_lock.unlock();
    }

    void on_close(client::close_reason const& reason)
    {

        printf(PRINT_HEADER "Connection was closed!");
        exit(1);
    }
    
    void on_fail()
    {
        fprintf(stderr, PRINT_HEADER "Could not connect to server at " IP);
    }
};*/

extern "C" int main(int argc, FAR char *argv[])
{
    // Enable LED0 to shine

    // Mount the SD-card into the file system IF it isn't automounted?
    // Potential error: SDIO peripheral not initialized?
    // Potential fix: use SD-card over SPI
    if(mount("/dev/mmcsd0", "/mnt", "vfat", 0, nullptr) < 0){
        perror(PRINT_HEADER "Could not mount the SD Card!");
        return;
    }

    //Start the SocketCAN Reader module
    std::cout << "Started the SocketCAN module!" << std::endl;
    std::thread socket_can_t (socket_can_reader);


    std::cout << "Size of can_frame: " << sizeof(struct can_frame) << std::endl;

    // Start the Data Handler module
    //std::thread data_handler_t (data_handler);
    // Start the SD Controller module

    // TODO:
    // Some while loop that polls for button input
    sleep(10); // Sleep for 10 seconds
    terminate = true;

    // Wait for the SocketCAN Reader module to terminate
    socket_can_t.join();
    std::cout << "Joined the SocketCAN Reader" << std::endl;

    // Wait for the Data Handler module to terminae
    //data_handler_t.join();
    // Wait for the SD Controller module to terminate

    // The program has terminated, clean up the connection
    //active_client.sync_close();
    //active_client.clear_con_listeners();

    std::cout << "Done!" << std::endl;
    return 0;
}
