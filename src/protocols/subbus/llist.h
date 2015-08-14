#ifndef NN_LLIST_INCLUDED
#define NN_LLIST_INCLUDED

#include <stddef.h>

// data type - set for integers, modifiable
typedef int nn_llist_data; 
struct nn_llist_node
{
    nn_llist_data data;
	struct nn_llist_node *next;
};
 
 
/* Gets the length of the linked-list */
int nn_llist_len(struct nn_llist_node *nn_llist_node_head);

/* Pushes a new node to the linked-list */ 
void nn_llist_push(struct nn_llist_node **nn_llist_node_head, nn_llist_data d); 

/* Removes the head from the linked-list & returns its value */
nn_llist_data nn_llist_pop(struct nn_llist_node **nn_llist_node_head);

/* prints all the linked-list data */
void nn_llist_print(struct nn_llist_node **nn_llist_node_head);

/* clears the linked-list of all elements */
void nn_llist_clear(struct nn_llist_node **nn_llist_node_head);

/* appends a node to the linked-list */
void nn_llist_snoc(struct nn_llist_node **nn_llist_node_head, nn_llist_data d);

/* checks for an element */
int nn_llist_elem(struct nn_llist_node **nn_llist_node_head, nn_llist_data d);
 

#endif