#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <unistd.h>

#include <openssl/sha.h>

#define PACKET_SZ 4096


int connect_to_server(char * host);

unsigned char * exec_request(char * host, const char * request, long * out_sz);

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "USAGE: %s HOST OUTPUT_FILE\n", argv[0]);
        perror("ERROR: No host provided.");
        exit(1);
    }

    long file_sz, checksum_sz;

    struct timeval t1, t2;

    gettimeofday(&t1, NULL);

    unsigned char * file_content = exec_request(argv[1], "READ ../file_2mb.txt END", &file_sz);

    gettimeofday(&t2, NULL);

    double elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1e6;

    unsigned char * file_checksum = exec_request(argv[1], "CHECKSUM ../file_2mb.txt END", &checksum_sz);

    unsigned char checksum[SHA512_DIGEST_LENGTH];

    SHA512(file_content, file_sz, checksum);

    if (memcmp(checksum, file_checksum, SHA512_DIGEST_LENGTH) == 0) {
        printf("DownloadSuccessful: true\n");
    } else {
        printf("DownloadSuccessful: false\n");
    }

    FILE * output_file = fopen(argv[2], "wb");

    if (!output_file) {
        printf("[ERROR] Could not open ouput file.\n");
    } else {
        fwrite(file_content, sizeof(unsigned char), file_sz, output_file);
        fclose(output_file);
    }

    free(file_content);
    free(file_checksum);

    printf("TimeElapsed: %f second(s)\n", elapsed);
    printf("DownloadRate: %f bytes per second\n", file_sz / elapsed);

    return EXIT_SUCCESS;
}

unsigned char * exec_request(char * host, const char * request, long * out_sz) {
    int client;

    if ((client = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket() error!");
        exit(1);
    }

    struct sockaddr_in serveraddr;

    memset(&(serveraddr), 0, sizeof(serveraddr));

    serveraddr.sin_family = AF_INET;

    inet_pton(AF_INET, host, &serveraddr.sin_addr.s_addr);

    serveraddr.sin_port = htons(8080);

    if (connect(client, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1) {
        perror("socket() error!");
        exit(1);
    }

    write(client, request, strlen(request));

    read(client, out_sz, sizeof(long));

    unsigned char * response = (unsigned char *) malloc(sizeof(unsigned char) * (*out_sz));

    long num_total_packets = ceil((double) *out_sz / (double) PACKET_SZ);

    printf("NTP: %ld\n", num_total_packets);

    for (int i = 0; i < num_total_packets; i = i + 1) {
        // printf("idx: %d %d\n", i * PACKET_SZ, PACKET_SZ);
        long num_bytes = read(client, &response[i * PACKET_SZ], PACKET_SZ);
    }

    close(client);

    return response;
}
