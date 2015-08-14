/*
    Copyright (c) 2013-2014 Martin Sustrik  All rights reserved.

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

#include <stdlib.h>
#include "xsubbus.h"
#include "trie.h"

#include "../../nn.h"
#include "../../subbus.h"
#include "../utils/fq.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/int.h"
#include "../../utils/attr.h"

#include <stddef.h>
#include <string.h>

/*  To make the algorithm super efficient we directly cast pipe pointers to
    pipe IDs (rather than maintaining a hash table). For this to work, it is
    neccessary for the pointer to fit in 64-bit ID. */
CT_ASSERT (sizeof (uint64_t) >= sizeof (struct nn_pipe*));

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xsubbus_destroy (struct nn_sockbase *self);
static const struct nn_sockbase_vfptr nn_xsubbus_sockbase_vfptr = {
    NULL,
    nn_xsubbus_destroy,
    nn_xsubbus_add,
    nn_xsubbus_rm,
    nn_xsubbus_in,
    nn_xsubbus_out,
    nn_xsubbus_events,
    nn_xsubbus_send,
    nn_xsubbus_recv,
    nn_xsubbus_setopt,
    nn_xsubbus_getopt
};

void nn_xsubbus_init (struct nn_xsubbus *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_dist_init (&self->outpipes);
    nn_fq_init (&self->inpipes);
	nn_xtrie_init(&self->trie);
}

void nn_xsubbus_term (struct nn_xsubbus *self)
{
	nn_xtrie_term(&self->trie);
    nn_fq_term (&self->inpipes);
    nn_dist_term (&self->outpipes);
    nn_sockbase_term (&self->sockbase);
}

static void nn_xsubbus_destroy (struct nn_sockbase *self)
{
    struct nn_xsubbus *xsubbus;

    xsubbus = nn_cont (self, struct nn_xsubbus, sockbase);

    nn_xsubbus_term (xsubbus);
    nn_free (xsubbus);
}

int nn_xsubbus_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsubbus *xsubbus;
    struct nn_xsubbus_data *data;
    int rcvprio;
    size_t sz;

    xsubbus = nn_cont (self, struct nn_xsubbus, sockbase);

    sz = sizeof (rcvprio);
    nn_pipe_getopt (pipe, NN_SOL_SOCKET, NN_RCVPRIO, &rcvprio, &sz);
    nn_assert (sz == sizeof (rcvprio));
    nn_assert (rcvprio >= 1 && rcvprio <= 16);

    data = nn_alloc (sizeof (struct nn_xsubbus_data), "pipe data (xsubbus)");
    alloc_assert (data);
    nn_fq_add (&xsubbus->inpipes, &data->initem, pipe, rcvprio);
    nn_dist_add (&xsubbus->outpipes, &data->outitem, pipe);
    nn_pipe_setdata (pipe, data);

    return 0;
}

void nn_xsubbus_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsubbus *xsubbus;
    struct nn_xsubbus_data *data;

    xsubbus = nn_cont (self, struct nn_xsubbus, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_fq_rm (&xsubbus->inpipes, &data->initem);
    nn_dist_rm (&xsubbus->outpipes, &data->outitem);

    nn_free (data);
}

void nn_xsubbus_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsubbus *xsubbus;
    struct nn_xsubbus_data *data;

    xsubbus = nn_cont (self, struct nn_xsubbus, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_fq_in (&xsubbus->inpipes, &data->initem);
}

void nn_xsubbus_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsubbus *xsubbus;
    struct nn_xsubbus_data *data;

    xsubbus = nn_cont (self, struct nn_xsubbus, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_dist_out (&xsubbus->outpipes, &data->outitem);
}

int nn_xsubbus_events (struct nn_sockbase *self)
{
    return (nn_fq_can_recv (&nn_cont (self, struct nn_xsubbus,
        sockbase)->inpipes) ? NN_SOCKBASE_EVENT_IN : 0) | NN_SOCKBASE_EVENT_OUT;
}


int nn_xsubbus_broadcast(struct nn_sockbase *self, struct nn_msg *msg)
{
	size_t hdrsz;
	struct nn_pipe *exclude;

	hdrsz = nn_chunkref_size(&msg->sphdr);
	if (hdrsz == 0)
		exclude = NULL;
	else if (hdrsz == sizeof(uint64_t)) {
		memcpy(&exclude, nn_chunkref_data(&msg->sphdr), sizeof(exclude));
		nn_chunkref_term(&msg->sphdr);
		nn_chunkref_init(&msg->sphdr, 0);
	}
	else
		return -EINVAL;

	return nn_dist_send(&nn_cont(self, struct nn_xsubbus, sockbase)->outpipes, msg, exclude);
}

