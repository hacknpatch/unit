#include <stdbool.h>
#include <stddef.h>

#include "ut-object.h"

#pragma once

typedef void (*UtWritableInputStreamReadingCallback)(UtObject *object,
                                                     UtObject *stream);

UtObject *ut_writable_input_stream_new();

void ut_writable_input_stream_set_reading_callback(
    UtObject *object, UtObject *callback_object,
    UtWritableInputStreamReadingCallback reading_callback);

size_t ut_writable_input_stream_write(UtObject *object, UtObject *data,
                                      bool complete);

bool ut_object_is_writable_input_stream(UtObject *object);
