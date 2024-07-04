#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "rudp.h"



// ==================== Timer Functions ====================

void start_timer(Timer *self)
{
    self->active = true;
    time(&self->start_time);
}

void update_time(Timer *self)
{
    if (self->active) {
        time(&self->current_time);
        self->elapsed_time = difftime(self->current_time, self->start_time);
    }
}

void reset_timer(Timer *self)
{
    time(&self->start_time);
}

void stop_timer(Timer *self)
{
    self->active = false;
}

bool timeout(Timer *self)
{
    return self->active && self->elapsed_time >= TIMEOUT;
}



// ==================== RUDP_Segment Functions ====================

void make_segment(RUDP_Segment *self, char *data, uint16_t data_length, uint8_t seqno)
{
    self->header.ack = 0;
    self->header.last = 0;
    self->header.seqno = seqno;
    memcpy(self->data, data, data_length);
}

void make_ack_segment(RUDP_Segment *self, uint8_t seqno)
{
    self->header.ack = 1;
    self->header.last = 0;
    self->header.seqno = seqno;
}



// ==================== RUDP_Window Functions ====================

void window_init(RUDP_Window *self, RUDP *rudp)
{
    self->rudp = rudp;
    self->base = 0;
    self->next = 0;
}

// Returns index of segment with seqno within window range.
uint8_t get_index(RUDP_Window *self, uint8_t seqno)
{
    uint8_t index = self->base;
    while (self->rudp->buffer[index].header.seqno != seqno 
            && index - self->base + 1 < WINDOW_SIZE) 
    {
        index++;
    }
    return index;
}



// ==================== ReceiverThread Functions ====================

void rt_init(ReceiverThread *self, RUDP *rudp)
{
    self->rudp = rudp;
    self->bytes_received = 0;
    self->stop = false;
}

void* receive(void* arg)
{
    ReceiverThread *self = (ReceiverThread*)arg;
    RUDP_Window *window = &self->rudp->window;
    RUDP_Segment *buffer = self->rudp->buffer;
    Timer *timers = self->rudp->timers;
    RUDP_Segment segment, ack;
    ssize_t bytes;
    uint8_t index, timer_index;

    struct sockaddr_in host_addr;
    socklen_t host_addr_size;

    while (1) {
        host_addr_size = sizeof(host_addr);
        bytes = recvfrom(self->rudp->sockfd, &segment, sizeof(RUDP_Segment), 0, 
                            (struct sockaddr*)&host_addr, &host_addr_size);
        if (bytes == -1) {
            self->bytes_received = -1;
            self->stop = true;
            break;
        }
        index = get_index(window, segment.header.seqno);
        // For Sender.
        // If an ack is received.
        if (segment.header.ack) {
            if (self->rudp->logs) {
                printf("Ack Received: %d", segment.header.seqno);
                if (segment.header.last) {
                    printf(" --> Last Ack");
                }
                printf("\n");
            }
            // If ack is in [sendBase, sendBase + WINDOW_SIZE - 1].
            if (index >= window->base && index <= window->base + WINDOW_SIZE - 1) {
                // Find and stop the timer for the segment for which the ack is received.
                for (timer_index = 0; timer_index < WINDOW_SIZE; timer_index++) {
                    if (timers[timer_index].index == index) {
                        break;
                    }
                }
                stop_timer(&timers[timer_index]);

                // Mark the segment as received using ack field.
                buffer[index].header.ack = 1;

                // If earliest ack is received than advance window base to the next unacked segment.
                while (buffer[window->base].header.ack) {
                    window->base++;
                }
            }
        }
        // For Receiver.
        // If a data segment is received.
        else {
            if (self->rudp->logs) {
                printf("Segment Received: %d", segment.header.seqno);
                if (segment.header.last) {
                    printf(" --> Last Segment");
                }
                printf("\n");
            }

            // If segment is in [rcvBase, rcvBase + WINDOW_SIZE - 1].
            if (index >= window->base && index <= window->base + WINDOW_SIZE - 1) {
                // Store segment in buffer.
                buffer[index] = segment;

                // Mark the segment as received using ack field.
                buffer[index].header.ack = 1;

                self->bytes_received += bytes - sizeof(RUDP_Header);
                // Send ack for received segment.
                make_ack_segment(&ack, segment.header.seqno);
                if (buffer[index].header.last) {
                    ack.header.last = 1;
                }
                
                bytes = sendto(self->rudp->sockfd, &ack, sizeof(RUDP_Header), 0, 
                                    (struct sockaddr*)&host_addr, host_addr_size);
                if (bytes == -1) {
                    self->bytes_received = -1;
                    self->stop = true;
                    break;
                }
                if (self->rudp->logs) {
                    printf("Sent ACK: %d", ack.header.seqno);
                    if (ack.header.last) {
                        printf(" --> Last ACK");
                    }
                    printf("\n");
                }

                // If in order segemnt is received then advance window base to next not yet received segment.
                while (buffer[window->base].header.ack) {
                    window->base++;
                }
            }
            // If segment is in [rcvBase - WINDOW_SIZE, rcvBase - 1].
            else if (index >= window->base - WINDOW_SIZE && index <= window->base - 1) {
                // Send ack for received segment.
                make_ack_segment(&ack, segment.header.seqno);
                if (buffer[index].header.last) {
                    ack.header.last = 1;
                }
                bytes = sendto(self->rudp->sockfd, &ack, sizeof(RUDP_Header), 0, 
                                    (struct sockaddr*)&host_addr, host_addr_size);
                if (bytes == -1) {
                    self->bytes_received = -1;
                    self->stop = true;
                    break;
                }
                if (self->rudp->logs) {
                    printf("Resent ACK: %d", ack.header.seqno);
                    if (ack.header.last) {
                        printf(" --> Last ACK");
                    }
                    printf("\n");
                }
            }
        }
        // If all segments or acks received.
        if (buffer[window->base - 1].header.last) {
            self->stop = true;
            break;
        }
        usleep(1);
    }
    pthread_exit(NULL);
}



