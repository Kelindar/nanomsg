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

#include "xpub.h"
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

#include <stddef.h>

struct nn_xpub_data {
	struct nn_fq_data in_item;
    struct nn_dist_data out_item;
	struct nn_trie trie;
};

struct nn_xpub {
    struct nn_sockbase sockbase;
    struct nn_fq in_pipes;
    struct nn_dist out_pipes;
};

/*  Private functions. */
static void nn_xpub_init (struct nn_xpub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_xpub_term (struct nn_xpub *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xpub_destroy (struct nn_sockbase *self);
static int nn_xpub_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpub_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpub_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpub_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xpub_events (struct nn_sockbase *self);
static int nn_xpub_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xpub_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xpub_setopt (struct nn_sockbase *self, int level, int option, const void *optval, size_t optvallen);
static int nn_xpub_getopt (struct nn_sockbase *self, int level, int option, void *optval, size_t *optvallen);
static int nn_xpub_subscribe(struct nn_sockbase *self, struct nn_trie *trie, const void *subval, size_t subvallen);
static int nn_xpub_unsubscribe(struct nn_sockbase *self, struct nn_trie *trie, const void *subval, size_t subvallen);
static int nn_xpub_handle_event(struct nn_sockbase *self, struct nn_msg *msg, struct nn_pipe *pipe);
static const struct nn_sockbase_vfptr nn_xpub_sockbase_vfptr = {
    NULL,
    nn_xpub_destroy,
    nn_xpub_add,
    nn_xpub_rm,
    nn_xpub_in,
    nn_xpub_out,
    nn_xpub_events,
    nn_xpub_send,
    nn_xpub_recv,
    nn_xpub_setopt,
    nn_xpub_getopt
};

static void nn_xpub_init (struct nn_xpub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_dist_init (&self->out_pipes);
	nn_fq_init(&self->in_pipes);
}

static void nn_xpub_term (struct nn_xpub *self)
{
	nn_fq_term(&self->in_pipes);
    nn_dist_term (&self->out_pipes);
    nn_sockbase_term (&self->sockbase);
}

void nn_xpub_destroy (struct nn_sockbase *self)
{
    struct nn_xpub *xpub;

    xpub = nn_cont (self, struct nn_xpub, sockbase);

    nn_xpub_term (xpub);
    nn_free (xpub);
}

static int nn_xpub_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xpub *xpub;
    struct nn_xpub_data *data;
	int rcvprio;
	size_t sz;

    xpub = nn_cont (self, struct nn_xpub, sockbase);

	sz = sizeof(rcvprio);
	nn_pipe_getopt(pipe, NN_SOL_SOCKET, NN_RCVPRIO, &rcvprio, &sz);
	nn_assert(sz == sizeof(rcvprio));
	nn_assert(rcvprio >= 1 && rcvprio <= 16);

    data = nn_alloc (sizeof (struct nn_xpub_data), "pipe data (pub)");
    alloc_assert (data);
	
	nn_trie_init(&data->trie);
    nn_dist_add (&xpub->out_pipes, &data->out_item, pipe);
	nn_fq_add(&xpub->in_pipes, &data->in_item, pipe, rcvprio);
    nn_pipe_setdata (pipe, data);

    return 0;
}

static void nn_xpub_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xpub *xpub;
    struct nn_xpub_data *data;

    xpub = nn_cont (self, struct nn_xpub, sockbase);
    data = nn_pipe_getdata (pipe);

	nn_trie_term(&data->trie);
    nn_dist_rm (&xpub->out_pipes, &data->out_item);
	nn_fq_rm(&xpub->in_pipes, &data->in_item);
    nn_free (data);
}

static void nn_xpub_in (NN_UNUSED struct nn_sockbase *self,
                       NN_UNUSED struct nn_pipe *pipe)
{
	struct nn_xpub *xpub;
	struct nn_xpub_data *data;

	xpub = nn_cont(self, struct nn_xpub, sockbase);
	data = nn_pipe_getdata(pipe);
	nn_fq_in(&xpub->in_pipes, &data->in_item);
}

