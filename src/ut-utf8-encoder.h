#include <stdbool.h>

#include "ut-object.h"

#pragma once

UtObject *ut_utf8_encoder_new(UtObject *input);

bool ut_object_is_utf8_encoder(UtObject *object);
