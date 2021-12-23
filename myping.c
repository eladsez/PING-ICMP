// Sending ICMP Echo Requests using Raw-sockets.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

// ICMP header len for echo req
#define ICMP_HDRLEN 8

// Checksum algo
unsigned short calculate_checksum(unsigned short * paddress, int len);

// Change SOURCE_IP and DESTINATION_IP to the relevant
// for your computer
//  Note. You can place another IP-source address that does not belong to your
//  computer (IP-spoofing), i.e. just another IP from your subnet, and the ICMP
//  still be sent, but do not expect to see ICMP_ECHO_REPLY in most such cases
//  since anti-spoofing is wide-spread.

#define SOURCE_IP "10.0.2.15"
// i.e the gateway or ping to google.com for their ip-address
#define DESTINATION_IP "1.1.1.1"

#define ICMP_ECHO_ID 20

// clock setter
struct timeval start, end;

void startClock(){
        gettimeofday(&start, NULL);
}

float stopClock(){
        gettimeofday(&end, NULL);
        float time = (float)(end.tv_usec - start.tv_usec)/1000;
        return time;
}

void display(void *buff, int len, int *recvIndex){
        struct iphdr *ip = buff;
        struct icmphdr *icmp = buff+ip->ihl*4;

        if ( icmp->un.echo.id == ICMP_ECHO_ID ) {
                *(recvIndex) += 1;
                printf("echo response from %s", inet_ntoa(*((struct in_addr *)&(ip->saddr))));
                printf(" icmp_seq=%d RTT=%.3lf ms \n", icmp->un.echo.sequence, stopClock());
        }

}

//--------------------------------------------------------------------/
//--- listener - separate process to listen for and collect messages--/
//--------------------------------------------------------------------/
void listener(int *recvIndex, int *responseSock){
        if (*responseSock < 0) {
                printf("Response socket ERROR");
                exit(0);
        }
        struct sockaddr_in input_addr;
        unsigned char buff[1024];
        int len = sizeof(input_addr);
        int bytes;
        while(1) {
                bzero(buff, sizeof(buff));
                bytes = recvfrom(*responseSock, buff, sizeof(buff), 0, (struct sockaddr*)&input_addr, &len);
                if ( bytes > 0 ) {
                        display(buff, bytes, recvIndex);
                        break;
                }
                else if(bytes == 0) {
                        printf("ERROR The listener connection closed");
                        exit(1);
                }
                else if (bytes < 0) {
                        printf("ERROR faild to receive\n");
                        exit(1);
                }
        }
}

void ping(struct sockaddr_in *dest_addr, int *sock, int *sendIndex){
        struct icmp icmphdr; // ICMP-header
        char data[IP_MAXPACKET] = "This is the ping.\n";

        int datalen = strlen(data) + 1;

        //===================//
        // ICMP header init  //
        //===================//

        // Message Type (8 bits): ICMP_ECHO_REQUEST
        icmphdr.icmp_type = ICMP_ECHO;

        // Message Code (8 bits): echo request as defined in wikipeida
        icmphdr.icmp_code = 0;

        // Identifier (16 bits): some number to trace the response.
        // It will be copied to the response packet and used to map response to the request sent earlier.
        // Thus, it serves as a Transaction-ID when we need to make "ping"
        icmphdr.icmp_id = ICMP_ECHO_ID; // hey

        // Sequence Number (16 bits): starts at 0
        // because we're not payloading from another request.
        icmphdr.icmp_seq = *sendIndex;

        // ICMP header checksum (16 bits): set to 0 not to include into checksum calculation
        icmphdr.icmp_cksum = 0;

        // Combine the packet
        char packet[IP_MAXPACKET];

        // Next, ICMP header
        memcpy (packet, &icmphdr, ICMP_HDRLEN);

        // After ICMP header, add the ICMP data.
        memcpy (packet + ICMP_HDRLEN, data, datalen);

        // Calculate the ICMP header checksum
        icmphdr.icmp_cksum = calculate_checksum((unsigned short *) (packet), ICMP_HDRLEN + datalen);
        memcpy (packet, &icmphdr, ICMP_HDRLEN);

        // Send the packet using sendto() for sending datagrams.
        if (sendto (*sock, packet,ICMP_HDRLEN + datalen, 0, (struct sockaddr *) dest_addr, sizeof (*dest_addr)) == -1)
                printf ("sendto() failed with error: %d", errno);
        else {
                startClock();
                (*sendIndex) += 1;
        }

}

int main(){
        // Create raw socket for IP-RAW (make ICMP-header by yourself)
        int sock = -1;
        if ((sock = socket (AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1) {
                printf ("socket() failed with error: %d", errno);
                printf ("To create a raw socket, the process needs to be run by Admin/root user.\n\n");
                return -1;
        }

        // init the dest address
        struct sockaddr_in dest_addr;
        memset (&dest_addr, 0, sizeof (struct sockaddr_in)); // The port is irrelevant for Networking and therefore was zeroed.
        dest_addr.sin_family = AF_INET;
        inet_pton(AF_INET, DESTINATION_IP, &(dest_addr.sin_addr));
        //this line equal to the line above need to check why
        // dest_in.sin_addr.s_addr = DESTINATION_IP;

        printf("PING %s\n", DESTINATION_IP);
        int sendIndex = 0;
        int recvIndex = 0;

        while(sendIndex < 3) {
                ping(&dest_addr, &sock, &sendIndex);
                if(sendIndex > 0)
                        listener(&recvIndex, &sock);
        }
        // Close the raw socket descriptor.
        close(sock);
        int pocketloss = 100 - (int)((float)recvIndex/(float)sendIndex *100);

        printf("%d packets transmitted, %d received, %d%% packet loss\n",sendIndex, recvIndex, pocketloss);

        return 0;
}


// Compute checksum (RFC 1071).
unsigned short calculate_checksum(unsigned short * paddress, int len){
        int nleft = len;
        int sum = 0;
        unsigned short * w = paddress;
        unsigned short answer = 0;

        while (nleft > 1)
        {
                sum += *w++;
                nleft -= 2;
        }

        if (nleft == 1)
        {
                *((unsigned char *)&answer) = *((unsigned char *)w);
                sum += answer;
        }

        // add back carry outs from top 16 bits to low 16 bits
        sum = (sum >> 16) + (sum & 0xffff); // add hi 16 to low 16
        sum += (sum >> 16);           // add carry
        answer = ~sum;                // truncate to 16 bits

        return answer;
}
