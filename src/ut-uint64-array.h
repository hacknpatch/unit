#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ut-object.h"

#pragma once

UtObject *ut_uint64_array_new();

UtObject *ut_uint64_array_new_sized(size_t length);

UtObject *ut_uint64_array_new_from_elements(size_t length, ...);

UtObject *ut_uint64_array_new_from_va_elements(size_t length, va_list ap);

uint64_t *ut_uint64_array_get_data(UtObject *object);

bool ut_object_is_uint64_array(UtObject *object);