static void nn_xpub_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xpub *xpub;
    struct nn_xpub_data *data;

    xpub = nn_cont (self, struct nn_xpub, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_dist_out (&xpub->out_pipes, &data->out_item);
}

static int nn_xpub_events (NN_UNUSED struct nn_sockbase *self)
{
	struct nn_xpub* sock;
	sock = nn_cont(self, struct nn_xpub, sockbase);
	return (nn_fq_can_recv(&sock->in_pipes) ? NN_SOCKBASE_EVENT_IN : 0) |
		(nn_lb_can_send(&sock->out_pipes) ? NN_SOCKBASE_EVENT_OUT : 0);
}

static int nn_xpub_send (struct nn_sockbase *self, struct nn_msg *msg)
{
	struct nn_xpub_data *pipe_data;
	struct nn_dist *dist;
	char op;
	int rc;
	struct nn_list_item *it;
	struct nn_dist_data *data;
	struct nn_msg copy;
	
	// Get the message header
	op = *((char*)nn_chunkref_data(&msg->body));
	dist = &(nn_cont(self, struct nn_xpub, sockbase)->out_pipes);

	// Messages are prefixed with operation type 'M'
	if (op == 'M') {

		// Match the message and retrieve the list of subscribers to send the message to
		/*subscribers = nn_trie_match(&xpub->trie, (uint8_t*)nn_chunkref_data(&msg->body) + 1, nn_chunkref_size(&msg->body) - 1);
		if (subscribers == NULL) {
			nn_msg_term(msg);
			return 0;
		}

		printf("outpipes: %d\n", xpub->out_pipes.count);

		struct nn_list_item *it;
		struct nn_dist_data *data;
		struct nn_xpub_data *pipe_data;

		it = nn_list_begin(&xpub->out_pipes.pipes);
		while (it != nn_list_end(&xpub->out_pipes.pipes)) {
			data = nn_cont(it, struct nn_dist_data, item);
			pipe_data = (struct nn_xpub_data*)(nn_pipe_getdata(data->pipe));

			//printf(" - %d\n", data->pipe);
			printf(" - %d\n", pipe_data->test);
			it = nn_list_next(&xpub->out_pipes.pipes, it);
		}*/

		//return nn_dist_send(&xpub->out_pipes, msg, NULL);;
		// Send the message to the list of subscribers
		//return nn_vector_send(subscribers, msg, NULL);
		//return 0;

		/*  In the specific case when there are no outbound pipes. There's nowhere
		to send the message to. Deallocate it. */
		if (nn_slow(dist->count) == 0) {
			nn_msg_term(msg);
			return 0;
		}

		/*  Send the message to all the subscribers. */
		nn_msg_bulkcopy_start(msg, dist->count);
		it = nn_list_begin(&dist->pipes);
		while (it != nn_list_end(&dist->pipes)) {
			data = nn_cont(it, struct nn_dist_data, item);
			nn_msg_bulkcopy_cp(&copy, msg);

			// Match
			pipe_data = (struct nn_xpub_data*)(nn_pipe_getdata(data->pipe));
			rc = nn_trie_match(&pipe_data->trie, (uint8_t*)nn_chunkref_data(&msg->body) + 1, nn_chunkref_size(&msg->body) - 1);
			if (rc == 0) {
				nn_msg_term(&copy);
			} else if (rc == 1) {
				rc = nn_pipe_send(data->pipe, &copy);
				errnum_assert(rc >= 0, -rc);
				if (rc & NN_PIPE_RELEASE) {
					--dist->count;
					it = nn_list_erase(&dist->pipes, it);
					continue;
				}
			} else {
				errnum_assert(0, -rc);
			}
			it = nn_list_next(&dist->pipes, it);
		}
		nn_msg_term(msg);
		return 0;
	}
	else {
		// Other event types are broadcasted through the network
		return nn_dist_send(dist, msg, NULL);;
	}

}

