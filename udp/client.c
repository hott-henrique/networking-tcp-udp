#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <unistd.h>

#include <openssl/sha.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

typedef struct { long key; unsigned char * content; long length; } content_t;

int connect_to_server(char * host);

unsigned char * exec_request(char * host, const char * request, long * sz, double * packet_loss);

#define PACKET_SZ 1024
char BUFFER[PACKET_SZ];

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "USAGE: %s HOST\n", argv[0]);
        perror("ERROR: No host provided.");
        exit(1);
    }

    long file_sz, checksum_sz;
    double file_packet_loss, checksum_packet_sz;

    struct timeval t1, t2;

    gettimeofday(&t1, NULL);

    unsigned char * file_content = exec_request(argv[1], "READ ../file.txt END", &file_sz, &file_packet_loss);

    if (!file_content) {
        puts("[ERROR] Download was not sucessful.");
    }

    gettimeofday(&t2, NULL);

    double elapsed = (t2.tv_sec - t1.tv_sec);


    if (file_content) {
        unsigned char * file_checksum = exec_request(argv[1], "CHECKSUM ../file.txt END", &checksum_sz, &checksum_packet_sz);

        unsigned char checksum[SHA512_DIGEST_LENGTH];

        SHA512((unsigned char*) file_content, file_sz, checksum);

        if (memcmp(checksum, file_checksum, SHA512_DIGEST_LENGTH) == 0) {
            FILE * output_file = fopen(argv[2], "wb");

            if (!output_file) {
                printf("[ERROR] Could not open ouput file.\n");
            } else {
                fwrite(file_content, sizeof(unsigned char), file_sz, output_file);
                fclose(output_file);
            }
        } else {
            printf("[ERROR] Files do not match.\n");
        }

        FILE * output_file = fopen(argv[2], "wb");

        if (!output_file) {
            printf("[ERROR] Could not open ouput file.\n");
        } else {
            fwrite(file_content, sizeof(unsigned char), file_sz, output_file);
            fclose(output_file);
        }

        free(file_content);
    }

    printf("DownloadRate: %f bytes per second\n", file_sz / elapsed);
    printf("PacketLoss: %f\n", file_packet_loss);

    return EXIT_SUCCESS;
}

unsigned char * exec_request(char * host, const char * request, long * out_sz, double * out_packet_loss) {
    int client;

    if ((client = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket() error!");
        exit(1);
    }

    struct sockaddr_in serveraddr;

    memset(&(serveraddr), 0, sizeof(serveraddr));

    serveraddr.sin_family = AF_INET;

    inet_pton(AF_INET, host, &serveraddr.sin_addr.s_addr);

    serveraddr.sin_port = htons(8080);

    struct timeval timeout;
    timeout.tv_sec = 7;
    timeout.tv_usec = 0;

    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0) {
        perror("socket() error!");
        exit(1);
    }

    if (setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) < 0) {
        perror("socket() error!");
        exit(1);
    }

    if (connect(client, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) == -1) {
        perror("socket() error!");
        exit(1);
    }

    socklen_t server_length = sizeof(serveraddr);

    sendto(client, request, strlen(request), MSG_CONFIRM, (struct sockaddr *) &serveraddr, sizeof(serveraddr));

    content_t * hash = NULL;

    size_t response_length = 0,
           total_length = 0,
           num_packets = 0,
           num_total_packets = 0;

    while ((response_length = recvfrom(client,
                                       BUFFER, sizeof(BUFFER) / sizeof(BUFFER[0]),
                                       MSG_WAITALL,
                                       (struct sockaddr *) &serveraddr, &server_length))) {
        if (response_length == -1) {
            break;
        }

        num_packets = num_packets + 1;

        total_length = total_length + (response_length - (2 * sizeof(long)));

        long packet_idx = ((long *) BUFFER)[0];

        num_total_packets = ((long *) BUFFER)[1];

        unsigned char * memory = (unsigned char *) malloc(
            sizeof(unsigned char) * (response_length - (sizeof(long) * 2))
        );

        content_t s = { .key = packet_idx, .content = memory, .length = response_length - (2 * sizeof(long)) };

        hmputs(hash, s);

        memcpy(memory, &BUFFER[sizeof(long) * 2], response_length - (sizeof(long) * 2));

        if (num_packets == num_total_packets) {
            break;
        }
    }

    *out_packet_loss = (double)shlen(hash) / (double)num_total_packets;

    unsigned char * file_content = NULL;

    *out_sz = total_length;

    if (num_packets == num_total_packets) {
        file_content = (unsigned char *) malloc((sizeof(unsigned char) * total_length) + 1);

        for (int i = 0; i < shlen(hash); ++i) {
            content_t packet = hash[i];

            memcpy(
                &file_content[packet.key * (PACKET_SZ - (2 * sizeof(long)))],
                packet.content,
                packet.length
            );
        }
    }

    for (int i = 0; i < shlen(hash); ++i) {
        free(hash[i].content);
    }

    hmfree(hash);

    close(client);

    return file_content;
}