// ==================== RUDP Functions ====================

int rudp_socket(RUDP *self)
{
    self->logs = false;
    self->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    return self->sockfd;
}

int rudp_close(RUDP *self)
{
    return close(self->sockfd);
}

int rudp_bind(RUDP *self, const struct sockaddr *addr, socklen_t addrlen)
{
    return bind(self->sockfd, addr, addrlen);
}


// Used to make sements from buffer argument and insert in RUDP buffer before sending.
void insert_segments(RUDP *self)
{
    uint8_t seqno = 0;
    uint8_t i;
    // Make no_of_segments - 1 segments with data of MAX_PAYLOAD_SIZE bytes.
    for (i = 0; i < self->no_of_segments - 1; i++) {
        make_segment(&self->buffer[i], self->buffer_arg, MAX_PAYLOAD_SIZE, seqno);
        seqno = (seqno + 1) % SEQUENCE_NUMBERS;
        self->buffer_arg += MAX_PAYLOAD_SIZE;
    }
    // For last segment length is calculated separately because it may not be
    // a multiple of MAX_PAYLOAD_SIZE and the last bit is set in the header.
    make_segment(&self->buffer[i], self->buffer_arg, self->buffer_arg_len - (i * MAX_PAYLOAD_SIZE), seqno);
    self->buffer[i].header.last = 1;
}

ssize_t rudp_sendto(RUDP *self, const void *buffer, size_t length, 
                        const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (self->logs) {
        printf("------------------------------\n");
    }
    if (length > BUFFER_SIZE * MAX_PAYLOAD_SIZE) {
        fprintf(stderr, "Cannot send more than %d bytes\n", BUFFER_SIZE * MAX_PAYLOAD_SIZE);
        return -1;
    }
    uint8_t i;
    ssize_t bytes_sent = 0;
    ssize_t bytes;

    // Initialize RUDP_Window and ReceiverThread struct variables.
    window_init(&self->window, self);
    rt_init(&self->receiver, self);

    // Disable all timers.
    for (i = 0; i < WINDOW_SIZE; i++) {
        self->timers[i].active = false;
    }
    self->next_timer = 0;

    self->buffer_arg = (char*)buffer;
    self->buffer_arg_len = length;
    self->no_of_segments = length / MAX_PAYLOAD_SIZE;
    if (length % MAX_PAYLOAD_SIZE != 0) {
        self->no_of_segments++;
    }

    insert_segments(self);

    // Start receiver thread.
    pthread_create(&self->receiver.tid, NULL, receive, (void*)&self->receiver);
    
    while (!self->receiver.stop) {
        // Send more segments if segments sent are less than window size 
        // and all segments are not sent.
        if (self->window.next - self->window.base + 1 < WINDOW_SIZE 
            && self->window.next < self->no_of_segments)
        {
            if (self->window.next != self->no_of_segments - 1) {
                bytes = sendto(self->sockfd, &self->buffer[self->window.next], sizeof(RUDP_Segment), 0, 
                                    dest_addr, addrlen);
            }
            else {
                bytes = sendto(self->sockfd, &self->buffer[self->window.next], sizeof(RUDP_Header) + length - ((self->no_of_segments - 1) * MAX_PAYLOAD_SIZE), 0, 
                                    dest_addr, addrlen);

            }

            if (self->logs) {
                printf("Sent Segment: %d", self->buffer[self->window.next].header.seqno);
                if (self->buffer[self->window.next].header.last) {
                    printf(" --> Last Segment");
                }
                printf("\n");
            }

            bytes_sent += bytes - sizeof(RUDP_Header);

            // Increment the next_timer index to the index of the next available timer.
            while (self->timers[self->next_timer].active)
            {
                self->next_timer = (self->next_timer + 1) % WINDOW_SIZE;
            }
            // Start timer.
            self->timers[self->next_timer].index = self->window.next;
            start_timer(&self->timers[self->next_timer]);
            self->window.next++;
        }
        // Check for timeouts.
        for (i = 0; i < WINDOW_SIZE; i++) {
            update_time(&self->timers[i]);
            if (timeout(&self->timers[i])) {
                self->buffer[self->timers[i].index].header.ack = 0;
                if (self->timers[i].index != self->no_of_segments - 1) {
                    bytes = sendto(self->sockfd, &self->buffer[self->timers[i].index], sizeof(RUDP_Segment), 0, 
                                        dest_addr, addrlen);
                }
                else {
                    bytes = sendto(self->sockfd, &self->buffer[self->timers[i].index], sizeof(RUDP_Header) + length - ((self->no_of_segments - 1) * MAX_PAYLOAD_SIZE), 0, 
                                        dest_addr, addrlen);
                }

                if (self->logs) {
                    printf("Timeout. Resent Segment: %d", self->buffer[self->timers[i].index].header.seqno);
                    if (self->buffer[self->timers[i].index].header.last) {
                        printf(" --> Last Segment");
                    }
                    printf("\n");
                }

                reset_timer(&self->timers[i]);
            }
        }
    }
    
    pthread_join(self->receiver.tid, NULL);
    if (self->receiver.bytes_received == -1) {
        return -1;
    }
    return bytes_sent;
}


