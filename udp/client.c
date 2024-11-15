#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <unistd.h>

#include <openssl/sha.h>


int connect_to_server(char * host);

unsigned char * exec_request(char * host, const char * request, long * sz);

#define PACKET_SZ 1024
char BUFFER[PACKET_SZ];

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "USAGE: %s HOST\n", argv[0]);
        perror("ERROR: No host provided.");
        exit(1);
    }

    long file_sz, checksum_sz;

    struct timeval t1, t2;

    gettimeofday(&t1, NULL);

    unsigned char * file_content = exec_request(argv[1], "READ ../file.txt END", &file_sz);

    gettimeofday(&t2, NULL);

    double elapsed = (t2.tv_sec - t1.tv_sec);

    unsigned char * file_checksum = exec_request(argv[1], "CHECKSUM ../file.txt END", &checksum_sz);

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

    free(file_content);
    free(file_checksum);

    return EXIT_SUCCESS;
}

unsigned char * exec_request(char * host, const char * request, long * out_sz) {
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

    if (connect(client, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) == -1) {
        perror("socket() error!");
        exit(1);
    }

    socklen_t server_length = sizeof(serveraddr);

    sendto(client, request, strlen(request), MSG_CONFIRM, (struct sockaddr *) &serveraddr, sizeof(serveraddr));

    int response_length = recvfrom(client, BUFFER, sizeof(long) * 3, MSG_WAITALL, (struct sockaddr *) &serveraddr, &server_length);

    BUFFER[response_length] = '\0';

    long packet_idx  = ((long *) BUFFER)[0];
    long num_packets = ((long *) BUFFER)[1];
    *out_sz   = ((long *) BUFFER)[2];

    // printf("%ld %ld %ld\n", packet_idx, num_packets, *out_sz);

    sendto(client, "ACKNOWLEDGMENT END", 18, MSG_CONFIRM, (struct sockaddr *) &serveraddr, sizeof(serveraddr));

    unsigned char * file_buffer = malloc(sizeof(unsigned char) * (*out_sz));

    if (!file_buffer) {
        close(client);
        perror("malloc() error!");
        exit(1);
    }

    for (int i = 0; i < num_packets; i = i + 1) {
        int response_length = recvfrom(client,
                                       BUFFER, sizeof(BUFFER) / sizeof(BUFFER[0]),
                                       MSG_WAITALL,
                                       (struct sockaddr *) &serveraddr, &server_length);


        // BUFFER[response_length] = '\0';
        // puts(&BUFFER[8]);

        packet_idx = ((long *) BUFFER)[0];

        // printf("rl: %d | pidx: %ld %ld %ld\n", response_length, (packet_idx - 1) * 1020, sizeof(long), sizeof(int));

        memcpy(&file_buffer[(packet_idx - 1) * (PACKET_SZ - sizeof(long))], &BUFFER[8], response_length - sizeof(long));
    }

    close(client);

    return file_buffer;
}
