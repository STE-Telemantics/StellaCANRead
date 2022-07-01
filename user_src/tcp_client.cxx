#include "StellaCANRead.h"

#include <memory.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

#define PRINT_HEADER "[TCP Client] "

tcp_client::~tcp_client()
{
    close_con(); // Close the connection
}

void tcp_client::init()
{
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror(PRINT_HEADER "Failed to open socket!");
        return;
    }

    memset(&m_servaddr, 0, sizeof(sockaddr_in)); // Clean struct of garbage

    m_servaddr.sin_family = AF_INET;
    m_servaddr.sin_addr.s_addr = inet_addr(m_host.c_str());
    m_servaddr.sin_port = htons(m_port);

    int enable = 1;
#if USE_TCP_NODELAY
    // Enable TCP No Delay to disable Nagle's algorithm and send messages ASAP instead of grouping them into larger frames
    // Could create congestion due to the increase in packets sent
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
#endif

    // Enable socket Keep Alive to keep connection available as long as possible
    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(int));
}

int tcp_client::open_con()
{
    return connect(sockfd, (struct sockaddr *)&m_servaddr, sizeof(struct sockaddr_in));
}

int tcp_client::reconnect()
{
    close_con(); // First close the socket before trying to reconnect
    init();      // Reinitialize the socket

    return open_con(); // TODO: some form of exponential backoff stuff until connected
}

void tcp_client::close_con()
{
    close(sockfd);
}
