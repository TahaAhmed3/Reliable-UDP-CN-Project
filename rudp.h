#ifndef RUDP_H
#define RUDP_H

#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>


#define MAX_PAYLOAD_SIZE 500  // Max number of data bytes that can be sent in a segment.

// Bits for sequence number = 6.
// sequence numbers = 2 ^ 6 = 64.
#define SEQUENCE_NUMBERS 64   // Total number of sequence numbers.

// window size <= sequence numbers / 2 = 32.
#define WINDOW_SIZE 8         // Max number of unacknowledged segments that can be sent.
#define TIMEOUT 3             // Time in seconds to wait before retransmission.
#define BUFFER_SIZE 256       // Max segments that can be sent using one function call.

#define FILE_BUFFER_SIZE 102400   // Max file bytes that will be read and sent using one function call.


/*
<--------- 8 bits --------->
+-----------------+---+----+
| sequence number |ack|last|
+-----------------+---+----+
|                          |
|          data            |
+--------------------------+
*/

struct RUDP_Header
{
    unsigned int seqno : 6;
    unsigned int ack : 1;
    unsigned int last : 1;
}__attribute__((packed));
typedef struct RUDP_Header RUDP_Header;



struct RUDP_Segment
{
    RUDP_Header header;
    char data[MAX_PAYLOAD_SIZE];
}__attribute__((packed));
typedef struct RUDP_Segment RUDP_Segment;



struct RUDP;
typedef struct RUDP_Window
{
    struct RUDP *rudp;
    // For sender it is index of earliest unacked segment. 
    // For receiver it is index of earliest expected segment.
    uint8_t base;
    // For sender it is index of the next segment to be sent. 
    uint8_t next;
} RUDP_Window;



typedef struct ReceiverThread
{
    struct RUDP *rudp;
    pthread_t tid;
    ssize_t bytes_received;
    bool stop;
} ReceiverThread;


typedef struct Timer
{
    uint8_t index; // Index of the associated segment in the buffer.
    bool active;
    time_t start_time;
    time_t current_time;
    uint8_t elapsed_time;
} Timer;


typedef struct RUDP
{
    int sockfd;
    RUDP_Segment buffer[BUFFER_SIZE];
    uint8_t no_of_segments; // Number of segments in buffer.
    RUDP_Window window;
    ReceiverThread receiver;
    Timer timers[WINDOW_SIZE];
    uint8_t next_timer; // Index of next timer to be used.
    char *buffer_arg;
    size_t buffer_arg_len;
    bool logs;
} RUDP;



void rudp_init(RUDP *self);


// On success 0 is returned. On error -1 is returned.
int rudp_socket(RUDP *self);


// On success 0 is returned. On error -1 is returned.
int rudp_close(RUDP *self);


// On success 0 is returned. On error -1 is returned.
int rudp_bind(RUDP *self, const struct sockaddr *addr, socklen_t addrlen);


// Here bytes sent or received refer to the data bytes. It does not include 
// the bytes sent or received for acks or retransmissions.

// On succes the number of bytes sent are returned. On error -1 is returned.
ssize_t rudp_sendto(RUDP *self, const void *buffer, size_t length, 
                        const struct sockaddr *dest_addr, socklen_t addrlen);


// On succes the number of bytes received are returned. On error -1 is returned.
ssize_t rudp_recvfrom(RUDP *self, void *buffer, size_t length, 
                        struct sockaddr *src_addr, socklen_t *addrlen);



// Upon successful completion, the number of bytes sent is returned.
// Otherwise, -1 is returned.
ssize_t SendFileTo(RUDP *rudp, FILE* fp, const struct sockaddr *dest_addr, socklen_t addrlen);


// Upon successful completion, the number of bytes received is returned.
// Otherwise, -1 is returned.
ssize_t ReceiveFileFrom(RUDP *rudp, FILE* fp, struct sockaddr *src_addr, socklen_t *addrlen);




#endif


