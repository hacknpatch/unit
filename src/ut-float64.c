#include <assert.h>
#include <stdio.h>

#include "ut.h"

typedef struct {
  UtObject object;
  double value;
} UtFloat64;

static char *ut_float64_to_string(UtObject *object) {
  UtFloat64 *self = (UtFloat64 *)object;
  return ut_cstring_new_printf("<float64>(%g)", self->value);
}

static bool ut_float64_equal(UtObject *object, UtObject *other) {
  UtFloat64 *self = (UtFloat64 *)object;
  if (!ut_object_is_float64(other)) {
    return false;
  }
  UtFloat64 *other_self = (UtFloat64 *)other;
  return self->value == other_self->value;
}

static int ut_float64_hash(UtObject *object) {
  UtFloat64 *self = (UtFloat64 *)object;
  return self->value;
}

static UtObjectInterface object_interface = {.type_name = "UtFloat64",
                                             .to_string = ut_float64_to_string,
                                             .equal = ut_float64_equal,
                                             .hash = ut_float64_hash};

UtObject *ut_float64_new(double value) {
  UtObject *object = ut_object_new(sizeof(UtFloat64), &object_interface);
  UtFloat64 *self = (UtFloat64 *)object;
  self->value = value;
  return object;
}

double ut_float64_get_value(UtObject *object) {
  assert(ut_object_is_float64(object));
  UtFloat64 *self = (UtFloat64 *)object;
  return self->value;
}

bool ut_object_is_float64(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
