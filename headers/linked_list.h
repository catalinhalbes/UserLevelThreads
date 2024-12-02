#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdio.h>
#include <stdlib.h>

////////////////////// GENERIC LIST //////////////////////

typedef int (*filter_func)(void*);

typedef struct generic_node_t {
    void*           data;
    struct generic_node_t*    next;
    struct generic_node_t*    prev;
} generic_node_t;

typedef struct {
    generic_node_t*     head;
    generic_node_t*     tail;
    size_t              size;
} generic_linked_list_t;

// assuming that *list points to a valid memory location
// especially assuming that *list is not NULL
// also assuming that the condition function is not NULL

void init_linked_list(generic_linked_list_t* list);
void insert_first(generic_linked_list_t* list, void* data);
void insert_last(generic_linked_list_t* list, void* data);
generic_node_t* find_node(generic_linked_list_t* list, filter_func condition);
// make sure that the node is part of this list otherwise when the deleted node should be the head or the tail this function will replace the pointers with the wrong ones
void delete_node(generic_linked_list_t* list, generic_node_t* node);
void delete_first(generic_linked_list_t* list);
void delete_last(generic_linked_list_t* list);
int  delete_by_condition(generic_linked_list_t* list, filter_func condition);
void rotate_front_to_back(generic_linked_list_t* list);
void rotate_back_to_front(generic_linked_list_t* list);
void destroy_list(generic_linked_list_t* list);

////////////////////// USER LEVEL THREAD LIST //////////////////////

typedef struct ult_t ult_t;

typedef struct ult_node_t {
    ult_t*                ult;
    struct ult_node_t*    next;
    struct ult_node_t*    prev;
} ult_node_t;

typedef struct {
    ult_node_t*     head;
    ult_node_t*     tail;
    size_t          size;
} ult_linked_list_t;

void init_ult_linked_list(ult_linked_list_t* list);
void insert_ult_first(generic_linked_list_t* list, ult_t* ult);
void insert_ult_last(ult_linked_list_t* list, ult_t* ult);
void delete_ult_node(ult_linked_list_t* list, ult_node_t* node);
void delete_ult_first(ult_linked_list_t* list);
void delete_ult_last(generic_linked_list_t* list);
void rotate_ult_front_to_back(ult_linked_list_t* list);
void destroy_ult_list(ult_linked_list_t* list);

////////////////////// MUTEX LIST //////////////////////

typedef struct ult_mutex_t ult_mutex_t;

typedef struct mutex_node_t {
    ult_mutex_t*            mutex;
    struct mutex_node_t*    next;
    struct mutex_node_t*    prev;
} mutex_node_t;

typedef struct {
    mutex_node_t*     head;
    mutex_node_t*     tail;
    size_t            size;
} mutex_linked_list_t;

void init_mutex_linked_list(mutex_linked_list_t* list);
void insert_mutex_last(mutex_linked_list_t* list, ult_mutex_t* mutex);
void delete_mutex_node(mutex_linked_list_t* list, mutex_node_t* node);
void destroy_mutex_list(mutex_linked_list_t* list);

#endif // LINKED_LIST_H
