#include <stdio.h>
#include <stdlib.h>

#include "llist.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/attr.h"

void nn_vector_init(struct nn_vector *vector) {
	// initialize size and capacity
	vector->size = 0;
	vector->count = 0;
	vector->capacity = VECTOR_INITIAL_CAPACITY;

	// allocate memory for vector->data
	vector->data = malloc(sizeof(int) * vector->capacity);
}

void nn_vector_add(struct nn_vector *vector, struct nn_pipe* value) {
	// Try to find the pipe first or an empty slot
	int slot = -1;
	for (int i = 0; i < vector->size; ++i) {
		if (vector->data[i] == value)
			break;
		if (slot == -1 && vector->data[i] == NULL)
			slot = i;
	}

	if (slot >= 0) {
		// Put to the empty slot
		vector->data[slot] = value;
		++vector->count;
	}
	else {
		// make sure there's room to expand into
		nn_vector_double_capacity_if_full(vector);

		// append the value and increment vector->size
		vector->data[vector->size++] = value;
		++vector->count;
	}
}

void nn_vector_remove(struct nn_vector *vector, struct nn_pipe* value) {
	for (int i = 0; i < vector->size; ++i) {
		if (vector->data[i] == value) {
			vector->data[i] = NULL;
			--vector->count;
			break;
		}
	}
}

void nn_vector_remove_at(struct nn_vector *vector, int index){
	vector->data[index] = NULL;
}

struct nn_pipe* nn_vector_get(struct nn_vector *vector, int index) {
	if (index >= vector->size || index < 0) {
		printf("Index %d out of bounds for vector of size %d\n", index, vector->size);
		exit(1);
	}
	return vector->data[index];
}

void nn_vector_double_capacity_if_full(struct nn_vector *self) {
	if (self->size >= self->capacity) {
		// double vector->capacity and resize the allocated memory accordingly
		self->capacity *= 2;
		self->data = realloc(self->data, sizeof(int) * self->capacity);
	}
}

void nn_vector_free(struct nn_vector *self) {
	free(self->data);
}

int nn_vector_send(struct nn_vector *self, struct nn_msg *msg, struct nn_pipe *exclude)
{
	int rc;
	int subcount, i;
	struct nn_pipe *pipe;
	struct nn_msg copy;

	/*  TODO: We can optimise for the case when there's only one outbound
		pipe here. No message copying is needed in such case. */

	/*  In the specific case when there are no outbound pipes. There's nowhere
		to send the message to. Deallocate it. */
	if (nn_slow(self->count) == 0) {
		nn_msg_term(msg);
		return 0;
	}

	/*  Send the message to all the subscribers. */
	nn_msg_bulkcopy_start(msg, self->count);
	subcount = self->size;
	for (i = 0; i < subcount; ++i) {
		pipe = self->data[i];
		if (pipe == NULL)
			continue;

		nn_msg_bulkcopy_cp(&copy, msg);
		if (nn_fast(pipe == exclude)) {
			nn_msg_term(&copy);
		}
		else {
			rc = nn_pipe_send(pipe, &copy);
			errnum_assert(rc >= 0, -rc);
			if (rc & NN_PIPE_RELEASE) {
				--self->count;
				nn_vector_remove_at(self, i);
				//it = nn_list_erase (&self->pipes, it);
				continue;
			}
		}
	}

	nn_msg_term(msg);
	return 0;
}
