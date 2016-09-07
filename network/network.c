//FILE: network/network.c
//
//Description: this file implements network layer process
//
//Date: April 29,2008

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "network.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

/**************************************************************/
// macros
/**************************************************************/
#define NETWORK_WAITTIME 10 //network layer waits this time for establishing the routing paths
#define MAX 128

/**************************************************************/
//delare global variables
/**************************************************************/
int overlay_conn; 			//connection to the overlay
int transport_conn;			//connection to the transport
nbr_cost_entry_t* nct;			//neighbor cost table
dv_t* dv;				//distance vector table
pthread_mutex_t* dv_mutex;		//dvtable mutex#define _DEFAULT_SOURCE
routingtable_t* routingtable;		//routing table
pthread_mutex_t* routingtable_mutex;	//routingtable mutex
int why_killed = 0;		// variable to indicate whether SNP or some ON has terminated.
						// If 0, then ON has terminated, and if 1, SNP has terminated.
int listenfd = -1; // listening socket descriptor for waitTransport()
pthread_t main_thread = -1; // main thread id
/**************************************************************/
//implementation network layer functions
/**************************************************************/


/*
 * freeMemory - Free memory.
 *
 * Frees the nct, dv, and routingtable structures.
 *
 */
void freeMemory() {

	if (nct != NULL) {
		nbrcosttable_destroy(nct);
		nct = NULL;
	}

	if (dv != NULL) {
		pthread_mutex_lock(dv_mutex);
		dvtable_destroy(dv);
		pthread_mutex_unlock(dv_mutex);
		dv = NULL;
	}

	if (routingtable != NULL) {
		pthread_mutex_lock(routingtable_mutex);
		routingtable_destroy(routingtable);
		pthread_mutex_unlock(routingtable_mutex);
		routingtable = NULL;
	}

	if (dv_mutex != NULL) {
		pthread_mutex_destroy(dv_mutex);
		free(dv_mutex);
		dv_mutex = NULL;
	}

	if (routingtable_mutex != NULL) {
		pthread_mutex_destroy(routingtable_mutex);
		free(routingtable_mutex);
		routingtable_mutex = NULL;
	}
}

//This function is used to for the SNP process to connect to the local ON process on port OVERLAY_PORT.
//TCP descriptor is returned if success, otherwise return -1.
int connectToOverlay() {

	// Declare variables.
	int sockfd;
	struct sockaddr_in servaddr;

	// Create socket.
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd<0) {
		perror("Socket error");
		return -1;
	}

	// Get local hostname.
	char hostname[MAX];
	memset(hostname, 0, sizeof(hostname));
	gethostname(hostname, sizeof(hostname));

	// Get IP of the local host.
	struct hostent *he;
	struct in_addr **addr_list;
	he = gethostbyname(hostname);
	addr_list = (struct in_addr **)he->h_addr_list;

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = addr_list[0]->s_addr;
	servaddr.sin_port = htons(OVERLAY_PORT); // Convert to big-endian.

	// Connect to the local ON.
	if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))<0) {
		printf("Problem in connecting to the local ON process.\n");
		perror("What is going on???");
		return -1;
	}
	return sockfd;
}

