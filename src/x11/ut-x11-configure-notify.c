#include <assert.h>

#include "ut.h"

typedef struct {
  UtObject object;
  uint32_t window;
  int16_t x;
  int16_t y;
  uint16_t width;
  uint16_t height;
} UtX11ConfigureNotify;

static UtX11EventInterface x11_event_interface = {};

static UtObjectInterface object_interface = {
    .type_name = "UtX11ConfigureNotify",
    .interfaces = {{&ut_x11_event_id, &x11_event_interface}}};

UtObject *ut_x11_configure_notify_new(uint32_t window, int16_t x, int16_t y,
                                      uint16_t width, uint16_t height) {
  UtObject *object =
      ut_object_new(sizeof(UtX11ConfigureNotify), &object_interface);
  UtX11ConfigureNotify *self = (UtX11ConfigureNotify *)object;
  self->window = window;
  self->x = x;
  self->y = y;
  self->width = width;
  self->height = height;
  return object;
}

uint32_t ut_x11_configure_notify_get_window(UtObject *object) {
  assert(ut_object_is_x11_configure_notify(object));
  UtX11ConfigureNotify *self = (UtX11ConfigureNotify *)object;
  return self->window;
}

int16_t ut_x11_configure_notify_get_x(UtObject *object) {
  assert(ut_object_is_x11_configure_notify(object));
  UtX11ConfigureNotify *self = (UtX11ConfigureNotify *)object;
  return self->x;
}

int16_t ut_x11_configure_notify_get_y(UtObject *object) {
  assert(ut_object_is_x11_configure_notify(object));
  UtX11ConfigureNotify *self = (UtX11ConfigureNotify *)object;
  return self->y;
}

uint16_t ut_x11_configure_notify_get_width(UtObject *object) {
  assert(ut_object_is_x11_configure_notify(object));
  UtX11ConfigureNotify *self = (UtX11ConfigureNotify *)object;
  return self->width;
}

uint16_t ut_x11_configure_notify_get_height(UtObject *object) {
  assert(ut_object_is_x11_configure_notify(object));
  UtX11ConfigureNotify *self = (UtX11ConfigureNotify *)object;
  return self->height;
}

bool ut_object_is_x11_configure_notify(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
