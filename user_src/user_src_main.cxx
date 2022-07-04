#include "StellaCANRead.h"

#include <cstdio>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>

#define PRINT_HEADER "[Main] "

using namespace std::chrono;

bool terminate;

// TCP Client
std::mutex MUTEX_DH_TCP_CLIENT;
std::mutex MUTEX_FT_TCP_CLIENT;
std::condition_variable TCP_CLIENT_DISCONNECTED;
std::condition_variable TCP_CLIENT_CONNECTED;

// A TCP Client used by the data handler to send messages to the server
tcp_client data_handler_client;

// A TCP Client used by the ft client to send messages to the server
tcp_client ft_tcp_client;

int main(int argc, char *argv[])
{

    // Initialize the tcp client instances, but not yet connect to the server
    // No need to protect these calls with a mutex, as no other threads are yet created
    data_handler_client = tcp_client(TCP_IP, TCP_PORT);
    data_handler_client.init();
    data_handler_client.connected = false; // Start in a disconnected state
    ft_tcp_client = tcp_client(TCP_IP, TCP_PORT);
    ft_tcp_client.init();
    ft_tcp_client.connected = false; // Start in disconnected state

    // Start the SocketCAN Reader module
    std::cout << PRINT_HEADER << "Started the SocketCAN Module!" << std::endl;
    std::thread socket_can_t(socket_can_reader);

    // Start the Data Handler module
    std::cout << PRINT_HEADER << "Started the Data Handler Module" << std::endl;
    std::thread data_handler_t(data_handler);

    // Start the SD Controller module
    std::cout << PRINT_HEADER << "Started the SD Controller Module" << std::endl;
    std::thread sd_controller_t(sd_controller);

    // Wait a short period before starting the FT Client, to ensure the SD Controller has finished setting up
    std::this_thread::sleep_for(10ms);

    // Start the FT Client module
    std::cout << PRINT_HEADER << "Started the FT Client Module" << std::endl;
    std::thread ft_client_t(ft_client);

    // Create a mutex lock for the DH TCP client, do not yet lock
    std::unique_lock<std::mutex> dh_client_lock(MUTEX_DH_TCP_CLIENT, std::defer_lock);
    // Create a mutex lock for the FT TCP client, do not yet lock
    std::unique_lock<std::mutex> ft_client_lock(MUTEX_FT_TCP_CLIENT, std::defer_lock);

#if USE_TIMER
    // Determine at which point the program should be terminated
    int64_t terminatetime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count() + PROG_DUR;
#endif

    // Enter critical section
    // Lock the TCP client mutexes
    std::lock(dh_client_lock, ft_client_lock);

    // Now we can take all the time we need to connect to the server
    int open_dh = data_handler_client.open_con();
    int open_ft = ft_tcp_client.open_con();

    if (open_dh < 0)
    { // Failed to connect to the server
        perror(PRINT_HEADER "Failed to open TCP connection");
        data_handler_client.connected = false;
    }
    else
    {
        data_handler_client.connected = true; // Otherwise we are connected
    }

    if (open_ft < 0)
    { // Failed to connect to the server
        perror(PRINT_HEADER "Failed to open TCP connection");
        ft_tcp_client.connected = false;
    }
    else
    {
        ft_tcp_client.connected = true; // Otherwise we are connected
    }

    // Unlock the mutexes
    ft_client_lock.unlock();
    dh_client_lock.unlock();
    // Exit critical section

    // Loop until terminate is true
    while (!terminate)
    {
        // Enter critical section
        // Lock the dh mutex (We don't lock both as a CV can only take one mutex)
        dh_client_lock.lock();

        // Wait until we disconnect or terminate is true
        while ((data_handler_client.connected && ft_tcp_client.connected) && !terminate)
        {

#if USE_TIMER
            // Keep track of the timer
            int64_t curtime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            if (curtime > terminatetime)
            {
                terminate = true;
                // Unlock dh mutex
                dh_client_lock.unlock();
                // Exit critical section
                break;
            }
#endif

            // Wait until we are signalled that one of the TCP clients has disconnected, or we timed out
            TCP_CLIENT_DISCONNECTED.wait_for(dh_client_lock, COND_TIMEOUT);
        }

        if (terminate)
        {
            break;
        }

        // Now we can also safely lock the ft_tcp_client mutex
        ft_client_lock.lock();

#if USE_TIMER
        // Keep track of the timer here as well
        int64_t curtime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        if (curtime > terminatetime)
        {
            terminate = true;
            // Unlock mutexes
            ft_client_lock.unlock();
            dh_client_lock.unlock();
            // Exit critical section
            break;
        }
#endif

        // Try to reconnect to the server
        open_dh = data_handler_client.reconnect();
        open_ft = ft_tcp_client.reconnect();

        // Check if we successfully reconnected
        if (open_dh == 0 && open_ft == 0)
        {
            // If yes, set connected to true
            data_handler_client.connected = true;
            ft_tcp_client.connected = true;
            std::cout << PRINT_HEADER "Reconnected with server" << std::endl;

            // Notify the ft_client that there is a TCP connection again
            TCP_CLIENT_CONNECTED.notify_one();
        }
        else
        {
            // Otherwise, we could not connect to the server
            perror(PRINT_HEADER "Failed to open TCP connections");
        }

        // Unlock the mutexes
        ft_client_lock.unlock();
        dh_client_lock.unlock();
        // Exit critical section

        // Now sleep for RECON_DELAY milliseconds before trying to reconnect again
        std::this_thread::sleep_for(RECON_DELAY);
    }

    // Wait for the SocketCAN Reader module to terminate
    socket_can_t.join();
    std::cout << PRINT_HEADER << "Joined the SocketCAN Reader" << std::endl;

    // Wait for the Data Handler module to terminae
    data_handler_t.join();
    std::cout << PRINT_HEADER << "Joined the Data Handler Module" << std::endl;

    // Wait for the SD Controller module to terminate
    sd_controller_t.join();
    std::cout << PRINT_HEADER << "Joined the SD Controller Module" << std::endl;

    ft_client_t.join();
    std::cout << PRINT_HEADER << "Joined the FT Client Module" << std::endl;

    // The program has terminated, clean up the connections
    data_handler_client.close_con();
    ft_tcp_client.close_con();

    std::cout << PRINT_HEADER << "Finished!" << std::endl;
    return 0;
}
