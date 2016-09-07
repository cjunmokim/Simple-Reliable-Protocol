
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//This function creates a neighbor cost table dynamically
//and initialize the table with all its neighbors' node IDs and direct link costs.
//The neighbors' node IDs and direct link costs are retrieved from topology.dat file.
nbr_cost_entry_t* nbrcosttable_create()
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int *neighborArray = topology_getNbrArray();
	int myNodeID = topology_getMyNodeID();

	// Dynamically allocate memory to the nbr cost table.
	nbr_cost_entry_t *nct = (nbr_cost_entry_t *)calloc(neighborNum, sizeof(nbr_cost_entry_t));

	// Initialize table with information from topology.dat
	for (int i=0; i<neighborNum; i++) {
		nct[i].nodeID = neighborArray[i];
		nct[i].cost = topology_getCost(myNodeID, neighborArray[i]);
	}

	// Clean up.
	free(neighborArray);

  	return nct;
}

//This function destroys a neighbor cost table.
//It frees all the dynamically allocated memory for the neighbor cost table.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
	free(nct);
	return;
}

//This function is used to get the direct link cost from neighbor.
//The direct link cost is returned if the neighbor is found in the table.
//INFINITE_COST is returned if the node is not found in the table.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();

	// Loop through the table to find the neighbor with the matching nodeID.
	for (int i=0; i<neighborNum; i++) {
		if (nct[i].nodeID == nodeID) {
			return nct[i].cost;
		}
	}

	return INFINITE_COST; // There is no entry with the matching nodeID.
}

//This function prints out the contents of a neighbor cost table.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();

	printf("\nNeighbor Cost Table\n");

	// Loop through the table to print out each entry.
	for (int i=0; i<neighborNum; i++) {
		printf("#%d nodeID cost --> %d %d\n", i, nct[i].nodeID, nct[i].cost);
	}

	return;
}