//This thread sends out route update packets every ROUTEUPDATE_INTERVAL time
//The route update packet contains this node's distance vector.
//Broadcasting is done by set the dest_nodeID in packet header as BROADCAST_NODEID
//and use overlay_sendpkt() to send the packet out using BROADCAST_NODEID address.
void* routeupdate_daemon(void* arg) {

	// Declare and initialize variables.
	int N = topology_getNodeNum();
	int *neighborArray = topology_getNbrArray();

  	while (1) {
		// Sleep.
		sleep(ROUTEUPDATE_INTERVAL);

		// Create and initialize a route_update packet.
		snp_pkt_t packet;
		memset(&packet, 0, sizeof(snp_pkt_t));

		packet.header.src_nodeID = topology_getMyNodeID();
		packet.header.dest_nodeID = BROADCAST_NODEID;
		packet.header.length = sizeof(pkt_routeupdate_t);
		packet.header.type = ROUTE_UPDATE;

		pkt_routeupdate_t rp;
		memset(&rp, 0, sizeof(pkt_routeupdate_t));

		rp.entryNum = N;
		// Input this node's distance vector into route update entry structure.
		for (int i=0; i<N; i++) {
			if (dv == NULL) { // SNP needs to be terminated.
				break;
			}
			else {
				pthread_mutex_lock(dv_mutex);
				memset(&rp.entry[i], 0, sizeof(routeupdate_entry_t));
				rp.entry[i].nodeID = dv[0].dvEntry[i].nodeID;
				rp.entry[i].cost = dv[0].dvEntry[i].cost;
				pthread_mutex_unlock(dv_mutex);
			}
		}
		memcpy(packet.data, &rp, sizeof(pkt_routeupdate_t));

		// Send packet to the local ON.
		int ret = overlay_sendpkt(BROADCAST_NODEID, &packet, overlay_conn);

		if (ret<0) {
			if (why_killed == 0) { // Local ON has terminated.
				printf("ROUTE_UPDATE_DAEMON: ON has stopped, so terminate thread.\n");
				if (overlay_conn != -1) {
					close(overlay_conn);
					overlay_conn = -1;
				}
				if (transport_conn != -1) {
					close(transport_conn);
					transport_conn = -1;
				}
				if (listenfd != -1) {
					close(listenfd);
					listenfd = -1;
				}

				if (main_thread != -1) {
					pthread_cancel(main_thread);
					main_thread = -1;
				}

				// Clean up.
				free(neighborArray);
				freeMemory();
				pthread_exit(NULL);
			}
			else { // SNP has terminated.
				printf("ROUTE_UPDATE_DAEMON: SNP has received a SIGINT, so terminate thread.\n");
				if (overlay_conn != -1) {
					close(overlay_conn);
					overlay_conn = -1;
				}
				if (transport_conn != -1) {
					close(transport_conn);
					transport_conn = -1;
				}
				if (listenfd != -1) {
					close(listenfd);
					listenfd = -1;
				}

				// Clean up.
				free(neighborArray);
				pthread_exit(NULL);
			}
		}
		else {
			printf("ROUTE_UPDATE_DAEMON: SNP has successfully sent packet to local ON.\n");
		}
	}
  	return 0;
}

