#include <stdbool.h>

#include "ut-object.h"

#pragma once

typedef enum {
  UT_PROTOBUF_MESSAGE_FIELD_TYPE_SINGLE,
  UT_PROTOBUF_MESSAGE_FIELD_TYPE_OPTIONAL,
  UT_PROTOBUF_MESSAGE_FIELD_TYPE_REPEATED
} UtProtobufMessageFieldType;

/// Creates a new [UtProtobufMessageField] object.
///
/// !return-ref
UtObject *ut_protobuf_message_field_new(UtObject *value_type, const char *name,
                                        uint32_t number);

/// Creates a new [UtProtobufMessageField] object.
///
/// !return-ref
UtObject *ut_protobuf_message_field_new_optional(UtObject *value_type,
                                                 const char *name,
                                                 uint32_t number);

/// Creates a new [UtProtobufMessageField] object.
///
/// !return-ref
UtObject *ut_protobuf_message_field_new_repeated(UtObject *value_type,
                                                 const char *name,
                                                 uint32_t number);

/// Creates a new [UtProtobufMessageField] object.
///
/// !return-ref
UtObject *ut_protobuf_message_field_new_take(UtObject *value_type,
                                             const char *name, uint32_t number);

/// Creates a new [UtProtobufMessageField] object.
///
/// !return-ref
UtObject *ut_protobuf_message_field_new_optional_take(UtObject *value_type,
                                                      const char *name,
                                                      uint32_t number);

/// Creates a new [UtProtobufMessageField] object.
///
/// !return-ref
UtObject *ut_protobuf_message_field_new_repeated_take(UtObject *value_type,
                                                      const char *name,
                                                      uint32_t number);

UtProtobufMessageFieldType ut_protobuf_message_field_get_type(UtObject *object);

UtObject *ut_protobuf_message_field_get_value_type(UtObject *object);

const char *ut_protobuf_message_field_get_name(UtObject *object);

uint32_t ut_protobuf_message_field_get_number(UtObject *object);

/// Returns [true] if [object] is a [UtProtobufMessageField].
bool ut_object_is_protobuf_message_field(UtObject *object);
