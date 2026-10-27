#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "channel_emulator.h"
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#define EMULATOR_PORT 7000
#define RECEIVER_PORT 7001
#define BUFFER_SIZE 2048


#ifdef _WIN32

static void sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}

#else

static void sleep_ms(int ms)
{
    struct timespec req;

    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;

    nanosleep(&req, NULL);
}

#endif



int main(void)
{
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Winsock initialization failed.\n");
        return 1;
    }
#endif

    int sock;
    char buffer[BUFFER_SIZE];

    struct sockaddr_in emulator_addr;
    struct sockaddr_in receiver_addr;
    struct sockaddr_in sender_addr;

#ifdef _WIN32
    int addr_len = sizeof(sender_addr);
#else
    socklen_t addr_len = sizeof(sender_addr);
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);

#ifdef _WIN32
    if (sock == INVALID_SOCKET)
#else
    if (sock < 0)
#endif
    {
        perror("Socket creation failed");
        return 1;
    }

    memset(&emulator_addr, 0, sizeof(emulator_addr));

    emulator_addr.sin_family = AF_INET;
    emulator_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    emulator_addr.sin_port = htons(EMULATOR_PORT);

    if (bind(sock, (struct sockaddr *)&emulator_addr,
             sizeof(emulator_addr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));

    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    receiver_addr.sin_port = htons(RECEIVER_PORT);

    printf("UDP channel emulator started.\n");
    printf("Listening on port %d.\n", EMULATOR_PORT);
    printf("Forwarding to receiver port %d.\n", RECEIVER_PORT);

    ChannelConfig config;

config.loss_rate = 0.20;
config.duplicate_rate = 0.0;
config.corruption_rate = 0.20;
config.reorder_rate = 0.20;

config.min_delay_ms = 0;
config.max_delay_ms = 0;

config.seed = 1234;

initialize_channel(&config);
                                                    

/*
config.loss_rate = 0.0;
config.duplicate_rate = 0.0;
config.corruption_rate = 0.0;
config.reorder_rate = 0.50;

config.min_delay_ms = 20;
config.max_delay_ms = 100;
                                                                                             
config.seed = 1234;

initialize_channel(&config);*/


printf("Configured packet loss: %.0f%%\n",
       config.loss_rate * 100);

printf("Configured reordering probability: %.0f%%\n",
       config.reorder_rate * 100);
               
printf("Configured delay: %d-%d ms\n",       
       config.min_delay_ms, config.max_delay_ms);       
      
         
             
printf("Configured packet loss: %.0f%%\n",
       config.loss_rate * 100);
       

    while (1)
    {
        config.min_delay_ms = 20;
config.max_delay_ms = 100;
        int received = recvfrom(
            sock,
            buffer,
            BUFFER_SIZE,
            0,
            (struct sockaddr *)&sender_addr,
            &addr_len
        );

        if (received < 0)
        {
            perror("Receive failed");
            continue;
        }

        printf("Received %d bytes.\n", received);
        if (should_drop_packet(&config))
{
    printf("Packet dropped!\n");
    continue;
    
if (should_corrupt_packet(&config) && received > 0)
{
    int position = received / 2;

    buffer[position] ^= 0x01;

    printf("Packet corrupted at byte %d!\n", position);
}


}
int delay_range =
    config.max_delay_ms - config.min_delay_ms + 1;

int delay_ms = config.min_delay_ms;

if (delay_range > 0)
{
    delay_ms += rand() % delay_range;
}

printf("Delaying packet by %d ms\n", delay_ms);

sleep_ms(delay_ms);
     
int sent = sendto(
    sock,
    buffer,
    received,
    0,
    (struct sockaddr *)&receiver_addr,
    sizeof(receiver_addr)
);

if (sent < 0)
{
    printf("Forwarding failed.\n");
}
else
{
    printf("Packet forwarded: %d bytes\n", sent);

    if (should_duplicate_packet(&config))
    {
        int duplicate_sent = sendto(
            sock,
            buffer,
            received,
            0,
            (struct sockaddr *)&receiver_addr,
            sizeof(receiver_addr)
        );

        if (duplicate_sent >= 0)
        {
            printf("Duplicate packet sent!\n");
        }
    }
}



        if (sent < 0)
        {
            perror("Forwarding failed");
        }
        else
        {
            printf("Forwarded %d bytes.\n", sent);
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return 0;
}
