#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h> 

#define DATALIMIT       5010    /* Longest string to echo */
#define TIMEOUT_SECS    3       /* Seconds between retransmits */
#define DATAMSG         1       /* Message of Data type */
#define TEARDOWNMSG     4       /* Message of Tear Down type */
#define MAXTRIES        16      /* Number of times it can resend */

int tries = 0;   /* Count of times sent - GLOBAL for signal-handler access */

/* Structure of the segmentPacket for sending to reciever */
typedef struct packet_header {
    int type;
    char seq_num[16];
    char ack_num[16];
    char control;  // 0 for Data packet and 1 for ACK packet
    int length;
    char data[5001];
}packet;

void alarm_handler(int signum)     /* Handler for SIGALRM */
{
    printf("Alarm\n");
}

/* Creates and returns a Data Packet */
packet CreateDataPacket (int seq_no, int length, char* data)
{
    packet pkt;
    pkt.control = '0';
    pkt.type = 1;
    int i;
    for(i=0; i<16; i++)
        pkt.seq_num[i]='0';
    pkt.seq_num[15]='\0';
    i=14;
    while(seq_no > 0 && i>=0)
    {
        pkt.seq_num[i] = seq_no%10+'0';
        seq_no = seq_no/10;
        i--;
    }
    pkt.length = length;
    memset(pkt.data, 0, sizeof(pkt.data));
    strcpy(pkt.data, data);
    return pkt;
}

