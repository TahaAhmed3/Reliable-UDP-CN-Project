#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rudp.h"

#define MAX_FILENAME_LEN 100



int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    const int port = atoi(argv[1]);

    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t clnt_adr_sz;

    ssize_t bytes;
    char filename[MAX_FILENAME_LEN];
    const char *prefix = "received - ";
    FILE* fp;

    RUDP rudp;

    if (rudp_socket(&rudp) == -1) {
        perror("Failed to create socket");
        exit(1);
    }

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(port);

    if (rudp_bind(&rudp, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1) {
        perror("Failed to bind");
        exit(1);
    }

    rudp.logs = true;

    strcpy(filename, prefix);
    bytes = rudp_recvfrom(&rudp, filename + strlen(prefix), MAX_FILENAME_LEN - strlen(prefix), (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
    if (bytes == -1) {
        perror("Error in receiving filename");
        goto END;
    }

    filename[bytes + strlen(prefix)] = '\0';
    fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("Error in opening file.");
        goto END;
    }
    
    bytes = ReceiveFileFrom(&rudp, fp, (struct sockaddr*)&clnt_adr, &clnt_adr_sz);
    if (bytes == -1) {
        perror("Error in receiving file");
        goto END;
    }

    printf("\n\nFile Saved With Name: %s\n", filename);
    printf("Received File Size: %ld\n", bytes);


    END:

    fclose(fp);
    rudp_close(&rudp);

    return 0;
}


