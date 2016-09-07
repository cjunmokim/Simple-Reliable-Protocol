

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "routingtable.h"

//This is the hash function used the by the routing table
//It takes the hash key - destination node ID as input,
//and returns the hash value - slot number for this destination node ID.
//
//You can copy makehash() implementation below directly to routingtable.c:
//int makehash(int node) {
//	return node%MAX_ROUTINGTABLE_SLOTS;
//}
//
int makehash(int node)
{
	return node%MAX_ROUTINGTABLE_SLOTS;
}

//This function creates a routing table dynamically.
//All the entries in the table are initialized to NULL pointers.
//Then for all the neighbors with a direct link, create a routing entry using the neighbor itself as the next hop node, and insert this routing entry into the routing table.
//The dynamically created routing table structure is returned.
routingtable_t* routingtable_create()
{
	// Declare and initialize variables.
	int neighborNum = topology_getNbrNum();
	int *neighborArray = topology_getNbrArray();

	// Dynamically allocate memory for a routing table.
	routingtable_t *routingtable = (routingtable_t *)calloc(1, sizeof(routingtable_t));

	// Initialize all slots to NULL.
	for (int i=0; i<MAX_ROUTINGTABLE_SLOTS; i++) {
		routingtable->hash[i] = (routingtable_entry_t *)NULL;
	}

	// Create routingtable entries for neighbors.
	for (int i=0; i<neighborNum; i++) {

		// Make a routing table entry.
		routingtable_entry_t *entry = (routingtable_entry_t *)calloc(1, sizeof(routingtable_entry_t));
		entry->destNodeID = neighborArray[i];
		entry->nextNodeID = neighborArray[i];
		entry->next = (routingtable_entry_t *)NULL;

		// Calculate the hash number for this entry.
		int slot = makehash(entry->destNodeID);

		// Insert into the correct slot.
		routingtable_entry_t *ptr = routingtable->hash[slot];
		if (ptr == NULL) {
			routingtable->hash[slot] = entry;
		}
		else {
			while (ptr->next != NULL) {
				ptr = ptr->next;
			}
			ptr->next = entry;
		}
	}

	// Clean up.
	free(neighborArray);

	return routingtable;
}

//This funtion destroys a routing table.
//All dynamically allocated data structures for this routing table are freed.
void routingtable_destroy(routingtable_t* routingtable)
{
	// Free each entry in the routing table.
	for (int i=0; i<MAX_ROUTINGTABLE_SLOTS; i++) {
		routingtable_entry_t *ptr, *ptr2;

		// If there are entries for the ith slot, then free the stored linked list.
		if (routingtable->hash[i] != (routingtable_entry_t *)NULL) {
			ptr = routingtable->hash[i];
			while (ptr != NULL) {
				ptr2 = ptr;
				ptr = ptr->next;
				free(ptr2);
			}
		}
	}

	// Free the whole routing table.
	free(routingtable);

	return;
}

//This function updates the routing table using the given destination node ID and next hop's node ID.
//If the routing entry for the given destination already exists, update the existing routing entry.
//If the routing entry of the given destination is not there, add one with the given next node ID.
//Each slot in routing table contains a linked list of routing entries due to conflicting hash keys (differnt hash keys (destination node ID here) may have same hash values (slot entry number here)).
//To add an routing entry to the hash table:
//First use the hash function makehash() to get the slot number in which this routing entry should be stored.
//Then append the routing entry to the linked list in that slot.
void routingtable_setnextnode(routingtable_t* routingtable, int destNodeID, int nextNodeID)
{
	// Declare variables.
	routingtable_entry_t *ptr, *ptr2;

	// Find the hash of the destNodeID.
	int slot = makehash(destNodeID);

	// If there are no entries in the slot, create one with the passed inputs and add it.
	if (routingtable->hash[slot] == (routingtable_entry_t *)NULL) {

		// Make a routing table entry.
		routingtable_entry_t *entry = (routingtable_entry_t *)calloc(1, sizeof(routingtable_entry_t));
		entry->destNodeID = destNodeID;
		entry->nextNodeID = nextNodeID;
		entry->next = (routingtable_entry_t *)NULL;

		// Insert it into the slot.
		routingtable->hash[slot] = entry;
	}
	else {

		// Find the entry with the given destNodeID in the slot.
		ptr = routingtable->hash[slot];
		while (ptr != NULL) {
			if (ptr->destNodeID == destNodeID) {
				ptr->nextNodeID = nextNodeID;
				return;
			}
			ptr2 = ptr;
			ptr = ptr->next;
		}

		// The entry with the given destNodeID does not exist, so create one.
		routingtable_entry_t *entry = (routingtable_entry_t *)calloc(1, sizeof(routingtable_entry_t));
		entry->destNodeID = destNodeID;
		entry->nextNodeID = nextNodeID;
		entry->next = (routingtable_entry_t *)NULL;

		// Insert it into the slot.
		ptr2->next = entry;
	}
	return;
}

//This function looks up the destNodeID in the routing table.
//Since routing table is a hash table, this opeartion has O(1) time complexity.
//To find a routing entry for a destination node, you should first use the hash function makehash() to get the slot number and then go through the linked list in that slot to search the routing entry.
//If the destNodeID is found, return the nextNodeID for this destination node.
//If the destNodeID is not found, return -1.
int routingtable_getnextnode(routingtable_t* routingtable, int destNodeID)
{
	// Declare variables.
	routingtable_entry_t *ptr;

	// Find the hash of the destNodeID.
	int slot = makehash(destNodeID);

	// Loop through the linked list in the slot for the entry with the given destNodeID.
	ptr = routingtable->hash[slot];
	while (ptr != NULL) {

		// If the entry with the given destNodeID exists, then return nextNodeID.
		if (ptr->destNodeID == destNodeID) {
			return ptr->nextNodeID;
		}
	}

	// Entry with the given destNodeID does not exist, so return -1;
	return -1;
}

//This function prints out the contents of the routing table
void routingtable_print(routingtable_t* routingtable)
{
	printf("\nRouting Table\n");

	// Loop through the routing table and print the entries.
	for (int i=0; i<MAX_ROUTINGTABLE_SLOTS; i++) {
		if (routingtable->hash[i] != NULL) {
			printf("#%d (destNodeID, nextNodeID)\n", i);
			printf("	");

			routingtable_entry_t *ptr = routingtable->hash[i];
			while (ptr != NULL) {
				printf("(%d %d) ", ptr->destNodeID, ptr->nextNodeID);
				ptr = ptr->next;
			}
			printf("\n");
		}
	}
	return;
}
