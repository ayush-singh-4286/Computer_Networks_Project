
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

int main(void)
{
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed.\n");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(7000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    for (int i = 1; i <= 10; i++)
    {
        char message[100];
        sprintf(message, "PACKET %d", i);

        int sent = sendto(sock, message, (int)strlen(message), 0,
                          (struct sockaddr *)&addr, sizeof(addr));

        if (sent == SOCKET_ERROR)
            printf("Failed to send packet %d\n", i);
        else
            printf("Sent: %s\n", message);

        Sleep(10);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}