/* Creates and returns a terminal Packet */
packet CreateTerminalPacket (int seq_no, int length){

    packet pkt;
    pkt.control = '0';
    pkt.type = 4;
    int i;
    for(i=0; i<16; i++)
        pkt.seq_num[i]='0';
    pkt.seq_num[15]='\0';
    i=14;
    while(seq_no > 0 && i>=0)
    {
        pkt.seq_num[i] = seq_no%10+'0';
        seq_no = seq_no/10;
        i--;
    }
    pkt.length = length;
    memset(pkt.data, 0, sizeof(pkt.data));
    return pkt;
}

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server;              /* Receiving server address */
    struct sockaddr_in fromAddr;            /* Source address of echo */
    unsigned int portno;                  /* Receiving server port */
    unsigned int fromSize;                  /* In-out of address size for recvfrom() */
    struct sigaction myAction;              /* For setting signal handler */
    char *ServerIP;                           /* IP address of server */
    int respStringLen;                      /* Size of received datagram */
    char filename[30];

    /* Variables used to control the flow of data */
    int chunkSize;                          /* number of bits of data sent with each segment */
    int windowSize;                         /* Number of segments in limbo */
    int tries = 0;                           /* Number of tries itterator */

    if (argc != 5)    /* Test for correct number of arguments */
    {
        fprintf(stderr,"Usage: %s <Server IP> <Server Port> <Chunk Size> <Window Size>\n You gave %d Arguments\n", argv[0], argc);
        exit(1);
    }

    /* Set the corresponding agrs to their respective variables */
    ServerIP = argv[1];                          /* First arg:  server IP address (dotted quad) */
    portno = atoi(argv[2]);       /* Second arg: string to echo */
    chunkSize = atoi(argv[3]);                 /* Third arg: Size of chunks being sent */
    windowSize = atoi(argv[4]);                /* Fourth arg: Size of Segment window */

    /* Print out of initial connection data */
    printf("Attempting to Send to: \n");
    printf("IP:          %s\n", ServerIP);
    printf("Port:        %d\n", portno);

    /* Check to make sure appropriate chunk size was entered */
    if(chunkSize > DATALIMIT){
        fprintf(stderr, "Error: Chunk Size is too large. Must be < 512 bytes\n");
        exit(1);
    }

    /* Create a best-effort datagram socket using UDP */
    sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0){
        perror("socket() failed");
        exit(-1);
    }

    /* Construct the server address structure */
    memset(&server, 0, sizeof(server));    /* Zero out structure */
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ServerIP);  /* Server IP address */
    server.sin_port = htons(portno);       /* Server port */

    printf("\nEnter file name to send: ");
    scanf("%s",filename);
    FILE *file;
    int size;

    printf("Getting file Size\n");
    file = fopen(filename, "rb");

    while(file == NULL) {
        printf("\nError Opening File, Enter again: ");
        scanf("%s",filename);
        file = fopen(filename, "rb");
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("Total file size: %d\n",size);

    /* Calculate number of Segments */
    int totalSize = size;
    int numOfSegments = totalSize / chunkSize;
    /* Might have left overs */
    if (totalSize % chunkSize > 0){
        numOfSegments++;
    }
    /* Set seqNumber, base and ACK to 0 */
    int base = -1;           /* highest segments AKC recieved */
    int seqNumber = 0;      /* highest segment sent, reset by base */
    int dataLength = 0;     /* Chunk size */

    /* Print out of data stats */
    printf("Window Size: %d\n", windowSize);
    printf("Chunk Size:  %d\n", chunkSize);
    printf("Chunks:      %d\n", numOfSegments);

    /* Set signal handler for alarm signal */
    myAction.sa_handler = alarm_handler;
    if (sigemptyset(&myAction.sa_mask) < 0) /* block everything in handler */
    {
        perror("sigfillset() failed");
        exit(-1);
    }
    myAction.sa_flags = 0;

    if (sigaction(SIGALRM, &myAction, 0) < 0)
    {
        perror("sigaction() failed for SIGALRM");
        exit(-1);
    }

    /* bool used to keep the program running until teardown ack has been recieved */
    int noTearDownACK = 1;
    int read_size;
    int status=0;

    while(noTearDownACK)
    {
        /* Send chunks from base up to window size */
        while( (seqNumber - base) <= windowSize)
        {
            packet dataPacket;
            if(status==1){
                /* Reached end, create terminal packet */
                dataPacket = CreateTerminalPacket(seqNumber, 0);
                printf("Sending Terminal Packet\n");
            } else {
                /* Create Data Packet Struct */
                char seg_data[chunkSize];
                read_size = fread(seg_data, 1, chunkSize-1, file);
                if(read_size<=0)status=1;
                dataLength = read_size;
                dataPacket = CreateDataPacket(seqNumber, dataLength, seg_data);
               // printf("seq - %s\n", dataPacket.seq_num);
              //  printf("data - %s\n", dataPacket.data);
               // printf("size - %d\n", dataPacket.length);
                printf("Sending Packet: %d\n", seqNumber);
                //printf("Chunk: %s\n", seg_data);
            }

            /* Send the constructed data packet to the receiver */
            if (sendto(sockfd, &dataPacket, sizeof(dataPacket), 0,
                (struct sockaddr *) &server, sizeof(server)) != sizeof(dataPacket))
            {
                perror("sendto() sent a different number of bytes than expected");
                exit(-1);
            }

            seqNumber++;           
            if(status  == 1)
                break;
        }


        /* Set Timer */
        alarm(TIMEOUT_SECS);        /* Set the timeout */

        /* IF window is full alert that it is waiting */
        printf("Window full: waiting for ACKs\n");

        /* Listen for ACKs, get highest ACK, reset base */
        packet ack;
        while ((respStringLen = recvfrom(sockfd, &ack, sizeof(ack), 0,
               (struct sockaddr *) &fromAddr, &fromSize)) < 0)
        {
            if (errno == EINTR)     /* Alarm went off  */
            {
                /* reset the seqNumber back to one ahead of the last recieved ACK */
                seqNumber = base + 1;

                printf("Timeout: Resending\n");
                if(tries >= 10){
                    printf("Tries exceeded: Closing\n");
                    exit(1);
                }
                else 
                {
                    alarm(0);

                    while( (seqNumber - base) <= windowSize){
                        packet dataPacket;

                        if(status == 1){
                            /* Reached end, create terminal packet */
                            dataPacket = CreateTerminalPacket(seqNumber, 0);
                            printf("Sending Terminal Packet\n");
                        } else {
                            /* Create Data Packet Struct */
                            char seg_data[chunkSize];
                            read_size = fread(seg_data, 1, chunkSize-1, file);
                            if(read_size<=0)
                                status=1;
                            dataLength = read_size;

                            dataPacket = CreateDataPacket(seqNumber, dataLength, seg_data);
                            printf("Sending Packet: %d\n", seqNumber);
                            //printf("Chunk: %s\n", seg_data);
                        }

                        /* Send the constructed data packet to the receiver */
                        if (sendto(sockfd, &dataPacket, sizeof(dataPacket), 0,
                             (struct sockaddr *) &server, sizeof(server)) != sizeof(dataPacket))
                        {
                            perror("sendto() sent a different number of bytes than expected");
                            exit(-1);
                        }
                        seqNumber++;
                        if(status == 1)
                            break;
                    }
                    alarm(TIMEOUT_SECS);
                }
                tries++;
            }
            else
            {
                perror("recvfrom() failed");
                exit(-1);
            }
        }

        /* 8 is teardown ack */
        if(ack.type != 8){
            printf("----------------------- Recieved ACK: %s\n", ack.ack_num);
            int acknum = atoi(ack.ack_num );
            if(acknum > base){
                /* Advances the sending, reset tries */
                base = acknum;
            }
        } else {
            printf("Recieved Terminal ACK\n");
            noTearDownACK = 0;
        }

        /* recvfrom() got something --  cancel the timeout, reset tries */
        alarm(0);
        tries = 0;

    }


    close(sockfd);
    fclose(file);
    exit(0);
}

