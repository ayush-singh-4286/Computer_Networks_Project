
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SOCKET sock;
    struct sockaddr_in addr, sender;
    char buffer[2048];

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed.\n");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(7001);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        printf("Receiver bind failed. Is port 7001 already in use?\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Test receiver listening on port 7001...\n");

    while (1)
    {
        int sender_len = sizeof(sender);

        int n = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&sender, &sender_len);

        if (n > 0)
        {
            buffer[n] = '\0';
            printf("Received: %s\n", buffer);
            fflush(stdout);
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
