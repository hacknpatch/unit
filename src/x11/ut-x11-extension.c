#include <assert.h>

#include "ut-x11-extension.h"
#include "ut.h"

int ut_x11_extension_id = 0;

uint8_t ut_x11_extension_get_major_opcode(UtObject *object) {
  UtX11ExtensionInterface *x11_extension_interface =
      ut_object_get_interface(object, &ut_x11_extension_id);
  assert(x11_extension_interface != NULL);
  return x11_extension_interface->get_major_opcode(object);
}

bool ut_x11_extension_decode_event(UtObject *object, UtObject *data) {
  UtX11ExtensionInterface *x11_extension_interface =
      ut_object_get_interface(object, &ut_x11_extension_id);
  assert(x11_extension_interface != NULL);
  return x11_extension_interface->decode_event != NULL
             ? x11_extension_interface->decode_event(object, data)
             : false;
}

bool ut_x11_extension_decode_generic_event(UtObject *object, uint16_t code,
                                           UtObject *data) {
  UtX11ExtensionInterface *x11_extension_interface =
      ut_object_get_interface(object, &ut_x11_extension_id);
  assert(x11_extension_interface != NULL);
  return x11_extension_interface->decode_generic_event != NULL
             ? x11_extension_interface->decode_generic_event(object, code, data)
             : false;
}

UtObject *ut_x11_extension_decode_error(UtObject *object, UtObject *data) {
  UtX11ExtensionInterface *x11_extension_interface =
      ut_object_get_interface(object, &ut_x11_extension_id);
  assert(x11_extension_interface != NULL);
  return x11_extension_interface->decode_error != NULL
             ? x11_extension_interface->decode_error(object, data)
             : 0;
}

void ut_x11_extension_close(UtObject *object) {
  UtX11ExtensionInterface *x11_extension_interface =
      ut_object_get_interface(object, &ut_x11_extension_id);
  assert(x11_extension_interface != NULL);
  x11_extension_interface->close(object);
}

bool ut_object_implements_x11_extension(UtObject *object) {
  return ut_object_get_interface(object, &ut_x11_extension_id) != NULL;
}
