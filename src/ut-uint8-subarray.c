#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ut-list.h"
#include "ut-string.h"
#include "ut-uint8-array.h"
#include "ut-uint8-list.h"
#include "ut-uint8-subarray.h"
#include "ut-uint8.h"

typedef struct {
  UtObject object;
  UtObject *parent;
  size_t parent_length; // Parent length to catch crash if parent changes size.
  size_t start;
  size_t length;
} UtUint8Subarray;

static uint8_t *get_data(UtUint8Subarray *self) {
  assert(ut_list_get_length(self->parent) == self->parent_length);
  return ut_uint8_array_get_data(self->parent) + self->start;
}

static uint8_t ut_uint8_subarray_get_element(UtObject *object, size_t index) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  uint8_t *data = get_data(self);
  return data[index];
}

static uint8_t *ut_uint8_subarray_take_data(UtObject *object) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  uint8_t *data = get_data(self);
  uint8_t *copy = malloc(sizeof(uint8_t) * self->length);
  memcpy(copy, data, self->length);
  return copy;
}

static size_t ut_uint8_subarray_get_length(UtObject *object) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  return self->length;
}

static UtObject *ut_uint8_subarray_get_element_object(UtObject *object,
                                                      size_t index) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  uint8_t *data = get_data(self);
  return ut_uint8_new(data[index]);
}

static UtObject *ut_uint8_subarray_get_sublist(UtObject *object, size_t start,
                                               size_t count) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  return ut_uint8_subarray_new(self->parent, self->start + start, count);
}

static UtObject *ut_uint8_subarray_copy(UtObject *object) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  UtObject *copy = ut_uint8_array_new();
  uint8_t *data = get_data(self);
  ut_uint8_list_append_block(copy, data, self->length);
  return copy;
}

static char *ut_uint8_subarray_to_string(UtObject *object) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  uint8_t *data = get_data(self);
  UtObjectRef string = ut_string_new("<uint8>[");
  for (size_t i = 0; i < self->length; i++) {
    if (i != 0) {
      ut_string_append(string, ", ");
    }
    ut_string_append_printf(string, "%d", data[i]);
  }
  ut_string_append(string, "]");

  return ut_string_take_text(string);
}

static void ut_uint8_subarray_cleanup(UtObject *object) {
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  ut_object_unref(self->parent);
}

static UtUint8ListInterface uint8_list_interface = {
    .get_element = ut_uint8_subarray_get_element,
    .take_data = ut_uint8_subarray_take_data};

static UtListInterface list_interface = {
    .is_mutable = false,
    .get_length = ut_uint8_subarray_get_length,
    .get_element = ut_uint8_subarray_get_element_object,
    .get_sublist = ut_uint8_subarray_get_sublist,
    .copy = ut_uint8_subarray_copy};

static UtObjectInterface object_interface = {
    .type_name = "UtUint8Subarray",
    .to_string = ut_uint8_subarray_to_string,
    .cleanup = ut_uint8_subarray_cleanup,
    .interfaces = {{&ut_uint8_list_id, &uint8_list_interface},
                   {&ut_list_id, &list_interface},
                   {NULL, NULL}}};

UtObject *ut_uint8_subarray_new(UtObject *parent, size_t start, size_t length) {
  UtObject *object = ut_object_new(sizeof(UtUint8Subarray), &object_interface);
  UtUint8Subarray *self = (UtUint8Subarray *)object;

  assert(parent != NULL && ut_object_is_uint8_array(parent));
  size_t parent_length = ut_list_get_length(parent);
  assert(start + length <= parent_length);

  self->parent = ut_object_ref(parent);
  self->parent_length = parent_length;
  self->start = start;
  self->length = length;
  return object;
}

uint8_t *ut_uint8_subarray_get_data(UtObject *object) {
  assert(ut_object_is_uint8_subarray(object));
  UtUint8Subarray *self = (UtUint8Subarray *)object;
  return get_data(self);
}

bool ut_object_is_uint8_subarray(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
