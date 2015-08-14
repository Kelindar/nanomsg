#include <stdio.h>
#include <stdlib.h>

#include "llist.h"
 
int nn_llist_len(struct nn_llist_node *nn_llist_node_head)
{
    struct nn_llist_node *curr = nn_llist_node_head;
    int len = 0;
     
    while(curr)
    {
        ++len;
        curr = curr -> next;
    }
    return len;
}
 
void nn_llist_push(struct nn_llist_node **nn_llist_node_head, nn_llist_data d)
{
    struct nn_llist_node *nn_llist_node_new = malloc(sizeof(struct nn_llist_node));
     
    nn_llist_node_new -> data = d;
    nn_llist_node_new -> next = *nn_llist_node_head;
    *nn_llist_node_head = nn_llist_node_new;
}
 
nn_llist_data nn_llist_pop(struct nn_llist_node **nn_llist_node_head)
{
    struct nn_llist_node *nn_llist_node_togo = *nn_llist_node_head;
    nn_llist_data d;
     
    if(nn_llist_node_head)
    {
        d = nn_llist_node_togo -> data;
        *nn_llist_node_head = nn_llist_node_togo -> next;
        free(nn_llist_node_togo);
    }
    return d;
}
 
void nn_llist_print(struct nn_llist_node **nn_llist_node_head)
{
    struct nn_llist_node *nn_llist_node_curr = *nn_llist_node_head;
     
    if(!nn_llist_node_curr)
        puts("the nn_llist is empty");
    else
    {
        while(nn_llist_node_curr)
        {
            printf("%d ", nn_llist_node_curr -> data); //set for integers, modifiable
            nn_llist_node_curr = nn_llist_node_curr -> next;
        }
        putchar('\n');
    }
}
 
void nn_llist_clear(struct nn_llist_node **nn_llist_node_head)
{
    while(*nn_llist_node_head)
        nn_llist_pop(nn_llist_node_head);
}
 
void nn_llist_snoc(struct nn_llist_node **nn_llist_node_head, nn_llist_data d)
{
    struct nn_llist_node *nn_llist_node_curr = *nn_llist_node_head;
     
    if(!nn_llist_node_curr)
        nn_llist_push(nn_llist_node_head, d);
    else
    {
        //find the last nn_llist_node
        while(nn_llist_node_curr -> next)
            nn_llist_node_curr = nn_llist_node_curr -> next;
        //build the nn_llist_node after it
        nn_llist_push(&(nn_llist_node_curr -> next), d);
    }
}
 
int nn_llist_elem(struct nn_llist_node **nn_llist_node_head, nn_llist_data d)
{
    struct nn_llist_node *nn_llist_node_curr = *nn_llist_node_head;
     
    while(nn_llist_node_curr)
    {
        if(nn_llist_node_curr -> data == d) //set for numbers, modifiable
            return 1;
        else
            nn_llist_node_curr = nn_llist_node_curr -> next;
    }
    return 0;
}