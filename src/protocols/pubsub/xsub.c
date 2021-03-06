/*
    Copyright (c) 2012-2014 Martin Sustrik  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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

#include "xsub.h"
#include "trie.h"

#include "../../nn.h"
#include "../../pubsub.h"

#include "../utils/fq.h"
#include "../utils/dist.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/attr.h"

struct nn_xsub_data {
    struct nn_fq_data in_item;
    struct nn_dist_data out_item;
};

struct nn_xsub {
    struct nn_sockbase sockbase;
    struct nn_fq in_pipes;
    struct nn_dist out_pipes;
    struct nn_trie trie;
};

/*  Private functions. */
static void nn_xsub_init (struct nn_xsub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_xsub_term (struct nn_xsub *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xsub_destroy (struct nn_sockbase *self);
static int nn_xsub_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsub_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsub_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsub_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xsub_events (struct nn_sockbase *self);
static int nn_xsub_send(struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xsub_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xsub_setopt (struct nn_sockbase *self, int level, int option, const void *optval, size_t optvallen);
static int nn_xsub_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_xsub_sockbase_vfptr = {
    NULL,
    nn_xsub_destroy,
    nn_xsub_add,
    nn_xsub_rm,
    nn_xsub_in,
    nn_xsub_out,
    nn_xsub_events,
	nn_xsub_send,
    nn_xsub_recv,
    nn_xsub_setopt,
    nn_xsub_getopt
};

static void nn_xsub_init (struct nn_xsub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_fq_init (&self->in_pipes);
    nn_dist_init (&self->out_pipes);
    nn_trie_init (&self->trie);
}

static void nn_xsub_term (struct nn_xsub *self)
{
    nn_trie_term (&self->trie);
    nn_dist_term (&self->out_pipes);
    nn_fq_term (&self->in_pipes);
    nn_sockbase_term (&self->sockbase);
}

void nn_xsub_destroy (struct nn_sockbase *self)
{
    struct nn_xsub *xsub;

    xsub = nn_cont (self, struct nn_xsub, sockbase);

    nn_xsub_term (xsub);
    nn_free (xsub);
}

static int nn_xsub_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsub *xsub;
    struct nn_xsub_data *data;
    int rcvprio;
    size_t sz;

    xsub = nn_cont (self, struct nn_xsub, sockbase);

    sz = sizeof (rcvprio);
    nn_pipe_getopt (pipe, NN_SOL_SOCKET, NN_RCVPRIO, &rcvprio, &sz);
    nn_assert (sz == sizeof (rcvprio));
    nn_assert (rcvprio >= 1 && rcvprio <= 16);

    data = nn_alloc (sizeof (struct nn_xsub_data), "pipe data (sub)");
    alloc_assert (data);
    nn_pipe_setdata (pipe, data);
    nn_fq_add (&xsub->in_pipes, &data->in_item, pipe, rcvprio);
    nn_dist_add (&xsub->out_pipes, &data->out_item, pipe);

    printf("[XSUB] Connected: %d\n", pipe);

    return 0;
}

static void nn_xsub_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsub *xsub;
    struct nn_xsub_data *data;
    
    printf("[XSUB] Disconnected: %d\n", pipe);

    xsub = nn_cont (self, struct nn_xsub, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_fq_rm (&xsub->in_pipes, &data->in_item);
    nn_dist_rm (&xsub->out_pipes, &data->out_item);
    nn_free (data);
    
}

static void nn_xsub_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsub *xsub;
    struct nn_xsub_data *data;

    xsub = nn_cont (self, struct nn_xsub, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_fq_in (&xsub->in_pipes, &data->in_item);
}

static void nn_xsub_out (NN_UNUSED struct nn_sockbase *self,
    NN_UNUSED struct nn_pipe *pipe)
{
    struct nn_xsub *xsub;
    struct nn_xsub_data *data;

    xsub = nn_cont (self, struct nn_xsub, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_dist_out (&xsub->out_pipes, &data->out_item);
}

static int nn_xsub_events (struct nn_sockbase *self)
{
	struct nn_xsub* sock;
	sock = nn_cont(self, struct nn_xsub, sockbase);
	return (nn_fq_can_recv(&sock->in_pipes) ? NN_SOCKBASE_EVENT_IN : 0) |
		(nn_lb_can_send(&sock->out_pipes) ? NN_SOCKBASE_EVENT_OUT : 0);
}

static int nn_xsub_send(struct nn_sockbase *self, struct nn_msg *msg)
{
	char msgtype;
	void *msgval;
	size_t msglen;
	struct nn_xsub *xsub;

	// Set the context
	xsub = nn_cont(self, struct nn_xsub, sockbase);
	msgval = nn_chunkref_data(&msg->body);
	msglen = nn_chunkref_size(&msg->body);
	msgtype = *((char*)msgval);

	if (msgtype == 'S') {
		printf("[XSUB] Subscribe: %s\n", ((char*)msgval + 1));
		nn_trie_subscribe(&xsub->trie, ((char*)msgval + 1), msglen - 1);
	}

	if (msgtype == 'U') {
		printf("[XSUB] Unsubscribe: %s\n", ((char*)msgval + 1));
		nn_trie_unsubscribe(&xsub->trie, ((char*)msgval + 1), msglen - 1);
	}

	return nn_dist_send(&nn_cont(self, struct nn_xsub, sockbase)->out_pipes, msg, NULL);
}

static int nn_xsub_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_xsub *xsub;

    /*  Loop until there are no more messages to receive. */
    xsub = nn_cont (self, struct nn_xsub, sockbase);
    while (1) {
        rc = nn_fq_recv (&xsub->in_pipes, msg, NULL);
        if (nn_slow (rc == -EAGAIN))
            return -EAGAIN;
        errnum_assert (rc >= 0, -rc);
        return 0;
    }
}

static int nn_xsub_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return 0;
}

static int nn_xsub_getopt (NN_UNUSED struct nn_sockbase *self,
    NN_UNUSED int level, NN_UNUSED int option,
    NN_UNUSED void *optval, NN_UNUSED size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xsub_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xsub *self;

    self = nn_alloc (sizeof (struct nn_xsub), "socket (xsub)");
    alloc_assert (self);
    nn_xsub_init (self, &nn_xsub_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xsub_ispeer (int socktype)
{
    return socktype == NN_PUB ? 1 : 0;
}

static struct nn_socktype nn_xsub_socktype_struct = {
    AF_SP_RAW,
    NN_SUB,
    NN_SOCKTYPE_FLAG_NOSEND,
    nn_xsub_create,
    nn_xsub_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xsub_socktype = &nn_xsub_socktype_struct;