//This thread handles incoming packets from the ON process.
//It receives packets from the ON process by calling overlay_recvpkt().
//If the packet is a SNP packet and the destination node is this node, forward the packet to the SRT process.
//If the packet is a SNP packet and the destination node is not this node, forward the packet to the next hop according to the routing table.
//If this packet is an Route Update packet, update the distance vector table and the routing table.
void* pkthandler(void* arg) {

	// Declare and initialize variables.
	snp_pkt_t pkt;
	memset(&pkt, 0, sizeof(snp_pkt_t));
	int myNodeID = topology_getMyNodeID();
	int N = topology_getNodeNum();
	int neighborNum = topology_getNbrNum();
	int *neighborArray = topology_getNbrArray();

	// Keep on receiving packets from the local ON.
	while(overlay_recvpkt(&pkt,overlay_conn)>0) {
		printf("PKTHANDLER: received a packet from neighbor %d\n",pkt.header.src_nodeID);

		if (pkt.header.type == SNP) {
			// Case 1. Packet is an SNP packet and the destination node is this node.
			// Forward the packet to the SRT layer.
			if (pkt.header.dest_nodeID == myNodeID) {

				sendseg_arg_t send_arg;
				memset(&send_arg, 0, sizeof(sendseg_arg_t));
				memset(&send_arg.seg, 0, sizeof(seg_t));
				send_arg.nodeID = pkt.header.src_nodeID;

				memcpy(&send_arg.seg, pkt.data, sizeof(seg_t));

				// Forward the structure to the local SRT.
				int ret = forwardsegToSRT(transport_conn, send_arg.nodeID, &send_arg.seg);

				if (ret < 0) {
					if (why_killed == 0) { // SRT has terminated.
						printf("PKTHANDLER: SRT has terminated, so terminate thread.\n");
						if (transport_conn != -1) {
							close(transport_conn);
							transport_conn = -1;
						}

						free(neighborArray);
						pthread_exit(NULL);
					}
					else { // SNP has terminated.
						printf("PKTHANDLER: SNP has terminated, so terminate thread.\n");
						if (overlay_conn != -1) {
							close(overlay_conn);
							overlay_conn = -1;
						}
						if (transport_conn != -1) {
							close(transport_conn);
							transport_conn = -1;
						}
						if (listenfd != -1) {
							close(listenfd);
							listenfd = -1;
						}

						free(neighborArray);
						pthread_exit(NULL);
					}
				}
				else {
					printf("PKTHANDLER: Forwarded SNP packet to SRT.\n");
				}
			}
			// Case 2. Packet is an SNP packet and the destination node is not this node.
			// Send the packet to the local ON to send to the next hop node.
			else {
				int nextNodeID = pkt.header.dest_nodeID;

				// Get the next hop node ID from the routing table.
				pthread_mutex_lock(routingtable_mutex);
				int nextHopNodeID = routingtable_getnextnode(routingtable, nextNodeID);
				pthread_mutex_unlock(routingtable_mutex);
				if (nextHopNodeID == -1) {
					printf("PKTHANDLER: The routing table does not contain the entry for the given destination node ID.\n");
				}
				else {
					int ret = overlay_sendpkt(nextHopNodeID, &pkt, overlay_conn);

					if (ret < 0) {
						if (why_killed == 0) { // ON has been terminated.
							printf("PKTHANDLER: ON has terminated, so terminate thread.\n");
							if (overlay_conn != -1) {
								close(overlay_conn);
								overlay_conn = -1;
							}
							if (transport_conn != -1) {
								close(transport_conn);
								transport_conn = -1;
							}
							if (listenfd != -1) {
								close(listenfd);
								listenfd = -1;
							}

							free(neighborArray);
							pthread_exit(NULL);
						}
						else { // SNP has been terminated.
							printf("PKTHANDLER: SNP has been terminated, so terminate thread.\n");
							if (overlay_conn != -1) {
								close(overlay_conn);
								overlay_conn = -1;
							}
							if (transport_conn != -1) {
								close(transport_conn);
								transport_conn = -1;
							}
							if (listenfd != -1) {
								close(listenfd);
								listenfd = -1;
							}

							free(neighborArray);
							pthread_exit(NULL);
						}
					}
				}
			}
		}
		// Case 3. Packet is a Route Update packet.
		// Update dv table and routing table.
		else {
			int flag = 1; // variable to exit out of the while loop in edge cases
			pthread_mutex_lock(dv_mutex);

			pkt_routeupdate_t route_pkt;
			memset(&route_pkt, 0, sizeof(pkt_routeupdate_t));
			memcpy(&route_pkt, pkt.data, sizeof(pkt_routeupdate_t));

			// Copy the dv entry to the dv table.
			for (int i=0; i<neighborNum+1; i++) {
				if (dv == NULL) { // SNP needs to be terminated.
					pthread_mutex_unlock(dv_mutex);
					flag = 0;
					break;
				}

				if (dv[i].nodeID == pkt.header.src_nodeID) { // We have found the correct entry.
					for (int j=0; j<N; j++) {
						dvtable_setcost(dv, dv[i].nodeID, dv[i].dvEntry[j].nodeID, route_pkt.entry[j].cost);
					}
					break;
				}
			}

			// If flag == 0, then we have reached an edge case and we exit of the loop.
			if (flag == 0) {
				break;
			}

			// Update own distance vector in the dv table.
			for (int i=0; i<N; i++) {

				// If the dest node is own node, then skip.
				if (dv[0].dvEntry[i].nodeID == dv[0].nodeID) {
					dv[0].dvEntry[i].cost = 0;
					continue;
				}

				// Get the minimum path.
				for (int j=0; j<neighborNum; j++) {
					int cost = nbrcosttable_getcost(nct, neighborArray[j]);
					cost += dvtable_getcost(dv, neighborArray[j], dv[0].dvEntry[i].nodeID);

					// If computed cost is less than the current cost in dv, then we update the cost in the dv.
					if (cost < dvtable_getcost(dv, dv[0].nodeID, dv[0].dvEntry[i].nodeID)) {

						dvtable_setcost(dv, dv[0].nodeID, dv[0].dvEntry[i].nodeID, cost);

						// Update the routingtable's next hop node for the current dest node.
						pthread_mutex_lock(routingtable_mutex);
						routingtable_setnextnode(routingtable, dv[0].dvEntry[i].nodeID, neighborArray[j]);
						pthread_mutex_unlock(routingtable_mutex);
					}
				}
			}
			pthread_mutex_unlock(dv_mutex);
		}
	}

	if (why_killed == 0) { // Local ON has terminated.
		printf("PKTHANDLER: ON has stopped, so terminate thread.\n");
		if (overlay_conn != -1) {
			close(overlay_conn);
			overlay_conn = -1;
		}
		if (transport_conn != -1) {
			close(transport_conn);
			transport_conn = -1;
		}
		if (listenfd != -1) {
			close(listenfd);
			listenfd = -1;
		}

		// Clean up.
		free(neighborArray);
		freeMemory();
		pthread_exit(NULL);
	}
	else { // SNP has terminated.
		printf("PKTHANDLER: SNP has received a SIGINT, so terminate thread.\n");
		if (overlay_conn != -1) {
			close(overlay_conn);
			overlay_conn = -1;
		}
		if (transport_conn != -1) {
			close(transport_conn);
			transport_conn = -1;
		}
		if (listenfd != -1) {
			close(listenfd);
			listenfd = -1;
		}

		// Clean up.
		free(neighborArray);
		pthread_exit(NULL);
	}

  	return 0;
}