// Sets sequence numbers of the whole buffer 
// and sets acks to 0. 
void initialize_buffer(RUDP *self)
{
    uint8_t seqno = 0;
    for (uint8_t i = 0; i < BUFFER_SIZE - 1; i++) {
        self->buffer[i].header.ack = 0;
        self->buffer[i].header.seqno = seqno;
        seqno = (seqno + 1) % SEQUENCE_NUMBERS;
    }
}

// Used to copy data from all received segments in to the buffer argument.
void copy_to_buffer(RUDP *self)
{
    uint8_t i;
    for (i = 0; i < self->no_of_segments - 1; i++) {
        memcpy(self->buffer_arg, self->buffer[i].data, MAX_PAYLOAD_SIZE);
        self->buffer_arg += MAX_PAYLOAD_SIZE;
    }
    memcpy(self->buffer_arg, self->buffer[i].data, self->receiver.bytes_received - i * MAX_PAYLOAD_SIZE);
}

ssize_t rudp_recvfrom(RUDP *self, void *buffer, size_t length, 
                        struct sockaddr *src_addr, socklen_t *addrlen)
{
    if (self->logs) {
        printf("------------------------------\n");
    }
    // Initialize RUDP_Window and ReceiverThread struct variables.
    window_init(&self->window, self);
    rt_init(&self->receiver, self);

    // Setting sequence numbers is required for get_index function 
    // and setting acks to 0 is required for receive function because
    // ack 1 indicates that the segment has been received.
    initialize_buffer(self);

    self->buffer_arg = (char*)buffer;
    self->buffer_arg_len = length;

    // Start receiver thread.
    pthread_create(&self->receiver.tid, NULL, receive, (void*)&self->receiver);
    pthread_join(self->receiver.tid, NULL);

    // If no error occured.
    if (self->receiver.bytes_received != -1) {
        self->no_of_segments = self->receiver.bytes_received / MAX_PAYLOAD_SIZE;
        if (self->receiver.bytes_received % MAX_PAYLOAD_SIZE != 0) {
            self->no_of_segments++;
        }
        copy_to_buffer(self);
    }

    return self->receiver.bytes_received;
}




ssize_t SendFileTo(RUDP *rudp, FILE* fp, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    char buffer[FILE_BUFFER_SIZE];
    const char* eof = "EOF";
    short unsigned int eofLen = strlen(eof);
    size_t bytesRead;
    ssize_t bytesSent, totalBytesSent = 0;
    
    while (!feof(fp)) {
        bytesRead = fread(buffer, 1, FILE_BUFFER_SIZE, fp);
        bytesSent = rudp_sendto(rudp, buffer, bytesRead, dest_addr, addrlen);
        if (bytesSent == -1) {
            return -1;
        }
        totalBytesSent += bytesSent;
    }
    // Sending end-of-file indicator.
    bytesSent = rudp_sendto(rudp, eof, eofLen, dest_addr, addrlen);
    if (bytesSent == -1) {
        return -1;
    }

    return totalBytesSent;
}


ssize_t ReceiveFileFrom(RUDP *rudp, FILE* fp, struct sockaddr *src_addr, socklen_t *addrlen)
{
    char buffer[FILE_BUFFER_SIZE];
    const char* eof = "EOF";
    short unsigned int eofLen = strlen(eof);
    ssize_t bytesReceived, totalBytesReceived = 0;

    // Continue receiving data until an end-of-file indicator is encountered.
    while (1) {
        bytesReceived = rudp_recvfrom(rudp, buffer, FILE_BUFFER_SIZE, src_addr, addrlen);
        if (bytesReceived == -1) {
            return -1;
        }
        // Checking for end-of-file indicator.
        if (strncmp(buffer, eof, eofLen) == 0) {
            break;
        }
        fwrite(buffer, 1, bytesReceived, fp);
        totalBytesReceived += bytesReceived;
    }

    return totalBytesReceived;
}


