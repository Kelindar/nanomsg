/*
    Copyright (c) 2013 Martin Sustrik  All rights reserved.

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

#include "subbus.h"
#include "xsubbus.h"

#include "../../nn.h"
#include "../../subbus.h"

#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/err.h"
#include "../../utils/list.h"

struct nn_subbus {
    struct nn_xsubbus xsubbus;
};

/*  Private functions. */
static void nn_subbus_init (struct nn_subbus *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_subbus_term (struct nn_subbus *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_subbus_destroy (struct nn_sockbase *self);
static int nn_subbus_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_subbus_recv (struct nn_sockbase *self, struct nn_msg *msg);
static const struct nn_sockbase_vfptr nn_subbus_sockbase_vfptr = {
    NULL,
    nn_subbus_destroy,
    nn_xsubbus_add,
    nn_xsubbus_rm,
    nn_xsubbus_in,
    nn_xsubbus_out,
    nn_xsubbus_events,
    nn_subbus_send,
    nn_subbus_recv,
    nn_xsubbus_setopt,
    nn_xsubbus_getopt
};

static void nn_subbus_init (struct nn_subbus *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_xsubbus_init (&self->xsubbus, vfptr, hint);
}

static void nn_subbus_term (struct nn_subbus *self)
{
    nn_xsubbus_term (&self->xsubbus);
}

static void nn_subbus_destroy (struct nn_sockbase *self)
{
    struct nn_subbus *subbus;

    subbus = nn_cont (self, struct nn_subbus, xsubbus.sockbase);

    nn_subbus_term (subbus);
    nn_free (subbus);
}

static int nn_subbus_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_subbus *subbus;

    subbus = nn_cont (self, struct nn_subbus, xsubbus.sockbase);

    /*  Check for malformed messages. */
    if (nn_chunkref_size (&msg->sphdr))
        return -EINVAL;

    /*  Send the message. */
    rc = nn_xsubbus_send (&subbus->xsubbus.sockbase, msg);
    errnum_assert (rc == 0, -rc);

    return 0;
}

static int nn_subbus_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_subbus *subbus;

    subbus = nn_cont (self, struct nn_subbus, xsubbus.sockbase);

    /*  Get next message. */
    rc = nn_xsubbus_recv (&subbus->xsubbus.sockbase, msg);
    if (nn_slow (rc == -EAGAIN))
        return -EAGAIN;
    errnum_assert (rc == 0, -rc);
    nn_assert (nn_chunkref_size (&msg->sphdr) == sizeof (uint64_t));

    /*  Discard the header. */
    nn_chunkref_term (&msg->sphdr);
    nn_chunkref_init (&msg->sphdr, 0);
    
    return 0;
}

static int nn_subbus_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_subbus *self;

    self = nn_alloc (sizeof (struct nn_subbus), "socket (subbus)");
    alloc_assert (self);
    nn_subbus_init (self, &nn_subbus_sockbase_vfptr, hint);
    *sockbase = &self->xsubbus.sockbase;

    return 0;
}

static struct nn_socktype nn_subbus_socktype_struct = {
    AF_SP,
    NN_SUBBUS,
    0,
    nn_subbus_create,
    nn_xsubbus_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_subbus_socktype = &nn_subbus_socktype_struct;

