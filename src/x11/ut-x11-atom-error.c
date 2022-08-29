#include <assert.h>

#include "ut.h"

typedef struct {
  UtObject object;
  uint32_t atom;
} UtX11AtomError;

static UtX11ErrorInterface x11_error_interface = {};

static UtObjectInterface object_interface = {
    .type_name = "UtX11AtomError",
    .interfaces = {{&ut_x11_error_id, &x11_error_interface}, {NULL, NULL}}};

UtObject *ut_x11_atom_error_new(uint32_t atom) {
  UtObject *object = ut_object_new(sizeof(UtX11AtomError), &object_interface);
  UtX11AtomError *self = (UtX11AtomError *)object;
  self->atom = atom;
  return object;
}

uint32_t ut_x11_atom_error_get_atom(UtObject *object) {
  assert(ut_object_is_x11_atom_error(object));
  UtX11AtomError *self = (UtX11AtomError *)object;
  return self->atom;
}

bool ut_object_is_x11_atom_error(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
