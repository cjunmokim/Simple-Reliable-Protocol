//FILE: overlay/overlay.c
//
//Description: this file implements a ON process
//A ON process first connects to all the neighbors and then starts listen_to_neighbor threads each of which keeps receiving the incoming packets from a neighbor and forwarding the received packets to the SNP process. Then ON process waits for the connection from SNP process. After a SNP process is connected, the ON process keeps receiving sendpkt_arg_t structures from the SNP process and sending the received packets out to the overlay network.
//
//Date: April 28,2008


#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <assert.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "overlay.h"
#include "../topology/topology.h"
#include "neighbortable.h"

//you should start the ON processes on all the overlay hosts within this period of time
#define OVERLAY_START_DELAY 10

/**************************************************************/
//declare global variables
/**************************************************************/

//declare the neighbor table as global variable
nbr_entry_t* nt;
//declare the TCP connection to SNP process as global variable
int network_conn;
int why_killed = 0;

/**************************************************************/
//implementation overlay functions
/**************************************************************/

// This thread opens a TCP port on CONNECTION_PORT and waits for the incoming connection from all the neighbors that have a larger node ID than my nodeID,
// After all the incoming connections are established, this thread terminates
void* waitNbrs(void* arg) {
	//put your code here

	// Declare variables and initialize them.
	int neighborNum = topology_getNbrNum();
	int myNodeID = topology_getMyNodeID();
	int tcp_sd;
	struct sockaddr_in tcp_addr;
	int connection;

	// Create a socket.
	tcp_sd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_sd<0) {
		return 0;
	}
	memset(&tcp_addr, 0, sizeof(tcp_addr));
	tcp_addr.sin_family = AF_INET;
	tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcp_addr.sin_port = htons(CONNECTION_PORT);

	// Bind socket to address.
	if(bind(tcp_sd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr))< 0) {
		perror("binding error");
		return 0;
	}

	// Set socket state to listening.
	if(listen(tcp_sd, neighborNum) < 0) {
		perror("listening error");
		return 0;
	}
	printf("waiting for connection\n");

	// Find how many neighbors have smaller node IDs.
	int count = 0;
	for (int i=0; i<neighborNum; i++) {
		if (nt[i].nodeID > myNodeID) {
			count++;
		}
	}

	int connNum = 0;

	while (1) {
		// Exit thread if we have accepted count number of connections from neighbors.
		if (connNum == count) {
			pthread_exit(NULL);
		}

		// Exit thread if nt has already been freed by another thread.
		if (nt == NULL) {
			pthread_exit(NULL);
		}

		// Accept connection from neighbor.
		struct sockaddr_in node_addr;
		socklen_t node_addr_len = sizeof(struct sockaddr_in);
		connection = accept(tcp_sd,(struct sockaddr*)&node_addr, &node_addr_len);

		int nodeID = topology_getNodeIDfromip(&(node_addr.sin_addr));

		// If neighbor nodeID is not greater than local NodeID, then close connection.
		if (nodeID <= myNodeID) {
			close(connection);
			printf("Close connection since nodeID vs my ID --> %d %d\n", nodeID, myNodeID);
		}
		else { // Receive request and add connection descriptor to the neighbor table.
			nt_addconn(nt, nodeID, connection);

			connNum++;
		}
	}
	return 0;
}

// This function connects to all the neighbors that have a smaller node ID than my nodeID
// After all the outgoing connections are established, return 1, otherwise return -1
int connectNbrs() {
	//put your code here

	// Declare and intialize variables.
	int neighborNum = topology_getNbrNum();
	int myNodeID = topology_getMyNodeID();
	int sockfd;
	struct sockaddr_in servaddr;

	int i = 0;

	while (i < neighborNum) {
		if (nt == NULL) {
			return -1;
		}
		// Connect to neighbors only with node IDs less than local Node ID.
		if (nt[i].nodeID < myNodeID) {
			if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				printf("Problem in creating the socket.\n");
				return -1;
			}

			memset(&servaddr, 0, sizeof(servaddr));
			servaddr.sin_family = AF_INET;
			servaddr.sin_addr.s_addr = nt[i].nodeIP;
			servaddr.sin_port = htons(CONNECTION_PORT); // Convert to big-endian.

			// Connect to neighbor ON.
			if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))<0) {
				printf("Problem in connecting to the server.\n");
				return -1;
			}
			else {
				nt_addconn(nt, nt[i].nodeID, sockfd);
			}
		}
		i++;
	}
  	return EXIT_SUCCESS;
}

