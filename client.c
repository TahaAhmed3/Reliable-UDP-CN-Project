#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rudp.h"

#define MAX_FILENAME_LEN 100



int main(int argc, char* argv[1])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }
    const char *server_ip = argv[1];
    const int port = atoi(argv[2]);

    struct sockaddr_in serv_adr, from_adr;
    socklen_t adr_sz;

    ssize_t bytes;
    char filename[MAX_FILENAME_LEN];
    unsigned short int str_len;
    FILE* fp;

    RUDP rudp;

    if (rudp_socket(&rudp) == -1) {
        perror("Failed to create socket");
        exit(1);
    }

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = inet_addr(server_ip);
    serv_adr.sin_port = htons(port);

    rudp.logs = true;

    fputs("Enter filename: ", stdout);
    fgets(filename, MAX_FILENAME_LEN, stdin);
    str_len = strlen(filename);
    filename[--str_len] = '\0';

    bytes = rudp_sendto(&rudp, filename, str_len, (struct sockaddr*)&serv_adr, sizeof(serv_adr));
    if (bytes == -1) {
        perror("Error in sending filename");
        goto END;
    }

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror("Error in opening file");
        goto END;
    }
    
    bytes = SendFileTo(&rudp, fp, (struct sockaddr*)&serv_adr, sizeof(serv_adr));
    if (bytes == -1) {
        perror("Error in sending file");
        goto END;
    }

    printf("\n\nSent File Size: %ld\n", bytes);
    

    END:

    fclose(fp);
    rudp_close(&rudp);

    return 0;
}


