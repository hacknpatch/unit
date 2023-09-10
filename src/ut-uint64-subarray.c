#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ut-uint64-subarray.h"
#include "ut.h"

typedef struct {
  UtObject object;
  UtObject *parent;
  size_t parent_length; // Parent length to catch crash if parent changes size.
  size_t start;
  size_t length;
} UtUint64Subarray;

static uint64_t *get_data(UtUint64Subarray *self) {
  assert(ut_list_get_length(self->parent) == self->parent_length);
  return ut_uint64_list_get_writable_data(self->parent) + self->start;
}

static uint64_t ut_uint64_subarray_get_element(UtObject *object, size_t index) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  uint64_t *data = get_data(self);
  return data[index];
}

static const uint64_t *ut_uint64_subarray_get_const_data(UtObject *object) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  return get_data(self);
}

static uint64_t *ut_uint64_subarray_take_data(UtObject *object) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  uint64_t *data = get_data(self);
  uint64_t *copy = malloc(sizeof(uint64_t) * self->length);
  memcpy(copy, data, self->length);
  return copy;
}

static size_t ut_uint64_subarray_get_length(UtObject *object) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  return self->length;
}

static UtObject *ut_uint64_subarray_get_element_object(UtObject *object,
                                                       size_t index) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  uint64_t *data = get_data(self);
  return ut_uint64_new(data[index]);
}

static UtObject *ut_uint64_subarray_get_sublist(UtObject *object, size_t start,
                                                size_t count) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  return ut_uint64_subarray_new(self->parent, self->start + start, count);
}

static UtObject *ut_uint64_subarray_copy(UtObject *object) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  UtObject *copy = ut_uint64_array_new();
  uint64_t *data = get_data(self);
  ut_uint64_list_append_block(copy, data, self->length);
  return copy;
}

static char *ut_uint64_subarray_to_string(UtObject *object) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  uint64_t *data = get_data(self);
  UtObjectRef string = ut_string_new("<uint64>[");
  for (size_t i = 0; i < self->length; i++) {
    if (i != 0) {
      ut_string_append(string, ", ");
    }
    ut_cstring_ref value_string = ut_cstring_new_printf("%lu", data[i]);
    ut_string_append(string, value_string);
  }
  ut_string_append(string, "]");

  return ut_string_take_text(string);
}

static void ut_uint64_subarray_cleanup(UtObject *object) {
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  ut_object_unref(self->parent);
}

static UtUint64ListInterface uint64_list_interface = {
    .get_element = ut_uint64_subarray_get_element,
    .get_data = ut_uint64_subarray_get_const_data,
    .take_data = ut_uint64_subarray_take_data};

static UtListInterface list_interface = {
    .is_mutable = false,
    .get_length = ut_uint64_subarray_get_length,
    .get_element = ut_uint64_subarray_get_element_object,
    .get_sublist = ut_uint64_subarray_get_sublist,
    .copy = ut_uint64_subarray_copy};

static UtObjectInterface object_interface = {
    .type_name = "UtUint64Subarray",
    .to_string = ut_uint64_subarray_to_string,
    .cleanup = ut_uint64_subarray_cleanup,
    .interfaces = {{&ut_uint64_list_id, &uint64_list_interface},
                   {&ut_list_id, &list_interface},
                   {NULL, NULL}}};

UtObject *ut_uint64_subarray_new(UtObject *parent, size_t start,
                                 size_t length) {
  UtObject *object = ut_object_new(sizeof(UtUint64Subarray), &object_interface);
  UtUint64Subarray *self = (UtUint64Subarray *)object;

  assert(parent != NULL && ut_object_is_uint64_array(parent));
  size_t parent_length = ut_list_get_length(parent);
  assert(start + length <= parent_length);

  self->parent = ut_object_ref(parent);
  self->parent_length = parent_length;
  self->start = start;
  self->length = length;
  return object;
}

uint64_t *ut_uint64_subarray_get_data(UtObject *object) {
  assert(ut_object_is_uint64_subarray(object));
  UtUint64Subarray *self = (UtUint64Subarray *)object;
  return get_data(self);
}

bool ut_object_is_uint64_subarray(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
