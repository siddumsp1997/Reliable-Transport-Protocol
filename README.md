# Reliable-Transport-Protocol
Reliable Transport Protocol using Datagram sockets
In RelTP, the sender maintains a sequence number field in the packet header, and
acknowledgement (ACK) packets are used by the receiver to acknowledge the receipt of a
particular packet. If a sender transmits a packet, and then does not receive an ACK within a
timeout value (say, t), then it assumes the packet to be lost in the network, and retransmits the
packet.
In this protocol both the sender and the receiver also maintains a window. Let the window size
be w packets. This indicates that the sender can transmit a maximum of w packets without
waiting for an acknowledgement. Once the sender receives the acknowledgement, it slides the
window accordingly. The acknowledgement (ACK) is cumulative acknowledgement, i.e., an ACK
packet with sequence number n indicates that all the packets up to sequence number n has been
received correctly, and the receiver is expecting packet with sequence number n+1.
The protocol is explained using the following diagram. Say, the window size is 4. Packet 3 is lost,
and after the timeout, the sender retransmits the packets starting from packet 3. Note that the
receiver does not accept packets 4, 5 and 6 unless it receives packet 3, therefore all packets
starting from 3 needs to be retransmitted – therefore, the name of the protocol is Go-Back-N. 

The packet header structure is defined as follows : The header should contain a 16 bits sequence
number field, 16 bits acknowledgement number field, and 1 bit control field. If control
field = 0, then it is a DATA packet, else (control bit = 1) it is an ACK packet. 