//Each listen_to_neighbor thread keeps receiving packets from a neighbor. It handles the received packets by forwarding the packets to the SNP process.
//all listen_to_neighbor threads are started after all the TCP connections to the neighbors are established
void* listen_to_neighbor(void* arg) {
	//put your code here

	int i = *(int *)arg;
	free(arg);

	while (1) {

		if (nt == NULL) {
			pthread_exit(NULL);
		}

		// Declare snp packet.
		snp_pkt_t pkt;
		memset(&pkt, 0, sizeof(snp_pkt_t));

		// Receive packet from the neighbor.
		int r = recvpkt(&pkt, nt[i].conn);
		if (r<0) {
			if (why_killed == 1) { // Local ON has terminated.
				printf("LISTEN_TO_NEIGHBOR: ON has terminated, so terminate thread.\n");
				if (network_conn != -1) {
					close(network_conn);
					network_conn = -1;
				}
				pthread_exit(NULL);
			}
			else { // Neighbor ON has terminated.
				printf("LISTEN_TO_NEIGHBOR: Neighbor ON %d has terminated, so terminate thread.\n", nt[i].nodeID);
				close(nt[i].conn);
				nt[i].conn = -1;
				pthread_exit(NULL);
			}
		}
		else {
			printf("LISTEN_TO_NEIGHBOR: Received packet from neighbor ON %d\n", nt[i].nodeID);
		}

		// If network_conn  == -1, then SNP has not been started yet, so loop again.
		if (network_conn == -1) {
			printf("LISTEN_TO_NEIGHBOR: SNP not connected.\n");
		}
		else { // SNP has been started.

			// Forward the packet to the local SNP process.
			int ret = forwardpktToSNP(&pkt, network_conn);
			if (ret<0) {
				if (why_killed == 0) { // SNP has terminated.
					printf("LISTEN_TO_NEIGHBOR: SNP terminated, so cannot forward packet to SNP.\n");
					if (network_conn != -1) {
						close(network_conn);
						network_conn = -1;
					}
				}
				else { // Local ON has terminated.
					printf("LISTEN_TO_NEIGHBOR: ON terminated, so terminate thread.\n");
					network_conn = -1;
					pthread_exit(NULL);
				}
			}
			else {
				printf("LISTEN_TO_NEIGHBOR: Forwarded packet from ON to local SNP.\n");
			}
		}
	}
  	return 0;
}

