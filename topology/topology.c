//FILE: topology/topology.c
//
//Description: this file implements some helper functions used to parse
//the topology file
//
//Date: May 3,2010

#define _DEFAULT_SOURCE

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<errno.h>
#include<netdb.h>
#include<arpa/inet.h>
#include <unistd.h>
#include "topology.h"
#include "../common/constants.h"

#define FILEPATH "/net/tahoe3/momoney/cs60/lab6/topology/topology.dat"
#define MAX 128

char *names[MAX_NODE_NUM]; // String array to hold all the node names.

//this function returns node ID of the given hostname
//the node ID is an integer of the last 8 digit of the node's IP address
//for example, a node with IP address 202.120.92.3 will have node ID 3
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromname(char* hostname)
{
	// Check that the input is valid.
	if (hostname == NULL) {
		return -1;
	}

	struct hostent *he;
	struct in_addr **addr_list;

	// Get host info.
	if ((he=gethostbyname(hostname)) == NULL) {
		printf("The hostname is invalid.\n");
		return -1;
	}

	addr_list = (struct in_addr **)he->h_addr_list;

	return topology_getNodeIDfromip(addr_list[0]);

}

//this function returns node ID from the given IP address
//if the node ID can't be retrieved, return -1
int topology_getNodeIDfromip(struct in_addr* addr)
{

	char ip[MAX];
	strcpy(ip, inet_ntoa(*addr)); //Get the IP address.

	char *token;
	char *delim = ".";
	token = strtok(ip, delim);

	// Get the last part in the IP address.
	for (int i=0; i<3; i++) {
		token = strtok(NULL, delim);
	}

	int nodeID = atoi(token);
  	return nodeID;

}

//this function returns my node ID
//if my node ID can't be retrieved, return -1
int topology_getMyNodeID()
{
	char hostname[MAX];
	memset(hostname, 0, sizeof(hostname));
	gethostname(hostname, sizeof(hostname)); // Get local host name.

	return topology_getNodeIDfromname(hostname);
}

//this functions parses the topology information stored in topology.dat
//returns the number of neighbors
int topology_getNbrNum()
{
	// Get local host name.
	char hostname[MAX];
	gethostname(hostname, sizeof(hostname));

	// Open topology.dat file.
	FILE *fp;
	char buf[MAX];
	fp = fopen(FILEPATH, "r");

	if (fp == NULL) {
		printf("bad path.\n");
		return EXIT_FAILURE;
	}

	char *token;
	char *delim = " ";

	int neighborNum = 0;

	while (fgets(buf, MAX, fp) != NULL) {

		token = strtok(buf, delim);
		while (token != NULL) {
			// If token == local host name, then the other host name in the line
			// should be a neighbor. So increment neighborNum.
			if (strcmp(token, hostname) == 0) {
				neighborNum++;
				break;
			}
			token = strtok(NULL, delim);
		}
	}

	fclose(fp);
  	return neighborNum;
}

