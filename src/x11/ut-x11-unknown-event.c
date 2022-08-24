#include <assert.h>

#include "ut-cstring.h"
#include "ut-x11-event.h"
#include "ut-x11-unknown-event.h"

typedef struct {
  UtObject object;
  uint8_t code;
} UtX11UnknownEvent;

static char *ut_x11_unknown_event_to_string(UtObject *object) {
  UtX11UnknownEvent *self = (UtX11UnknownEvent *)object;
  return ut_cstring_new_printf("<UtX11UnknownEvent>(%d)", self->code);
}

static UtX11EventInterface x11_event_interface = {};

static UtObjectInterface object_interface = {
    .type_name = "UtX11UnknownEvent",
    .to_string = ut_x11_unknown_event_to_string,
    .interfaces = {{&ut_x11_event_id, &x11_event_interface}}};

UtObject *ut_x11_unknown_event_new(uint8_t code) {
  UtObject *object =
      ut_object_new(sizeof(UtX11UnknownEvent), &object_interface);
  UtX11UnknownEvent *self = (UtX11UnknownEvent *)object;
  self->code = code;
  return object;
}

uint8_t ut_x11_unknown_event_get_code(UtObject *object) {
  assert(ut_object_is_x11_unknown_event(object));
  UtX11UnknownEvent *self = (UtX11UnknownEvent *)object;
  return self->code;
}

bool ut_object_is_x11_unknown_event(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
