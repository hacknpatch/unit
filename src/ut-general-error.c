#include <assert.h>
#include <stdlib.h>

#include "ut.h"

typedef struct {
  UtObject object;
  char *description;
} UtGeneralError;

static char *ut_general_error_to_string(UtObject *object) {
  UtGeneralError *self = (UtGeneralError *)object;
  return ut_cstring_new_printf("<UtGeneralError>(\"%s\")", self->description);
}

static void ut_general_error_cleanup(UtObject *object) {
  UtGeneralError *self = (UtGeneralError *)object;
  free(self->description);
}

static char *ut_general_error_dup_description(UtObject *object) {
  UtGeneralError *self = (UtGeneralError *)object;
  return ut_cstring_new(self->description);
}

static UtErrorInterface error_interface = {
    .get_description = ut_general_error_dup_description};

static UtObjectInterface object_interface = {
    .type_name = "UtGeneralError",
    .to_string = ut_general_error_to_string,
    .cleanup = ut_general_error_cleanup,
    .interfaces = {{&ut_error_id, &error_interface}, {NULL, NULL}}};

UtObject *ut_general_error_new(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  UtObject *object = ut_general_error_new_valist(format, ap);
  va_end(ap);

  return object;
}

UtObject *ut_general_error_new_valist(const char *format, va_list ap) {
  UtObject *object = ut_object_new(sizeof(UtGeneralError), &object_interface);
  UtGeneralError *self = (UtGeneralError *)object;
  self->description = ut_cstring_new_vprintf(format, ap);
  return object;
}

UtObject *ut_general_error_new_literal(const char *description) {
  UtObject *object = ut_object_new(sizeof(UtGeneralError), &object_interface);
  UtGeneralError *self = (UtGeneralError *)object;
  self->description = ut_cstring_new(description);
  return object;
}

const char *ut_general_error_get_description(UtObject *object) {
  assert(ut_object_is_general_error(object));
  UtGeneralError *self = (UtGeneralError *)object;
  return self->description;
}

bool ut_object_is_general_error(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