//This function stops the SNP process.
//It closes all the connections and frees all the dynamically allocated memory.
//It is called when the SNP process receives a signal SIGINT.
void network_stop() {
	why_killed = 1;

	// Close connections.
	if (overlay_conn != -1) {
		close(overlay_conn);
		overlay_conn = -1;
	}
	if (transport_conn != -1) {
		close(transport_conn);
		transport_conn = -1;
	}
	if (listenfd != -1) {
		close(listenfd);
		listenfd = -1;
	}

	// Free memory.
	freeMemory();

	pthread_exit(NULL);
}

//This function opens a port on NETWORK_PORT and waits for the TCP connection from local SRT process.
//After the local SRT process is connected, this function keeps receiving sendseg_arg_ts which contains the segments and their destination node addresses from the SRT process. The received segments are then encapsulated into packets (one segment in one packet), and sent to the next hop using overlay_sendpkt. The next hop is retrieved from routing table.
//When a local SRT process is disconnected, this function waits for the next SRT process to connect.
void waitTransport() {
	//put your code here

	// Declare and initialize variables.
	struct sockaddr_in tcp_addr;
	struct sockaddr_in node_addr;
	socklen_t node_addr_len;
	int myNodeID = topology_getMyNodeID();

	// Create socket.
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd<0) {
		perror("Socket error");
		return;
	}

	memset(&tcp_addr, 0, sizeof(tcp_addr));
	tcp_addr.sin_family = AF_INET;
	tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcp_addr.sin_port = htons(NETWORK_PORT);

	// Bind socket.
	if(bind(listenfd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr))< 0) {
		perror("binding error");
		return;
	}

	// Listen for incoming connection from the SRT.
	if(listen(listenfd, 1) < 0) {
		perror("listening error:");
		return;
	}

	printf("waiting for connection\n");

	// Accept SRT connection.
	node_addr_len = sizeof(struct sockaddr_in);
	transport_conn = accept(listenfd,(struct sockaddr*)&node_addr, &node_addr_len);

	if (transport_conn<0) {
		printf("Error in accepting connection from local SRT.\n");
		return;
	}

	printf("%s\n", "Received request from local SRT.\n");

	while (1) {

		// Get a sendseg_arg_t packet from the local SRT.
		sendseg_arg_t arg;
		memset(&arg, 0, sizeof(sendseg_arg_t));
		memset(&arg.seg, 0, sizeof(seg_t));

		// Receive packet from SRT.
		int ret = getsegToSend(transport_conn, &(arg.nodeID), &(arg.seg));

		if (ret<0) {
			if (why_killed == 0) { // SRT has terminated.
				printf("WAIT_TRANSPORT: SRT has terminated, so listen for another incoming SRT connection.\n");
				if (transport_conn != -1) {
					close(transport_conn);
					transport_conn = -1;
				}
				transport_conn = accept(listenfd,(struct sockaddr*)&node_addr, &node_addr_len);
			}
			else { // Local SNP has terminated.
				printf("WAIT_TRANSPORT: SNP has terminated, so terminate function.\n");
				transport_conn = -1;
				return;
			}
		}
		else {
			// Encapsulate received sendseg_arg_t structure into packet.
			sendpkt_arg_t sp_arg;
			memset(&sp_arg, 0, sizeof(sendpkt_arg_t));
			memset(&sp_arg.pkt, 0, sizeof(snp_pkt_t));

			pthread_mutex_lock(routingtable_mutex);
			sp_arg.nextNodeID = routingtable_getnextnode(routingtable, arg.nodeID);
			pthread_mutex_unlock(routingtable_mutex);

			sp_arg.pkt.header.src_nodeID = myNodeID;
			sp_arg.pkt.header.dest_nodeID = arg.nodeID;
			sp_arg.pkt.header.length = sizeof(seg_t);
			sp_arg.pkt.header.type = SNP;
			memcpy(&sp_arg.pkt.data, &arg.seg, sizeof(seg_t));

			// Forward structure to the local ON layer to be sent to the next hop ON node.
			if (overlay_sendpkt(sp_arg.nextNodeID, &sp_arg.pkt, overlay_conn)<0) {
				if (why_killed == 0) { // Local ON terminates.
					printf("WAIT_TRANSPORT: ON has terminated, so cannot send packet to the ON.\n");
					if (overlay_conn != -1) {
						close(overlay_conn);
						overlay_conn = -1;
					}
					continue;
				}
				else { // Local SNP terminates.
					printf("WAIT_TRANSPORT: SNP has terminated, so terminate function.\n");
					return;
				}
			}
			else {
				printf("WAIT_TRANSPORT: Forwarded packet from SNP to ON\n");
			}
		}
	}
}

int main(int argc, char *argv[]) {
	printf("network layer is starting, pls wait...\n");

	//initialize global variables
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	overlay_conn = -1;
	transport_conn = -1;
	main_thread = pthread_self();

	// Print tables before update.
	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//register a signal handler which is used to terminate the process
	signal(SIGINT, network_stop);

	//connect to local ON process
	overlay_conn = connectToOverlay();
	if(overlay_conn<0) {
		printf("can't connect to overlay process\n");
		exit(1);
	}

	//start a thread that handles incoming packets from ON process
	pthread_t pkt_handler_thread;
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);
	pthread_detach(pkt_handler_thread);

	//start a route update thread
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);
	pthread_detach(routeupdate_thread);

	printf("network layer is started...\n");

	printf("waiting for routes to be established\n");
	sleep(NETWORK_WAITTIME);
	routingtable_print(routingtable);

	//wait connection from SRT process
	printf("waiting for connection from SRT process\n");
	waitTransport();

	return 0;
}
