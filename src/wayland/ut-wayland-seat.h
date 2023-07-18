#include <stdbool.h>
#include <stdint.h>

#include "ut-object.h"
#include "ut-wayland-keyboard.h"
#include "ut-wayland-pointer.h"
#include "ut-wayland-touch.h"

#pragma once

typedef enum {
  UT_WAYLAND_SEAT_CAPABILITY_POINTER = 1 << 0,
  UT_WAYLAND_SEAT_CAPABILITY_KEYBOARD = 1 << 1,
  UT_WAYLAND_SEAT_CAPABILITY_TOUCH = 1 << 2
} UtWaylandSeatCapability;

typedef void (*UtWaylandSeatCapabilitiesCallback)(UtObject *object,
                                                  uint32_t capabilities);
typedef void (*UtWaylandSeatNameCallback)(UtObject *object, const char *name);

typedef struct {
  UtWaylandSeatCapabilitiesCallback capabilities;
  UtWaylandSeatNameCallback name;
} UtWaylandSeatCallbacks;

UtObject *ut_wayland_seat_new(UtObject *client, uint32_t id,
                              UtObject *callback_object,
                              const UtWaylandSeatCallbacks *callbacks);

UtObject *
ut_wayland_seat_new_from_registry(UtObject *registry, uint32_t name,
                                  UtObject *callback_object,
                                  const UtWaylandSeatCallbacks *callbacks);

UtObject *
ut_wayland_seat_get_pointer(UtObject *object, UtObject *callback_object,
                            const UtWaylandPointerCallbacks *callbacks);

UtObject *
ut_wayland_seat_get_keyboard(UtObject *object, UtObject *callback_object,
                             const UtWaylandKeyboardCallbacks *callbacks);

UtObject *ut_wayland_seat_get_touch(UtObject *object, UtObject *callback_object,
                                    const UtWaylandTouchCallbacks *callbacks);

void ut_wayland_seat_release(UtObject *object);

bool ut_object_is_wayland_seat(UtObject *object);
