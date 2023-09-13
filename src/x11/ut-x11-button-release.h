#include <stdbool.h>
#include <stdint.h>

#include "ut-object.h"

#pragma once

UtObject *ut_x11_button_release_new(uint32_t window, uint8_t button, int16_t x,
                                    int16_t y);

uint32_t ut_x11_button_release_get_window(UtObject *object);

uint8_t ut_x11_button_release_get_button(UtObject *object);

int16_t ut_x11_button_release_get_x(UtObject *object);

int16_t ut_x11_button_release_get_y(UtObject *object);

/// Returns [true] if [object] is a [UtX11ButtonRelease].
bool ut_object_is_x11_button_release(UtObject *object);
