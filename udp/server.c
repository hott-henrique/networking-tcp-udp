#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <openssl/sha.h>

#define PACKET_SZ 1024


enum ERRORS {
    SERVER_ERROR,
    UNKNOWN_METHOD,
    FILE_NOT_FOUND,
} error_code;

char BUFFER[2048];
unsigned char RESPONSE_BUFFER[PACKET_SZ];


void error(const char * msg);

unsigned char * file_response(char * file, long * sz);
unsigned char * file_checksum_response(char * file, long * sz);
unsigned char * error_response(char * file, long * sz);

int main(int argc, char * argv[]) {
    int n;

    if (argc < 2) {
        error("ERROR, no port provided\n");
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }

    struct sockaddr_in server_address;

    bzero((char *) &server_address, sizeof(server_address));

    int port = atoi(argv[1]);

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    if (bind(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        error("ERROR on binding");
    }

    listen(sockfd, 0);

    struct sockaddr_in client_address;
    socklen_t client_length = sizeof(client_address);

    long msg_lenght = 0;

    while ((msg_lenght = recvfrom(sockfd,
                                  BUFFER, sizeof(BUFFER) / sizeof(BUFFER[0]),
                                  MSG_WAITALL,
                                  (struct sockaddr *) &client_address, &client_length))) {
        printf("REQUEST: %s\n", BUFFER);

        char * request_method = strtok(BUFFER, " ");

        unsigned char * (*response_reader)(char *, long *) = NULL;

        if (strcmp(request_method, "READ") == 0) {
            response_reader = file_response;
        } else if (strcmp(request_method, "CHECKSUM") == 0) {
            response_reader = file_checksum_response;
        } else {
            error_code = UNKNOWN_METHOD;
            response_reader = error_response;
        }

        char * requested_file = strtok(NULL, " ");

        long response_size;
        unsigned char * response = response_reader(requested_file, &response_size);

        /* INFO: With each content we send an integer representing the packet index. */
        long header_size = 2 * sizeof(long);
        long content_size = PACKET_SZ - header_size;
        long num_full_packets = response_size / content_size;
        long num_total_packets = ceil((double) response_size / (double) content_size);

        long i = 0;
        for (; i < num_full_packets; i = i + 1) {
            ((long *) RESPONSE_BUFFER)[0] = i;
            ((long *) RESPONSE_BUFFER)[1] = num_total_packets;

            memcpy(&RESPONSE_BUFFER[header_size], &response[i * content_size], content_size);

            sendto(sockfd, RESPONSE_BUFFER, PACKET_SZ, MSG_CONFIRM, (struct sockaddr *) &client_address, client_length);
        }

        if (num_full_packets != num_total_packets) {
            ((long *) RESPONSE_BUFFER)[0] = i;
            ((long *) RESPONSE_BUFFER)[1] = num_total_packets;

            memcpy(&RESPONSE_BUFFER[header_size], &response[i * content_size], content_size);

            sendto(sockfd,
                   RESPONSE_BUFFER,
                   header_size + (response_size - (num_full_packets * content_size)),
                   MSG_CONFIRM,
                   (struct sockaddr *) &client_address,
                   client_length);
        }

        free(response);
    }

    close(sockfd);

    return 0;
}

unsigned char * file_response(char * file, long * sz) {
    FILE * f = fopen(file, "rb");

    if (!f) {
        error_code = FILE_NOT_FOUND;
        return error_response(file, sz);
    }

    fseek(f, 0, SEEK_END);

    *sz = ftell(f);

    unsigned char * buffer = (unsigned char *) malloc(*sz * sizeof(unsigned char));

    fseek(f, 0, SEEK_SET);

    fread(buffer, 1, *sz, f);

    fclose(f);

    return buffer;
}

unsigned char * file_checksum_response(char * file, long * sz) {
    unsigned char * file_content = file_response(file, sz);

    unsigned char * checksum = malloc(sizeof(unsigned char) * SHA512_DIGEST_LENGTH);

    SHA512((unsigned char *) file_content, *sz, checksum);

    *sz = SHA512_DIGEST_LENGTH;

    free(file_content);

    return checksum;
}

unsigned char * error_response(char * file, long * sz) {
    switch (error_code) {
        case SERVER_ERROR:
            *sz = 12;
            return (unsigned char *)("SERVER ERROR");

        case UNKNOWN_METHOD:
            *sz = 14;
            return (unsigned char *)("UNKNOWN METHOD");

        case FILE_NOT_FOUND:
            *sz = 14;
            return (unsigned char *)("FILE NOT FOUND");
    }
}

void error(const char * msg) {
    perror(msg);
    exit(1);
}
