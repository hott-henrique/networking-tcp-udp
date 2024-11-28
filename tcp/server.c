#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <openssl/sha.h>

#define PACKET_SZ 4096


enum ERRORS {
    SERVER_ERROR,
    UNKNOWN_METHOD,
    FILE_NOT_FOUND,
} error_code;

char BUFFER[PACKET_SZ];

void error(const char * msg);

unsigned char * file_checksum_response(char * file, long * sz);
unsigned char * file_response(char * file, long * sz);
unsigned char * error_response(char * file, long * sz);

int main(int argc, char * argv[]) {
    int n;

    if (argc < 2) {
        error("ERROR, no port provided\n");
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
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

    int newsockfd = accept(sockfd, (struct sockaddr *) &client_address, &client_length);
    if (newsockfd < 0) {
        error("ERROR on accept");
    }

    int count = 0;

    while (1) {
        n = read(newsockfd, &BUFFER[count], 1);

        count = count + 1;

        if (count >= 3 && BUFFER[count - 3] == 'E' && BUFFER[count - 2] == 'N' && BUFFER[count - 1] == 'D') {
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

            long response_total_length = 0;

            unsigned char * response = response_reader(requested_file, &response_total_length);

            write(newsockfd, &response_total_length, sizeof(long));

            long content_size = PACKET_SZ;
            long num_full_packets = response_total_length / content_size;
            long num_total_packets = ceil((double) response_total_length / (double) content_size);

            for (int i = 0; i < num_full_packets; i = i + 1) {
                write(newsockfd, &response[i * PACKET_SZ], PACKET_SZ);
            }

            if (num_full_packets != num_total_packets) {
                write(newsockfd,
                      &response[num_full_packets * content_size],
                      (response_total_length - (num_full_packets * content_size)));
            }

            close(newsockfd);

            free(response);

            newsockfd = accept(sockfd, (struct sockaddr *) &client_address, &client_length);

            if (newsockfd < 0) {
                error("ERROR on accept");
            }

            count = 0;
        }

        if (n < 0) {
            error("ERROR reading from socket");
        }
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

    SHA512((unsigned char *)file_content, *sz, checksum);

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