static int nn_xpub_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_xpub *xpub;
    struct nn_pipe *pipe;
	uint8_t op;

    xpub = nn_cont (self, struct nn_xpub, sockbase);
    while (1) {

        // Get next message in fair-queued manner. 
        rc = nn_fq_recv (&xpub->in_pipes, msg, &pipe);
        if (nn_slow (rc < 0))
            return rc;

		//printf("Received %d bytes", nn_chunkref_size(&msg->body));

		//  The message should have no header. Drop malformed messages.
		if (nn_chunkref_size(&msg->sphdr) == 0) {

			// Check the type of the message
			op = *((uint8_t*)nn_chunkref_data(&msg->body));
			if (op == 77) { // 'M'
				return 0;
			}
			else {
				// This is a system event, handle differently
				nn_xpub_handle_event(self, msg, pipe);
				nn_msg_term(msg);
				continue;
			}
		}

		// Drop malformed messages
        nn_msg_term (msg);
    }

	return 0;
}

static int nn_xpub_handle_event(struct nn_sockbase *self, struct nn_msg *msg, struct nn_pipe *pipe)
{
	uint8_t op;
	char* topic;
	size_t size;
	struct nn_xpub_data *pipe_data;

	/*  The message should have no header. Drop malformed messages. */
	if (nn_chunkref_size(&msg->sphdr) != 0)		
		nn_msg_term(msg);

	op = *((uint8_t*)nn_chunkref_data(&msg->body));
	topic = (char*)(nn_chunkref_data(&msg->body)) + 1;
	size = strlen(topic);
	pipe_data = (struct nn_xpub_data*)(nn_pipe_getdata(pipe));

	if (op == 83) { // 'S'
		printf("SUBSCRIBE %d on '%s' \n", pipe, topic);
		nn_xpub_subscribe(self, &pipe_data->trie, topic, size);
	}

	if (op == 85) { // 'U'
		printf("UNSUBSCRIBE %d on '%s' \n", pipe, topic);
		nn_xpub_unsubscribe(self, &pipe_data->trie, topic, size);
	}


	/*  Add pipe ID to the message header. */
	nn_chunkref_term(&msg->sphdr);
	nn_chunkref_init(&msg->sphdr, sizeof(uint64_t));
	memset(nn_chunkref_data(&msg->sphdr), 0, sizeof(uint64_t));
	memcpy(nn_chunkref_data(&msg->sphdr), &pipe, sizeof(pipe));

	return 0;
}


static int nn_xpub_subscribe(struct nn_sockbase *self, struct nn_trie *trie, const void *subval, size_t subvallen)
{
	int rc;
	struct nn_xpub *xpub;

	xpub = nn_cont(self, struct nn_xpub, sockbase);
	rc = nn_trie_subscribe(trie, subval, subvallen);
	if (rc >= 0)
		return 0;
	return rc;
}

static int nn_xpub_unsubscribe(struct nn_sockbase *self, struct nn_trie *trie, const void *subval, size_t subvallen)
{
	int rc;
	struct nn_xpub *xpub;

	xpub = nn_cont(self, struct nn_xpub, sockbase);
	rc = nn_trie_unsubscribe(trie, subval, subvallen);
	if (rc >= 0)
		return 0;
	return rc;
}

static int nn_xpub_setopt (NN_UNUSED struct nn_sockbase *self,
    NN_UNUSED int level, NN_UNUSED int option,
    NN_UNUSED const void *optval, NN_UNUSED size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xpub_getopt (NN_UNUSED struct nn_sockbase *self,
    NN_UNUSED int level, NN_UNUSED int option,
    NN_UNUSED void *optval, NN_UNUSED size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xpub_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xpub *self;

    self = nn_alloc (sizeof (struct nn_xpub), "socket (xpub)");
    alloc_assert (self);
    nn_xpub_init (self, &nn_xpub_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xpub_ispeer (int socktype)
{
     return socktype == NN_SUB ? 1 : 0;
}

static struct nn_socktype nn_xpub_socktype_struct = {
    AF_SP_RAW,
    NN_PUB,
    NN_SOCKTYPE_FLAG_NORECV,
    nn_xpub_create,
    nn_xpub_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xpub_socktype = &nn_xpub_socktype_struct;

