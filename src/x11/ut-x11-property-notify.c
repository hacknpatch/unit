#include <assert.h>

#include "ut-x11-event.h"
#include "ut-x11-property-notify.h"

typedef struct {
  UtObject object;
  uint32_t window;
  uint32_t atom;
} UtX11PropertyNotify;

static UtX11EventInterface x11_event_interface = {};

static UtObjectInterface object_interface = {
    .type_name = "UtX11PropertyNotify",
    .interfaces = {{&ut_x11_event_id, &x11_event_interface}}};

UtObject *ut_x11_property_notify_new(uint32_t window, uint32_t atom) {
  UtObject *object =
      ut_object_new(sizeof(UtX11PropertyNotify), &object_interface);
  UtX11PropertyNotify *self = (UtX11PropertyNotify *)object;
  self->window = window;
  self->atom = atom;
  return object;
}

uint32_t ut_x11_property_notify_get_window(UtObject *object) {
  assert(ut_object_is_x11_property_notify(object));
  UtX11PropertyNotify *self = (UtX11PropertyNotify *)object;
  return self->window;
}

uint32_t ut_x11_property_notify_get_atom(UtObject *object) {
  assert(ut_object_is_x11_property_notify(object));
  UtX11PropertyNotify *self = (UtX11PropertyNotify *)object;
  return self->atom;
}

bool ut_object_is_x11_property_notify(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
