//FILE: overlay/neighbortable.c
//
//Description: this file the API for the neighbor table
//
//Date: May 03, 2010

#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* memset */
#include <unistd.h> /* close */
#include "neighbortable.h"
#include "../topology/topology.h"
#include "../common/constants.h"

//This function first creates a neighbor table dynamically. It then parses the topology/topology.dat file and fill the nodeID and nodeIP fields in all the entries, initialize conn field as -1 .
//return the created neighbor table
nbr_entry_t* nt_create()
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int *neighborArray = topology_getNbrArray();
	char **neighborNames = topology_getNbrNameArray();

	// Create and initialize neighbor table.
	nbr_entry_t *table = (nbr_entry_t *)calloc(1, sizeof(nbr_entry_t) * neighborNum);

	// Loop through the neighbor table, adding entries.
	for (int i=0; i<neighborNum; i++) {
		table[i].nodeID = neighborArray[i];

		// Get IP of neighbor node.
		struct hostent *he;
		struct in_addr **addr_list;
		he = gethostbyname(neighborNames[i]);
		addr_list = (struct in_addr **)he->h_addr_list;
		table[i].nodeIP = addr_list[0]->s_addr;

		table[i].conn = -1;
	}

	// Clean up.
	free(neighborArray);
	for (int i=0; i<neighborNum; i++) {
		free(neighborNames[i]);
	}
	free(neighborNames);

	return table;
}

//This function destroys a neighbortable. It closes all the connections and frees all the dynamically allocated memory.
void nt_destroy(nbr_entry_t* nt)
{
	int neighborNum = topology_getNbrNum();

	// Loop through the neighbor table, closing connections.
	for (int i=0; i<neighborNum; i++) {
		if (nt[i].conn != -1) {
			close(nt[i].conn);
			nt[i].conn = -1;
		}
	}

	// Free the neighbor table.
	free(nt);

  	return;
}

//This function is used to assign a TCP connection to a neighbor table entry for a neighboring node. If the TCP connection is successfully assigned, return 1, otherwise return -1
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
	int neighborNum = topology_getNbrNum();

	// Loop through the neighbor table, assigning TCP connection to each entry.
	for (int i=0; i<neighborNum; i++) {
		if (nt[i].nodeID == nodeID) {
			nt[i].conn = conn;
			return 1;
		}
	}
  	return -1;
}