//this functions parses the topology information stored in topology.dat
//returns the number of total nodes in the overlay
int topology_getNodeNum()
{
	// Create the node array that holds the names of all distinct nodes in the network.
	if (getNodeArray() < 0) {
		printf("Error in creating the node array to hold node names.\n");
		return EXIT_FAILURE;
	}

	int numNodes = 0;

	// Count number of nodes in the network.
	for (int i=0; i<MAX_NODE_NUM; i++) {
		if (names[i] != NULL) {
			numNodes++;
		}
		else {
			break;
		}
	}

	// Free the node array.
	freeNodeArray();

  	return numNodes;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the nodes' IDs in the overlay network
int* topology_getNodeArray()
{
	// Get the total number of nodes.
	int nodeNum = topology_getNodeNum();
	getNodeArray(); // Get the array of all node host names.

	int *nodeIDs = (int *)calloc(1, sizeof(int) * nodeNum);

	// Fill nodeIDs array with the correct IP from names[].
	for (int i=0; i<nodeNum; i++) {
		nodeIDs[i] = topology_getNodeIDfromname(names[i]);
	}

	// Free the node array.
	freeNodeArray();

  	return nodeIDs;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors'IDs
int* topology_getNbrArray()
{
	int neighborNum = topology_getNbrNum();
	int *neighborIDs = (int *)calloc(1, sizeof(int) * neighborNum);
	char hostname[MAX] = {0};

	// Get local host name.
	char buf[MAX];
	gethostname(hostname, sizeof(hostname));

	// Open topology.dat file.
	FILE *fp;
	fp = fopen(FILEPATH, "r");
	if (fp == NULL) {
		printf("bad path.\n");
		return (int *)NULL;
	}

	char *token, *token2;
	char *delim = " ";
	int index = 0;

	while (fgets(buf, MAX, fp) != NULL) {

		token = strtok(buf, delim); // first host name
		token2 = strtok(NULL, delim); // second host name

		// Add to neighborIDs the correct NodeID.
		if (strcmp(token, hostname) == 0) {
			neighborIDs[index] = topology_getNodeIDfromname(token2);
			index++;
		}
		else if (strcmp(token2, hostname) == 0) {
			neighborIDs[index] = topology_getNodeIDfromname(token);
			index++;
		}
	}

	// Close file.
	fclose(fp);

  	return neighborIDs;
}

//this functions parses the topology information stored in topology.dat
//returns a dynamically allocated array which contains all the neighbors' hostnames.
char **topology_getNbrNameArray()
{
	int neighborNum = topology_getNbrNum();
	char **neighborNames = (char **)calloc(neighborNum, sizeof(char *));
	for (int i=0; i<neighborNum; i++) {
		neighborNames[i] = (char *)calloc(MAX, 1);
	}

	// Get local host name.
	char hostname[MAX];
	memset(hostname, 0, sizeof(hostname));
	gethostname(hostname, sizeof(hostname));

	// Open topology.dat file.
	FILE *fp;
	char buf[MAX];

	fp = fopen(FILEPATH, "r");
	if (fp == NULL) {
		printf("bad path.\n");
		return (char **)NULL;
	}

	char *token, *token2;
	char *delim = " ";
	int index = 0;

	while (fgets(buf, MAX, fp) != NULL) {

		token = strtok(buf, delim); // first host name.
		token2 = strtok(NULL, delim); // second host name.

		// Copy the correct host name to the neighbor array.
		if (strcmp(token, hostname) == 0) {
			memcpy(neighborNames[index], token2, MAX);
			index++;
		}
		else if (strcmp(token2, hostname) == 0) {
			memcpy(neighborNames[index], token, MAX);
			index++;
		}
	}

	// Close file.
	fclose(fp);

	return neighborNames;

}

//this functions parses the topology information stored in topology.dat
//returns the cost of the direct link between the two given nodes
//if no direct link between the two given nodes, INFINITE_COST is returned
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
	// Open topology.dat file.
	FILE *fp;
	char buf[MAX];
	fp = fopen(FILEPATH, "r");

	char *token, *token2, *token3;
	char *delim = " ";
	int cost = INFINITE_COST;

	while (fgets(buf, MAX, fp) != NULL) {
		token = strtok(buf, delim); // first host name.
		token2 = strtok(NULL, delim); // second host name.
		token3 = strtok(NULL, delim); // cost.

		int n1 = topology_getNodeIDfromname(token);
		int n2 = topology_getNodeIDfromname(token2);

		if (n1 == fromNodeID && n2 == toNodeID) {
			cost = atof(token3);
			break;
		}
		else if (n2 == fromNodeID && n1 == toNodeID) {
			cost = atof(token3);
			break;
		}
	}

	fclose(fp);
	fp = NULL;

	return (unsigned int)cost;

}

/*
 * getNodeArray - Create array of all node names.
 *
 * This function reads in topology.dat, and parses it for all the distinct
 * node names in the overlay network. It then puts this information in the
 * global names[] variable.
 *
 * If successful, returns EXIT_SUCCESS, and if not, returns EXIT_FAILURE.
 *
 */
int getNodeArray() {

	// Open topology.dat file.
	FILE *fp;
	char buf[MAX];
	fp = fopen(FILEPATH, "r");

	if (fp == NULL) {
		printf("bad path.\n");
		return EXIT_FAILURE;
	}

	char *token;
	char *delim = " ";
	int arrayLen = 0;

	while (fgets(buf, MAX, fp) != NULL) {
		int flag = 1;
		token = strtok(buf, delim);

		for (int j=0; j<2; j++) {

			// If names[] is empty, then add the first token.
			if (arrayLen == 0) {
				names[0] = (char *)calloc(1, MAX);
				strcpy(names[0], token);
				arrayLen++;
			}
			else { // Otherwise, loop through names[] and check that token is not in it.
				for (int i=0; i<arrayLen; i++) {
					if (strcmp(token, names[i]) == 0) {
						flag = 0;
						break;
					}
				}

				// flag == 1 means that token is not yet in names[] so add it.
				if (flag) {
					names[arrayLen] = (char *)calloc(1, MAX);
					strcpy(names[arrayLen], token);
					arrayLen++;
				}
			}
			token = strtok(NULL, delim);
		}
	}

	// CLose file.
	fclose(fp);

	return EXIT_SUCCESS;

}


/*
 * freeNodeArray - Free array of all node names.
 *
 * This function frees names[] variable set in getNodeArray().
 *
 */
void freeNodeArray() {

	// Loop through names and free each string.
	for (int i=0; i<MAX_NODE_NUM; i++) {
		if (names[i] != NULL) {
			free(names[i]);
			names[i] = NULL;
		}
		else {
			break;
		}
	}
}