int nn_xsubbus_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    struct nn_dist* subscribers;
    struct nn_xsubbus *xsubbus;
	char op = *((char*)nn_chunkref_data(&msg->body));

	/* Messages are prefixed with operation type 'M' */
	if (op == 'M') {
		/* Match the message and retrieve the list of subscribers to send the message to */
		xsubbus = nn_cont(self, struct nn_xsubbus, sockbase);
		subscribers = nn_xtrie_match(&xsubbus->trie, nn_chunkref_data(&msg->body), nn_chunkref_size(&msg->body));
		if (subscribers == NULL) {
			nn_msg_term(msg);
			return 0;
		}

		/* Send the message to the list of subscribers */
		return nn_dist_send(subscribers, msg, NULL);
	}
	else {
		/* Other event types are broadcasted through the network */
		return nn_xsubbus_broadcast(self, msg);
	}
}


int nn_xsubbus_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_xsubbus *xsubbus;
    struct nn_pipe *pipe;
	uint8_t op;

    xsubbus = nn_cont (self, struct nn_xsubbus, sockbase);
    while (1) {

        /*  Get next message in fair-queued manner. */
        rc = nn_fq_recv (&xsubbus->inpipes, msg, &pipe);
        if (nn_slow (rc < 0))
            return rc;

		/*  The message should have no header. Drop malformed messages. */
		if (nn_chunkref_size(&msg->sphdr) == 0)
            break;
        nn_msg_term (msg);
    }

	/* Check if its' a message */
	op = *((uint8_t*)nn_chunkref_data(&msg->body));
	if (op == 77) { // 'M'
		printf("Message of %d bytes\n", rc);
		return 0;
	}
	else {
		/* This is a system event, handle differently*/
		return nn_xsubbus_handle_event(self, msg, pipe);
	}
}

int nn_xsubbus_handle_event(struct nn_sockbase *self, struct nn_msg *msg, struct nn_pipe *pipe)
{
	/*  The message should have no header. Drop malformed messages. */
	if (nn_chunkref_size(&msg->sphdr) != 0)		
		nn_msg_term(msg);

	uint8_t  op = *((uint8_t*)nn_chunkref_data(&msg->body));
	char* topic = (char*)(nn_chunkref_data(&msg->body)) + 1;
	size_t size = nn_chunkref_size(&msg->body) - 1;


	if (op == 83) { // 'S'
		printf("SUBSCRIBE %d on '%s' \n", pipe, topic);
		nn_xsubbus_subscribe(self, pipe, topic, size);
	}

	if (op == 85) { // 'U'
		printf("UNSUBSCRIBE %d on '%s' \n", pipe, topic);
		nn_xsubbus_unsubscribe(self, pipe, topic, size);
	}


	/*  Add pipe ID to the message header. */
	nn_chunkref_term(&msg->sphdr);
	nn_chunkref_init(&msg->sphdr, sizeof(uint64_t));
	memset(nn_chunkref_data(&msg->sphdr), 0, sizeof(uint64_t));
	memcpy(nn_chunkref_data(&msg->sphdr), &pipe, sizeof(pipe));

	return 0;
}


int nn_xsubbus_setopt (NN_UNUSED struct nn_sockbase *self, NN_UNUSED int level,
    NN_UNUSED int option,
    NN_UNUSED const void *optval, NN_UNUSED size_t optvallen)
{
	return -ENOPROTOOPT;
}

int nn_xsubbus_getopt (NN_UNUSED struct nn_sockbase *self, NN_UNUSED int level,
    NN_UNUSED int option,
    NN_UNUSED void *optval, NN_UNUSED size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xsubbus_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xsubbus *self;

    self = nn_alloc (sizeof (struct nn_xsubbus), "socket (subbus)");
    alloc_assert (self);
    nn_xsubbus_init (self, &nn_xsubbus_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xsubbus_ispeer (int socktype)
{
    return socktype == NN_SUBBUS ? 1 : 0;
}

int nn_xsubbus_subscribe(struct nn_sockbase *self, struct nn_pipe *pipe, const void *subval, size_t subvallen)
{
	int rc;
	struct nn_xsubbus *xsubbus;

	xsubbus = nn_cont(self, struct nn_xsubbus, sockbase);
	rc = nn_xtrie_subscribe(&xsubbus->trie, pipe, subval, subvallen);
	if (rc >= 0)
		return 0;
	return rc;
}

int nn_xsubbus_unsubscribe(struct nn_sockbase *self, struct nn_pipe *pipe, const void *subval, size_t subvallen)
{
	int rc;
	struct nn_xsubbus *xsubbus;

	xsubbus = nn_cont(self, struct nn_xsubbus, sockbase);
	rc = nn_xtrie_unsubscribe(&xsubbus->trie, pipe, subval, subvallen);
	if (rc >= 0)
		return 0;
	return rc;
}


static struct nn_socktype nn_xsubbus_socktype_struct = {
    AF_SP_RAW,
    NN_SUBBUS,
    0,
    nn_xsubbus_create,
    nn_xsubbus_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xsubbus_socktype = &nn_xsubbus_socktype_struct;

