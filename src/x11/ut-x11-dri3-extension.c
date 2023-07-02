#include <assert.h>

#include "ut-x11-buffer.h"
#include "ut-x11-client-private.h"
#include "ut-x11-dri3-extension.h"
#include "ut-x11-extension.h"
#include "ut.h"

typedef struct {
  UtObject object;
  void *callback;
  void *user_data;
} CallbackData;

static UtObjectInterface callback_data_object_interface = {
    .type_name = "Dri3CallbackData"};

static UtObject *callback_data_new(void *callback, void *user_data) {
  UtObject *object =
      ut_object_new(sizeof(CallbackData), &callback_data_object_interface);
  CallbackData *self = (CallbackData *)object;
  self->callback = callback;
  self->user_data = user_data;
  return object;
}

typedef struct {
  UtObject object;
  UtObject *client;
  uint8_t major_opcode;
} UtX11Dri3Extension;

static void query_version_reply_cb(UtObject *object, uint8_t data0,
                                   UtObject *data) {
  CallbackData *callback_data = (CallbackData *)object;

  size_t offset = 0;
  uint32_t major_version = ut_x11_buffer_get_card32(data, &offset);
  uint32_t minor_version = ut_x11_buffer_get_card32(data, &offset);

  if (callback_data->callback != NULL) {
    UtX11ClientDri3QueryVersionCallback callback =
        (UtX11ClientDri3QueryVersionCallback)callback_data->callback;
    callback(callback_data->user_data, major_version, minor_version, NULL);
  }
}

static void query_version_error_cb(UtObject *object, UtObject *error) {
  CallbackData *callback_data = (CallbackData *)object;

  if (callback_data->callback != NULL) {
    UtX11ClientDri3QueryVersionCallback callback =
        (UtX11ClientDri3QueryVersionCallback)callback_data->callback;
    callback(callback_data->user_data, 0, 0, error);
  }
}

static uint8_t ut_x11_dri3_extension_get_major_opcode(UtObject *object) {
  UtX11Dri3Extension *self = (UtX11Dri3Extension *)object;
  return self->major_opcode;
}

static void ut_x11_dri3_extension_close(UtObject *object) {
  UtX11Dri3Extension *self = (UtX11Dri3Extension *)object;
  self->client = NULL;
}

static UtX11ExtensionInterface x11_extension_interface = {
    .get_major_opcode = ut_x11_dri3_extension_get_major_opcode,
    .close = ut_x11_dri3_extension_close};

static UtObjectInterface object_interface = {
    .type_name = "UtX11Dri3Extension",
    .interfaces = {{&ut_x11_extension_id, &x11_extension_interface},
                   {NULL, NULL}}};

UtObject *ut_x11_dri3_extension_new(UtObject *client, uint8_t major_opcode) {
  UtObject *object =
      ut_object_new(sizeof(UtX11Dri3Extension), &object_interface);
  UtX11Dri3Extension *self = (UtX11Dri3Extension *)object;
  self->client = client;
  self->major_opcode = major_opcode;
  return object;
}

void ut_x11_dri3_extension_query_version(
    UtObject *object, UtX11ClientDri3QueryVersionCallback callback,
    void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_dri3_extension(object));
  UtX11Dri3Extension *self = (UtX11Dri3Extension *)object;

  UtObjectRef request = ut_x11_buffer_new();
  ut_x11_buffer_append_card32(request, 1);
  ut_x11_buffer_append_card32(request, 0);

  ut_x11_client_send_request_with_reply(
      self->client, self->major_opcode, 0, request, query_version_reply_cb,
      query_version_error_cb, callback_data_new(callback, user_data), cancel);
}

bool ut_object_is_x11_dri3_extension(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
