#ifndef NN_VECTOR_INCLUDED
#define NN_VECTOR_INCLUDED

#include <stddef.h>

#include "../../protocol.h"
#include "../utils/dist.h"
#define VECTOR_INITIAL_CAPACITY 32

// Define a vector type
typedef struct nn_vector{
  int size;
  int capacity; 
  int count;
  struct nn_dist_data **data;
};

void nn_vector_init(struct nn_vector *vector);

void nn_vector_add(struct nn_vector *vector, struct nn_dist_data* value);

struct nn_dist_data* nn_vector_get(struct nn_vector *vector, int index);

void nn_vector_double_capacity_if_full(struct nn_vector *vector);

void nn_vector_remove(struct nn_vector *vector, struct nn_dist_data* value);

void nn_vector_remove_at(struct nn_vector *vector, int index);

void nn_vector_free(struct nn_vector *vector);

int nn_vector_send (struct nn_vector *self, struct nn_msg *msg, struct nn_dist_data *exclude);

#endif