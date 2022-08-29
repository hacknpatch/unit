#include <assert.h>
#include <stdio.h>

#include "ut.h"

typedef struct {
  UtObject object;
  uint8_t value;
} UtUint8;

static char *ut_uint8_to_string(UtObject *object) {
  UtUint8 *self = (UtUint8 *)object;
  return ut_cstring_new_printf("<uint8>(%u)", self->value);
}

static bool ut_uint8_equal(UtObject *object, UtObject *other) {
  UtUint8 *self = (UtUint8 *)object;
  if (!ut_object_is_uint8(other)) {
    return false;
  }
  UtUint8 *other_self = (UtUint8 *)other;
  return self->value == other_self->value;
}

static int ut_uint8_hash(UtObject *object) {
  UtUint8 *self = (UtUint8 *)object;
  return self->value;
}

static UtObjectInterface object_interface = {.type_name = "UtUint8",
                                             .to_string = ut_uint8_to_string,
                                             .equal = ut_uint8_equal,
                                             .hash = ut_uint8_hash};

UtObject *ut_uint8_new(uint8_t value) {
  UtObject *object = ut_object_new(sizeof(UtUint8), &object_interface);
  UtUint8 *self = (UtUint8 *)object;
  self->value = value;
  return object;
}

uint8_t ut_uint8_get_value(UtObject *object) {
  assert(ut_object_is_uint8(object));
  UtUint8 *self = (UtUint8 *)object;
  return self->value;
}

bool ut_object_is_uint8(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
