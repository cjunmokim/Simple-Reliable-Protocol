
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//This function creates a dvtable(distance vector table) dynamically.
//A distance vector table contains the n+1 entries, where n is the number of the neighbors of this node, and the rest one is for this node itself.
//Each entry in distance vector table is a dv_t structure which contains a source node ID and an array of N dv_entry_t structures where N is the number of all the nodes in the overlay.
//Each dv_entry_t contains a destination node address the the cost from the source node to this destination node.
//The dvtable is initialized in this function.
//The link costs from this node to its neighbors are initialized using direct link cost retrived from topology.dat.
//Other link costs are initialized to INFINITE_COST.
//The dynamically created dvtable is returned.
dv_t* dvtable_create()
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int *neighborArray = topology_getNbrArray();
	int myNodeID = topology_getMyNodeID();
	int N = topology_getNodeNum();
	int *nodeArray = topology_getNodeArray();

	// Dynamically allocate memory to the dv table and dvEntry of each dv.
	dv_t *dvtable = (dv_t *)calloc(neighborNum+1, sizeof(dv_t));
	for (int i=0; i<neighborNum+1; i++) {
		dvtable[i].dvEntry = (dv_entry_t *)calloc(N, sizeof(dv_entry_t));
	}

	// Create own distance vector.
	dvtable[0].nodeID = myNodeID;
	for (int i=0; i<N; i++) {
		dvtable[0].dvEntry[i].nodeID = nodeArray[i];
		if (nodeArray[i] == myNodeID) { // Destination node is this node.
			dvtable[0].dvEntry[i].cost = 0;
		}
		else {
			dvtable[0].dvEntry[i].cost = topology_getCost(myNodeID, nodeArray[i]);
		}
	}

	// Create a distance vector for each neighbor.
	for (int i=1; i<neighborNum+1; i++) {
		dvtable[i].nodeID = neighborArray[i-1]; // Src node is the neighbor node.

		// Loop through each node in the dv entry and initialize cost to INFINITE_COST.
		for (int j=0; j<N; j++) {
			dvtable[i].dvEntry[j].nodeID = nodeArray[j];
			dvtable[i].dvEntry[j].cost = INFINITE_COST;
		}
	}

	// Clean up.
	free(neighborArray);
	free(nodeArray);

	return dvtable;
}

//This function destroys a dvtable.
//It frees all the dynamically allocated memory for the dvtable.
void dvtable_destroy(dv_t* dvtable)
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();

	// Loop through the dv table, freeing ech dv entry.
	for (int i=0; i<neighborNum+1; i++) {
		free(dvtable[i].dvEntry);
	}
	free(dvtable); // Free the whole table.
	dvtable = NULL;

	return;
}

//This function sets the link cost between two nodes in dvtable.
//If those two nodes are found in the table and the link cost is set, return 1.
//Otherwise, return -1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int N = topology_getNodeNum();
	int reason = -1;

	// Find the distance vector with the correct source node ID.
	for (int i=0; i<neighborNum+1; i++) {
		if (dvtable[i].nodeID == fromNodeID) {
			reason = 0;

			// Loop through the dv entry to find the correct dest node ID.
			for (int j=0; j<N; j++) {
				if (dvtable[i].dvEntry[j].nodeID == toNodeID) {
					reason = 1;

					// Set the correct cost.
					dvtable[i].dvEntry[j].cost = cost;
					return 1;
				}
			}
		}
	}

	// Either the fromNodeID or the toNodeID is not in the dv table.
	if (reason == -1) {
		printf("ERROR: dv table does not contain fromNodeID.\n");
	}
	else if (reason == 0) {
		printf("ERROR: dv table does not contain toNodeID.\n");
	}
	return -1;
}

//This function returns the link cost between two nodes in dvtable
//If those two nodes are found in dvtable, return the link cost.
//otherwise, return INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int N = topology_getNodeNum();

	// Loop through the dv table and find the correct src node ID.
	for (int i=0; i<neighborNum+1; i++) {
		if (dvtable[i].nodeID == fromNodeID) {

			// Loop through the dv entry and find the correct dest node ID.
			for (int j=0; j<N; j++) {
				if (dvtable[i].dvEntry[j].nodeID == toNodeID) {
					return dvtable[i].dvEntry[j].cost;
				}
			}
		}
	}

	// Either the fromNodeID or the toNodeID is not in the dv table.
	return INFINITE_COST;
}

//This function prints out the contents of a dvtable.
void dvtable_print(dv_t* dvtable)
{

	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int N = topology_getNodeNum();

	printf("\nDistance Vector Table\n");

	// Loop through the table to print out each entry.
	for (int i=0; i<neighborNum+1; i++) {
		printf("#%d src node ID, dvEntry(dest node ID, cost)\n", i);
		for (int j=0; j<N; j++) {
			printf("	%d, (%d, %d)\n", dvtable[i].nodeID, dvtable[i].dvEntry[j].nodeID, dvtable[i].dvEntry[j].cost);
		}
	}

	return;
}
