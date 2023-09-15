#include <assert.h>

#include "ut.h"

int ut_float64_list_id = 0;

UtObject *ut_float64_list_new() { return ut_float64_array_new(); }

UtObject *ut_float64_list_new_from_elements(size_t length, ...) {
  va_list ap;
  va_start(ap, length);
  return ut_float64_array_new_from_va_elements(length, ap);
  va_end(ap);
}

double ut_float64_list_get_element(UtObject *object, size_t index) {
  UtFloat64ListInterface *float64_list_interface =
      ut_object_get_interface(object, &ut_float64_list_id);
  assert(float64_list_interface != NULL);
  return float64_list_interface->get_element(object, index);
}

const double *ut_float64_list_get_data(UtObject *object) {
  UtFloat64ListInterface *float64_list_interface =
      ut_object_get_interface(object, &ut_float64_list_id);
  assert(float64_list_interface != NULL);
  if (float64_list_interface->get_data != NULL) {
    return float64_list_interface->get_data(object);
  } else {
    return NULL;
  }
}

double *ut_float64_list_get_writable_data(UtObject *object) {
  UtFloat64ListInterface *float64_list_interface =
      ut_object_get_interface(object, &ut_float64_list_id);
  assert(float64_list_interface != NULL);
  if (float64_list_interface->get_writable_data != NULL) {
    return float64_list_interface->get_writable_data(object);
  } else {
    return NULL;
  }
}

double *ut_float64_list_take_data(UtObject *object) {
  UtFloat64ListInterface *float64_list_interface =
      ut_object_get_interface(object, &ut_float64_list_id);
  assert(float64_list_interface != NULL);
  return float64_list_interface->take_data(object);
}

void ut_float64_list_append(UtObject *object, double value) {
  ut_float64_list_append_block(object, &value, 1);
}

void ut_float64_list_append_block(UtObject *object, const double *data,
                                  size_t data_length) {
  size_t length = ut_list_get_length(object);
  ut_float64_list_insert_block(object, length, data, data_length);
}

void ut_float64_list_prepend(UtObject *object, double value) {
  ut_float64_list_prepend_block(object, &value, 1);
}

void ut_float64_list_prepend_block(UtObject *object, const double *data,
                                   size_t data_length) {
  ut_float64_list_insert_block(object, 0, data, data_length);
}

void ut_float64_list_insert(UtObject *object, size_t index, double value) {
  ut_float64_list_insert_block(object, index, &value, 1);
}

void ut_float64_list_insert_block(UtObject *object, size_t index,
                                  const double *data, size_t data_length) {
  UtFloat64ListInterface *float64_list_interface =
      ut_object_get_interface(object, &ut_float64_list_id);
  assert(float64_list_interface != NULL);
  assert(ut_list_is_mutable(object));
  float64_list_interface->insert(object, index, data, data_length);
}

bool ut_object_implements_float64_list(UtObject *object) {
  return ut_object_get_interface(object, &ut_float64_list_id) != NULL;
}