//This function opens a TCP port on OVERLAY_PORT, and waits for the incoming connection from local SNP process. After the local SNP process is connected, this function keeps getting sendpkt_arg_ts from SNP process, and sends the packets to the next hop in the overlay network. If the next hop's nodeID is BROADCAST_NODEID, the packet should be sent to all the neighboring nodes.
void waitNetwork() {
	//put your code here

	// Declare and initialize variables.
	int tcp_sd;
	struct sockaddr_in tcp_addr;
	struct sockaddr_in node_addr;
	socklen_t node_addr_len;
	int neighborNum = topology_getNbrNum();

	// Create socket.
	tcp_sd = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_sd<0) {
		perror("socket");
		return;
	}

	memset(&tcp_addr, 0, sizeof(tcp_addr));
	tcp_addr.sin_family = AF_INET;
	tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcp_addr.sin_port = htons(OVERLAY_PORT);

	// Bind socket.
	if(bind(tcp_sd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr))< 0) {
		perror("Binding error");
		return;
	}

	// Listen for incoming connection from the SNP.
	if(listen(tcp_sd, 1) < 0) {
		perror("Listening error");
		return;
	}
	printf("waiting for connection\n");

	// Accept SNP connection.
	node_addr_len = sizeof(struct sockaddr_in);
	network_conn = accept(tcp_sd,(struct sockaddr*)&node_addr, &node_addr_len);

	if (network_conn<0) {
		printf("Error in accepting connection from local SNP.\n");
		return;
	}

	printf("%s\n", "Received request from local SNP.\n");

	while (1) {

		// Get a sendpkt_arg_t packet from the local SNP.
		sendpkt_arg_t send_arg;
		memset(&send_arg, 0, sizeof(sendpkt_arg_t));
		memset(&send_arg.pkt, 0, sizeof(snp_pkt_t));

		// Receive packet from SNP.
		int ret = getpktToSend(&send_arg.pkt, &send_arg.nextNodeID, network_conn);

		if (ret<0) {
			if (why_killed == 0) { // SNP has terminated.
				printf("WAITNETWORK: SNP has terminated, so listen for another SNP connection.\n");
				if (network_conn != -1) {
					close(network_conn);
					network_conn = -1;
				}
				network_conn = accept(tcp_sd,(struct sockaddr*)&node_addr, &node_addr_len);
			}
			else { // Local ON has terminated.
				printf("WAITNETWORK: ON has terminated, so terminate function.\n");
				if (network_conn != -1) {
					close(network_conn);
					network_conn = -1;
				}
				return;
			}
		}
		else {

			// If the nodeID is BROADCAST_NODEID, then forward the packet to each neighbor.
			if (send_arg.nextNodeID == BROADCAST_NODEID) {
				for (int i=0; i<neighborNum; i++) {

					if (nt[i].conn == -1) {
						printf("WAITNETWORK: There is no connection to neighbor ON %d, so cannot send packet to this ON.\n", nt[i].nodeID);
						continue;
					}

					// Send packet to other neighbor.
					if (sendpkt(&send_arg.pkt, nt[i].conn)<0) {
						if (why_killed == 0) { // Neighbor ON terminates.
							printf("WAITNETWORK: Neighbor ON %d has terminated, so cannot send packet to this ON.\n", nt[i].nodeID);
							close(nt[i].conn);
							nt[i].conn = -1;
							continue;
						}
						else { // Local ON terminates.
							printf("WAITNETWORK: ON has terminated, so terminate function.\n");
							return;
						}
					}
					else {
						printf("WAITNETWORK: Sent ROUTE UPDATE packet to neighbor ON %d\n", nt[i].nodeID);
					}
				}
			}
			else {

				// printf("WAITNETWORK: Received SNP packet send_arg.pkt.header.src_nodeID --> %d\n", send_arg.pkt.header.src_nodeID);
				// Forward packet to designated neighbor.
				for (int i=0; i<neighborNum; i++) {
					if (nt[i].nodeID == send_arg.nextNodeID) {
						if (nt[i].conn == -1) {
							printf("WAITNETWORK: There is no connection to neighbor ON %d, so cannot send packet to this ON.\n", nt[i].nodeID);
							break;
						}

						// Send packet to neighbor.
						if (sendpkt(&send_arg.pkt, nt[i].conn)<0) {
							if (why_killed == 0) { // Neighbor ON terminates.
								printf("WAITNETWORK: Neighbor ON %d has terminated, so cannot send packet to this ON.\n", nt[i].nodeID);
								close(nt[i].conn);
								nt[i].conn = -1;
								break;
							}
							else { // Local ON terminates.
								printf("WAITNETWORK: ON has terminated, so terminate function.\n");
								return;
							}
						}
						else {
							printf("WAITNETWORK: Sent SNP packet to neighbor ON %d\n", nt[i].nodeID);
							break;
						}
					}
				}
			}
		}
	}
}

//this function stops the overlay
//it closes all the connections and frees all the dynamically allocated memory
//it is called when receiving a signal SIGINT
void overlay_stop() {
	//put your code here
	why_killed = 1; // Local ON has terminated.
	if (network_conn != -1) {
		close(network_conn);
		network_conn = -1;
	}

	nt_destroy(nt); // Destroy nt and free memory.
	nt = NULL;

	return;
}

int main() {
	//start overlay initialization
	printf("Overlay: Node %d initializing...\n",topology_getMyNodeID());

	//create a neighbor table
	nt = nt_create();
	//initialize network_conn to -1, means no SNP process is connected yet
	network_conn = -1;

	//register a signal handler which is sued to terminate the process
	signal(SIGINT, overlay_stop);

	//print out all the neighbors
	int nbrNum = topology_getNbrNum();
	int i;
	for(i=0;i<nbrNum;i++) {
		printf("Overlay: neighbor %d:%d\n",i+1,nt[i].nodeID);
	}

	//start the waitNbrs thread to wait for incoming connections from neighbors with larger node IDs
	pthread_t waitNbrs_thread;
	pthread_create(&waitNbrs_thread,NULL,waitNbrs,(void*)0);

	//wait for other nodes to start
	sleep(OVERLAY_START_DELAY);

	//connect to neighbors with smaller node IDs
	connectNbrs();

	//wait for waitNbrs thread to return
	pthread_join(waitNbrs_thread,NULL);

	//at this point, all connections to the neighbors are created

	//create threads listening to all the neighbors
	for(i=0;i<nbrNum;i++) {
		int* idx = (int*)malloc(sizeof(int));
		*idx = i;
		pthread_t nbr_listen_thread;
		pthread_create(&nbr_listen_thread,NULL,listen_to_neighbor,(void*)idx);
	}
	printf("Overlay: node initialized...\n");
	printf("Overlay: waiting for connection from SNP process...\n");

	//waiting for connection from  SNP process
	waitNetwork();
}
