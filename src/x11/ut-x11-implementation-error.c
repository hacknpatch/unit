#include <assert.h>

#include "ut.h"

typedef struct {
  UtObject object;
} UtX11ImplementationError;

static UtX11ErrorInterface x11_error_interface = {};

static UtObjectInterface object_interface = {
    .type_name = "UtX11ImplementationError",
    .interfaces = {{&ut_x11_error_id, &x11_error_interface}, {NULL, NULL}}};

UtObject *ut_x11_implementation_error_new() {
  return ut_object_new(sizeof(UtX11ImplementationError), &object_interface);
}

bool ut_object_is_x11_implementation_error(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
