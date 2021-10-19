#include <stdbool.h>
#include <stddef.h>

#include "ut-object.h"

#pragma once

typedef size_t (*UtFileReadCallback)(void *user_data, UtObject *data);

UtObject *ut_file_new(const char *path);

void ut_file_open_read(UtObject *object);

void ut_file_open_write(UtObject *object, bool create);

void ut_file_read(UtObject *object, size_t count, UtFileReadCallback callback,
                  void *user_data, UtObject *cancel);

void ut_file_read_stream(UtObject *object, size_t block_size,
                         UtFileReadCallback callback, void *user_data,
                         UtObject *cancel);

void ut_file_read_all(UtObject *object, size_t block_size,
                      UtFileReadCallback callback, void *user_data,
                      UtObject *cancel);

void ut_file_close(UtObject *object);

bool ut_object_is_file(UtObject *object);
