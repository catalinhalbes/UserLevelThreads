#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdio.h>
#include <stdlib.h>

typedef int (*filter_func)(void*);

typedef struct node_t {
    void*           data;
    struct node_t*    next;
    struct node_t*    prev;
} node_t;

typedef struct {
    node_t*     head;
    node_t*     tail;
    size_t      size;
} linked_list_t;

// assuming that *list points to a valid memory location
// especially assuming that *list is not NULL
// also assuming that the condition function is not NULL

void init_linked_list(linked_list_t* list);
// *data should be managed by the user
void insert_first(linked_list_t* list, void* data);
// *data should be managed by the user
void insert_last(linked_list_t* list, void* data);
// node_t* find_node(linked_list_t* list, filter_func condition);
// make sure that the node is part of this list otherwise when the deleted node should be the head or the tail this function will replace the pointers with the wrong ones
void delete_node(linked_list_t* list, node_t* node);
void delete_first(linked_list_t* list);
// void delete_last(linked_list_t* list);
// int  delete_by_condition(linked_list_t* list, filter_func condition);
void rotate_front_to_back(linked_list_t* list);
// void rotate_back_to_front(linked_list_t* list);
void destroy_list(linked_list_t* list);

#endif // LINKED_LIST_H
