/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_SUBSUB_TRIE_INCLUDED
#define NN_SUBSUB_TRIE_INCLUDED

#include "../../utils/int.h"
#include "../utils/dist.h"
#include "llist.h"

#include <stddef.h>

/*  This class implements highly memory-efficient patricia trie. */

/* Maximum length of the prefix. */
#define NN_TRIE_PREFIX_MAX 8

/* Maximum number of children in the sparse mode. */
#define NN_TRIE_SPARSE_MAX 8

/* 'type' is set to this value when in the dense mode. */
#define NN_TRIE_DENSE_TYPE (NN_TRIE_SPARSE_MAX + 1)

/*  This structure represents a node in patricia trie. It's a header to be
    followed by the array of pointers to child nodes. Each node represents
    the string composed of all the prefixes on the way from the trie root,
    including the prefix in that node. */
struct nn_xtrie_node
{
    /*  Number of subscriptions to the given string. */
    uint32_t refcount;

    /*  Number of elements is a sparse array, or NN_TRIE_DENSE_TYPE in case
        the array of children is dense. */
    uint8_t type;

    /*  The node adds more characters to the string, compared to the parent
        node. If there is only a single character added, it's represented
        directly in the child array. If there's more than one character added,
        all but the last one are stored as a 'prefix'. */
    uint8_t prefix_len;
    uint8_t prefix [NN_TRIE_PREFIX_MAX];
    
    /*  The list of the subscriber pipes for the current leaf. We are using the
        nn_dist structure so we can distribute fairly. */
    struct nn_vector subscribers;

    /*  The array of characters pointing to individual children of the node.
        Actual pointers to child nodes are stored in the memory following
        nn_xtrie_node structure. */
    union {

        /*  Sparse array means that individual children are identified by
            characters stored in 'children' array. The number of characters
            in the array is specified in the 'type' field. */
        struct {
            uint8_t children [NN_TRIE_SPARSE_MAX];
        } sparse;

        /*  Dense array means that the array of node pointers following the
            structure corresponds to a continuous list of characters starting
            by 'min' character and ending by 'max' character. The characters
            in the range that have no corresponding child node are represented
            by NULL pointers. 'nbr' is the count of child nodes. */
        struct {
            uint8_t min;
            uint8_t max;
            uint16_t nbr;
            /*  There are 4 bytes of padding here. */
        } dense;
    } u;
};
/*  The structure is followed by the array of pointers to children. */

struct nn_xtrie {

    /*  The root node of the xtrie (representing the empty subscription). */
    struct nn_xtrie_node *root;

};

/*  Initialise an empty xtrie. */
void nn_xtrie_init (struct nn_xtrie *self);

/*  Release all the resources associated with the xtrie. */
void nn_xtrie_term (struct nn_xtrie *self);

/*  Add the string to the xtrie. If the string is not yet there, 1 is returned.
    If it already exists in the xtrie, its reference count is incremented and
    0 is returned. */
int nn_xtrie_subscribe (struct nn_xtrie *self, struct nn_pipe *pipe, const uint8_t *data, size_t size);

/*  Remove the string from the xtrie. If the string was actually removed,
    1 is returned. If reference count was decremented without falling to zero,
    0 is returned. */
int nn_xtrie_unsubscribe (struct nn_xtrie *self, struct nn_pipe *pipe, const uint8_t *data,
    size_t size);

/*  Checks the supplied string. If it matches it returns the subscribers list,
    if it does not it returns NULL. */
struct nn_vector* nn_xtrie_match (struct nn_xtrie *self, const uint8_t *data, size_t size);

/*  Debugging interface. */
void nn_xtrie_dump (struct nn_xtrie *self);

#endif

