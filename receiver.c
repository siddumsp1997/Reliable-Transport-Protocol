#include <stdio.h>      /* for printf() and fprintf() */
#include <stdlib.h>
#include <sys/socket.h> /* for socket() and bind() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */

#define ECHOMAX 50000     /* Longest string to echo */

/* Structure of the segmentPacket for sending to reciever */
typedef struct packet_header 
{
    int type;
    char seq_num[16];
    char ack_num[16];
    char control;  // 0 for Data packet and 1 for ACK packet
    int length;
    char data[5001];
} packet;

/* Creates and returns a segment Packet */
packet CreateACKPacket (int ack_type, int base){
    packet ack;
    ack.type = ack_type;
    ack.control = '1';
    int i;
    for(i=0; i<16; i++)
        ack.ack_num[i]='0';
    ack.ack_num[15]='\0';
    i=14;
    while(base > 0 && i>=0)
    {
        ack.ack_num[i] = base%10+'0';
        base = base/10;
        i--;
    }
    return ack;
}


int main(int argc, char *argv[])
{
    int sockfd;                        /* Socket */
    struct sockaddr_in echoServAddr; /* Local address */
    struct sockaddr_in echoClntAddr; /* Client address */
    unsigned int cliAddrLen;         /* Length of incoming message */
    char echoBuffer[ECHOMAX];        /* Buffer for echo string */
    unsigned short echoServPort;     /* Server port */
    int recvMsgSize;                 /* Size of received message */
    int chunkSize;                   /* Size of chunks to send */
    float loss_rate = 0;             /* lose rate range from 0.0 -> 1.0, initialized to zero b/c lose rate is optional */

    /* random generator seeding */
    srand48(2345);

    if (argc < 3)         /* Test for correct number of parameters */
    {
        fprintf(stderr,"Usage:  %s <UDP SERVER PORT> <CHUNK SIZE>\n", argv[0]);
        exit(1);
    }

    /* Set arguments to appropriate values */
    echoServPort = atoi(argv[1]);  /* First arg:  local port */
    chunkSize = atoi(argv[2]);  /* Second arg:  size of chunks */

    /* Create socket for sending/receiving datagrams */
    if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        perror("socket() failed");
        exit(-1);
    }

    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(echoServPort);      /* Local port */

    /* Bind to the local address */
    if (bind(sockfd, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
    {
        perror("bind() failed");
        exit(-1);
    }
    char filename[30];
    printf("\nEnter file name to save as : ");
    scanf("%s",filename);
    /* Initialize variables to their needed start values */
    char dataBuffer[8192];
    int base = -2;
    int seqNumber = 0;
    FILE* file = fopen(filename, "wb");
    if( file == NULL) {
        printf("Error has occurred. file file could not be opened\n");
        exit(-1); 
    }

    while(1) /* Run forever */
    {
        /* Set the size of the in-out parameter */
        cliAddrLen = sizeof(echoClntAddr);

        /* struct for incoming datapacket */
        packet dataPacket;

        /* struct for outgoing ACK */
        packet ack;

        /* Block until receive message from a client */
        if ((recvMsgSize = recvfrom(sockfd, &dataPacket, sizeof(dataPacket), 0,
            (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0)
        {
            perror("recvfrom() failed");
            exit(-1);
        }

        seqNumber = atoi(dataPacket.seq_num);
        printf("seq - %d \n", seqNumber);
        /* If seq is zero start new data collection */
        int data_seq_num = atoi(dataPacket.seq_num);
        int write_size;
        if(data_seq_num == 0 && dataPacket.type == 1)
        {
            printf("Recieved Initial Packet from %s\n", inet_ntoa(echoClntAddr.sin_addr));
            write_size = fwrite(dataPacket.data,1,dataPacket.length, file);
            //sleep(1);
            printf("ws - %d\n",write_size );
           // printf("data - %s\n", dataPacket.data);
            base = 0;
            ack = CreateACKPacket(2, base);
        } else if (data_seq_num == base + 1) /* if base+1 then its a subsequent in order packet */
        {
            /* then concatinate the data sent to the recieving buffer */
            printf("Recieved  Subseqent Packet #%s\n", dataPacket.seq_num);
            write_size = fwrite(dataPacket.data,1,dataPacket.length, file);
            //sleep(1);
            printf("ws - %d\n",write_size );
            //printf("data - %s\n", dataPacket.data);
            //printf("length - %d\n", dataPacket.length);
            base = atoi(dataPacket.seq_num);
            ack = CreateACKPacket(2, base);
        } else if (dataPacket.type == 1 && data_seq_num != base + 1)
        {
            /* if recieved out of sync packet, send ACK with old base */
            printf("Recieved Out of Sync Packet #%s\n", dataPacket.seq_num);
            /* Resend ACK with old base */
            ack = CreateACKPacket(2, base);
        }

        /* type 4 means that the packet recieved is a termination packet */
        if(dataPacket.type == 4 && seqNumber == base ){
            base = -1;
            /* create an ACK packet with terminal type 8 */
            ack = CreateACKPacket(8, base);
        }

        /* Send ACK for Packet Recieved */
        if(base >= 0)
        {
            printf("------------------------------------  Sending ACK #%d\n", base);
            printf("acknum - %s\n", ack.ack_num);
            if (sendto(sockfd, &ack, sizeof(ack), 0,
                 (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) != sizeof(ack))
            {
                perror("sendto() sent a different number of bytes than expected");
                exit(-1);
            }
        } else if (base == -1) {
            printf("Recieved Teardown Packet\n");
            printf("Sending Terminal ACK - %d\n", base);
            if (sendto(sockfd, &ack, sizeof(ack), 0,
                 (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) != sizeof(ack))
            {
                perror("sendto() sent a different number of bytes than expected");
                exit(-1);
            }
        }

        /* if data packet is terminal packet, display and clear the recieved message */
        if(dataPacket.type == 4 && base == -1){
            fclose(file);
            close(sockfd);
            printf("\n MESSAGE RECIEVED\n %s\n\n", dataBuffer);
            memset(dataBuffer, 0, sizeof(dataBuffer));
            exit(1);
        }
    }
    /* NOT REACHED */
}




