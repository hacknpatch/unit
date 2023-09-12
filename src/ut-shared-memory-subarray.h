#include <stdbool.h>

#include "ut-object.h"

#pragma once

UtObject *ut_shared_memory_subarray_new(UtObject *parent, size_t start,
                                        size_t length);

/// Returns [true] if [object] is a [UtSharedMemorySubarray].
bool ut_object_is_shared_memory_subarray(UtObject *object);
