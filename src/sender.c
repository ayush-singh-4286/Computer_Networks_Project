
#include "packet.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define ACK_TIMEOUT_SECONDS 1
#define MAX_RETRIES 10

static int send_and_wait_ack(
    int sockfd,
    struct sockaddr_in *receiver,
    socklen_t receiver_len,
    uint32_t sequence,
    uint8_t type,
    const uint8_t *payload,
    uint16_t payload_length)
{
    uint8_t packet[MAX_PACKET_SIZE];
    size_t packet_length;

    if (encode_packet(
            packet, sizeof(packet),
            sequence, type,
            payload, payload_length,
            &packet_length) != 0)
        return -1;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {

        ssize_t sent = sendto(
            sockfd,
            packet,
            packet_length,
            0,
            (struct sockaddr *)receiver,
            receiver_len);

        if (sent != (ssize_t)packet_length) {
            perror("sendto");
            return -1;
        }

        printf("Sending packet: seq=%" PRIu32
               ", attempt=%d\n",
               sequence, attempt + 1);

        uint8_t ack_buffer[MAX_PACKET_SIZE];

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        ssize_t received = recvfrom(
            sockfd,
            ack_buffer,
            sizeof(ack_buffer),
            0,
            (struct sockaddr *)&from,
            &from_len);

        if (received < 0) {
            if (errno == EINTR)
                continue;

            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recvfrom");
                return -1;
            }

            printf("ACK timeout. Retransmitting...\n");
            continue;
        }

        if (from.sin_family != AF_INET ||
            from.sin_addr.s_addr != receiver->sin_addr.s_addr ||
            from.sin_port != receiver->sin_port)
            continue;

        uint32_t ack_sequence;
        uint8_t ack_type;
        uint16_t ack_length;
        const uint8_t *ack_payload;

        if (decode_packet(
                ack_buffer,
                (size_t)received,
                &ack_sequence,
                &ack_type,
                &ack_payload,
                &ack_length) != 0) {
            printf("Invalid ACK received.\n");
            continue;
        }

        if (ack_sequence == sequence &&
            ack_length == 0 &&
            ((type == TYPE_DATA && ack_type == TYPE_ACK) ||
             (type == TYPE_FIN && ack_type == TYPE_FIN_ACK))) {

            printf("ACK received for seq=%" PRIu32 "\n",
                   sequence);

            return 0;
        }
    }

    fprintf(stderr,
            "Maximum retries exceeded for seq=%" PRIu32 "\n",
            sequence);

    return -1;
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        printf("Usage: %s <receiver_ip> <port> <input_file>\n",
               argv[0]);
        return EXIT_FAILURE;
    }

    char *end;
    errno = 0;

    unsigned long port = strtoul(argv[2], &end, 10);

    if (errno != 0 || *argv[2] == '\0' ||
        *end != '\0' || port == 0 || port > 65535) {
        fprintf(stderr, "Invalid port.\n");
        return EXIT_FAILURE;
    }

    FILE *file = fopen(argv[3], "rb");

    if (!file) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("socket");
        fclose(file);
        return EXIT_FAILURE;
    }

    struct timeval timeout;
    timeout.tv_sec = ACK_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt");
        fclose(file);
        close(sockfd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in receiver;
    memset(&receiver, 0, sizeof(receiver));

    receiver.sin_family = AF_INET;
    receiver.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, argv[1],
                  &receiver.sin_addr) != 1) {
        fprintf(stderr, "Invalid receiver IP.\n");
        fclose(file);
        close(sockfd);
        return EXIT_FAILURE;
    }

    socklen_t receiver_len = sizeof(receiver);

    uint8_t payload[MAX_PAYLOAD_SIZE];
    uint32_t sequence = 0;
    int success = 1;

    while (1) {

        size_t bytes_read = fread(
            payload, 1, sizeof(payload), file);

        if (bytes_read > 0) {

            if (send_and_wait_ack(
                    sockfd,
                    &receiver,
                    receiver_len,
                    sequence,
                    TYPE_DATA,
                    payload,
                    (uint16_t)bytes_read) != 0) {

                success = 0;
                break;
            }

            sequence++;
        }

        if (bytes_read < sizeof(payload)) {

            if (ferror(file)) {
                perror("fread");
                success = 0;
            }

            break;
        }
    }

    if (success) {
        if (send_and_wait_ack(
                sockfd,
                &receiver,
                receiver_len,
                sequence,
                TYPE_FIN,
                NULL,
                0) != 0) {
            success = 0;
        }
    }

    fclose(file);
    close(sockfd);

    if (success)
        printf("\nFile transfer completed successfully.\n");
    else
        printf("\nFile transfer failed.\n");

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
