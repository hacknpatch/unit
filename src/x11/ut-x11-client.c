#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ut-x11-buffer.h"
#include "ut-x11-client-private.h"
#include "ut-x11-core.h"
#include "ut-x11-dri3-extension.h"
#include "ut-x11-extension.h"
#include "ut-x11-generic-event-extension.h"
#include "ut-x11-present-extension.h"
#include "ut-x11-randr-extension.h"
#include "ut-x11-shape-extension.h"
#include "ut-x11-shm-extension.h"
#include "ut-x11-sync-extension.h"
#include "ut-x11-xfixes-extension.h"
#include "ut-x11-xinput-extension.h"
#include "ut.h"

typedef struct _UtX11Client UtX11Client;

typedef struct {
  UtObject object;
  uint16_t sequence_number;
  UtX11ClientDecodeReplyFunction decode_reply_function;
  UtX11ClientHandleErrorFunction handle_error_function;
  UtObject *callback_object;
  UtObject *cancel;
} Request;

static void request_cleanup(UtObject *object) {
  Request *self = (Request *)object;
  ut_object_unref(self->callback_object);
  ut_object_unref(self->cancel);
}

static UtObjectInterface request_object_interface = {
    .type_name = "X11ClientRequest",
    .cleanup = request_cleanup,
    .interfaces = {{NULL, NULL}}};

static UtObject *
request_new(uint16_t sequence_number,
            UtX11ClientDecodeReplyFunction decode_reply_function,
            UtX11ClientHandleErrorFunction handle_error_function,
            UtObject *callback_object, UtObject *cancel) {
  UtObject *object = ut_object_new(sizeof(Request), &request_object_interface);
  Request *self = (Request *)object;
  self->sequence_number = sequence_number;
  self->decode_reply_function = decode_reply_function;
  self->handle_error_function = handle_error_function;
  self->callback_object = callback_object;
  self->cancel = ut_object_ref(cancel);
  return object;
}

typedef enum {
  WINDOW_CLASS_INHERIT_FROM_PARENT = 0,
  WINDOW_CLASS_INPUT_OUTPUT = 1,
  WINDOW_CLASS_INPUT_ONLY = 2
} UtX11WindowClass;

typedef enum {
  VALUE_MASK_BACKGROUND_PIXMAP = 0x00000001,
  VALUE_MASK_BACKGROUND_PIXEL = 0x00000002,
  VALUE_MASK_BORDER_PIXMAP = 0x00000004,
  VALUE_MASK_BORDER_PIXEL = 0x00000008,
  VALUE_MASK_BIT_GRAVITY = 0x00000010,
  VALUE_MASK_WIN_GRAVITY = 0x00000020,
  VALUE_MASK_BACKING_STORE = 0x00000040,
  VALUE_MASK_BACKING_PLANES = 0x00000080,
  VALUE_MASK_BACKING_PIXEL = 0x00000100,
  VALUE_MASK_OVERRIDE_REDIRECT = 0x00000200,
  VALUE_MASK_SAVE_UNDER = 0x00000400,
  VALUE_MASK_EVENT_MASK = 0x00000800,
  VALUE_MASK_DO_NOT_PROPAGATE_MASK = 0x00001000,
  VALUE_MASK_COLORMAP = 0x00002000,
  VALUE_MASK_CURSOR = 0x00004000
} UtX11ValueMask;

struct _UtX11Client {
  UtObject object;
  UtObject *cancel;
  UtObject *socket;
  UtObject *read_cancel;

  UtObject *extensions;
  UtObject *core;
  UtObject *shape_extension;
  UtObject *shm_extension;
  UtObject *xinput_extension;
  UtObject *sync_extension;
  UtObject *xfixes_extension;
  UtObject *randr_extension;
  UtObject *present_extension;
  UtObject *dri3_extension;

  const UtX11EventCallbacks *event_callbacks;
  UtX11ClientErrorCallback error_callback;
  void *callback_user_data;
  UtObject *callback_cancel;

  UtX11ClientConnectCallback connect_callback;
  void *connect_user_data;
  UtObject *connect_cancel;

  bool setup_complete;
  char *vendor;
  uint32_t release_number;
  uint32_t resource_id_base;
  uint32_t resource_id_mask;
  uint16_t maximum_request_length;
  UtObject *pixmap_formats;
  UtObject *screens;

  uint32_t next_resource_id;
  uint16_t sequence_number;
  UtObject *requests;
};

UtObject *get_extension_by_major_opcode(UtX11Client *self,
                                        uint8_t major_opcode) {
  size_t extensions_length = ut_list_get_length(self->extensions);
  for (size_t i = 0; i < extensions_length; i++) {
    UtObject *extension = ut_object_list_get_element(self->extensions, i);
    if (ut_x11_extension_get_major_opcode(extension) == major_opcode) {
      return extension;
    }
  }

  return NULL;
}

static Request *find_request(UtX11Client *self, uint16_t sequence_number) {
  size_t requests_length = ut_list_get_length(self->requests);
  for (size_t i = 0; i < requests_length; i++) {
    Request *request = (Request *)ut_object_list_get_element(self->requests, i);
    if (request->sequence_number == sequence_number) {
      return request;
    }
  }

  return NULL;
}

static void generic_event_query_version_cb(void *user_data,
                                           uint16_t major_version,
                                           uint16_t minor_version,
                                           UtObject *error) {}

static void shape_query_version_cb(void *user_data, uint16_t major_version,
                                   uint16_t minor_version, UtObject *error) {}

static void xinput_query_version_cb(void *user_data, uint16_t major_version,
                                    uint16_t minor_version, UtObject *error) {}

static void shm_query_version_cb(void *user_data, uint16_t major_version,
                                 uint16_t minor_version, uint16_t uid,
                                 uint16_t gid, uint8_t pixmap_format,
                                 bool shared_pixmaps, UtObject *error) {}

static void big_requests_enable_cb(void *user_data,
                                   uint32_t maximum_request_length,
                                   UtObject *error) {
  UtX11Client *self = user_data;
  self->maximum_request_length = maximum_request_length;
}

static void sync_initialize_cb(void *user_data, uint8_t version_major,
                               uint8_t version_minor, UtObject *error) {}

static void xfixes_query_version_cb(void *user_data, uint32_t version_major,
                                    uint32_t version_minor, UtObject *error) {}

static void randr_query_version_cb(void *user_data, uint32_t version_major,
                                   uint32_t version_minor, UtObject *error) {}

static void present_query_version_cb(void *user_data, uint32_t version_major,
                                     uint32_t version_minor, UtObject *error) {}

static void dri3_query_version_cb(void *user_data, uint32_t version_major,
                                  uint32_t version_minor, UtObject *error) {}

static void query_generic_event_cb(void *user_data, bool present,
                                   uint8_t major_opcode, uint8_t first_event,
                                   uint8_t first_error, UtObject *error) {
  UtX11Client *self = user_data;
  if (present) {
    UtObjectRef generic_event_extension =
        ut_x11_generic_event_extension_new((UtObject *)self, major_opcode);
    ut_list_append(self->extensions, generic_event_extension);

    ut_x11_generic_event_extension_query_version(generic_event_extension,
                                                 generic_event_query_version_cb,
                                                 self, self->cancel);
  }
}

static void query_shape_cb(void *user_data, bool present, uint8_t major_opcode,
                           uint8_t first_event, uint8_t first_error,
                           UtObject *error) {
  UtX11Client *self = user_data;
  if (present) {
    self->shape_extension =
        ut_x11_shape_extension_new((UtObject *)self, major_opcode, first_event,
                                   NULL, NULL, self->callback_cancel);
    ut_list_append(self->extensions, self->shape_extension);

    ut_x11_shape_extension_query_version(
        self->shape_extension, shape_query_version_cb, self, self->cancel);
  }
}

static void query_shm_cb(void *user_data, bool present, uint8_t major_opcode,
                         uint8_t first_event, uint8_t first_error,
                         UtObject *error) {
  UtX11Client *self = user_data;
  if (present) {
    self->shm_extension = ut_x11_shm_extension_new(
        (UtObject *)self, major_opcode, first_event, first_error);
    ut_list_append(self->extensions, self->shm_extension);

    ut_x11_shm_extension_query_version(
        self->shm_extension, shm_query_version_cb, self, self->cancel);
  }
}

static void query_xinput_cb(void *user_data, bool present, uint8_t major_opcode,
                            uint8_t first_event, uint8_t first_error,
                            UtObject *error) {
  UtX11Client *self = user_data;
  if (present) {
    self->xinput_extension = ut_x11_xinput_extension_new(
        (UtObject *)self, major_opcode, first_event, first_error,
        self->event_callbacks, self->callback_user_data, self->callback_cancel);
    ut_list_append(self->extensions, self->xinput_extension);

    ut_x11_xinput_extension_query_version(
        self->xinput_extension, xinput_query_version_cb, self, self->cancel);
  }
}

static void query_big_requests_cb(void *user_data, bool present,
                                  uint8_t major_opcode, uint8_t first_event,
                                  uint8_t first_error, UtObject *error) {
  UtX11Client *self = user_data;

  if (present) {
    UtObjectRef big_requests_extension =
        ut_x11_big_requests_extension_new((UtObject *)self, major_opcode);
    ut_list_append(self->extensions, big_requests_extension);

    ut_x11_big_requests_extension_enable(
        big_requests_extension, big_requests_enable_cb, self, self->cancel);
  }
}

static void query_sync_cb(void *user_data, bool present, uint8_t major_opcode,
                          uint8_t first_event, uint8_t first_error,
                          UtObject *error) {
  UtX11Client *self = user_data;

  if (present) {
    self->sync_extension = ut_x11_sync_extension_new(
        (UtObject *)self, major_opcode, first_event, first_error,
        self->event_callbacks, self->callback_user_data, self->callback_cancel);
    ut_list_append(self->extensions, self->sync_extension);

    ut_x11_sync_extension_initialize(self->sync_extension, sync_initialize_cb,
                                     self, self->cancel);
  }
}

static void query_xfixes_cb(void *user_data, bool present, uint8_t major_opcode,
                            uint8_t first_event, uint8_t first_error,
                            UtObject *error) {
  UtX11Client *self = user_data;

  if (present) {
    self->xfixes_extension = ut_x11_xfixes_extension_new(
        (UtObject *)self, major_opcode, first_event, first_error,
        self->event_callbacks, self->callback_user_data, self->callback_cancel);
    ut_list_append(self->extensions, self->xfixes_extension);

    ut_x11_xfixes_extension_query_version(
        self->xfixes_extension, xfixes_query_version_cb, self, self->cancel);
  }
}

static void query_randr_cb(void *user_data, bool present, uint8_t major_opcode,
                           uint8_t first_event, uint8_t first_error,
                           UtObject *error) {
  UtX11Client *self = user_data;

  if (present) {
    self->randr_extension = ut_x11_randr_extension_new(
        (UtObject *)self, major_opcode, first_event, first_error,
        self->event_callbacks, self->callback_user_data, self->callback_cancel);
    ut_list_append(self->extensions, self->randr_extension);

    ut_x11_randr_extension_query_version(
        self->randr_extension, randr_query_version_cb, self, self->cancel);
  }
}

static void query_present_cb(void *user_data, bool present,
                             uint8_t major_opcode, uint8_t first_event,
                             uint8_t first_error, UtObject *error) {
  UtX11Client *self = user_data;

  if (present) {
    self->present_extension = ut_x11_present_extension_new(
        (UtObject *)self, major_opcode, self->event_callbacks,
        self->callback_user_data, self->callback_cancel);
    ut_list_append(self->extensions, self->present_extension);

    ut_x11_present_extension_query_version(
        self->present_extension, present_query_version_cb, self, self->cancel);
  }
}

static void query_dri3_cb(void *user_data, bool present, uint8_t major_opcode,
                          uint8_t first_event, uint8_t first_error,
                          UtObject *error) {
  UtX11Client *self = user_data;

  if (present) {
    self->dri3_extension =
        ut_x11_dri3_extension_new((UtObject *)self, major_opcode);
    ut_list_append(self->extensions, self->dri3_extension);

    ut_x11_dri3_extension_query_version(
        self->dri3_extension, dri3_query_version_cb, self, self->cancel);

    // FIXME: More reliably do this on the last setup request.
    self->connect_callback(self->connect_user_data, NULL);
  }
}

static void send_request(UtObject *object, uint8_t opcode, uint8_t data0,
                         UtObject *data,
                         UtX11ClientDecodeReplyFunction decode_reply_function,
                         UtX11ClientHandleErrorFunction handle_error_function,
                         UtObject *callback_object, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;

  size_t data_length = data != NULL ? ut_list_get_length(data) : 0;
  assert(data_length % 4 == 0);

  UtObjectRef request = ut_x11_buffer_new();
  ut_x11_buffer_append_card8(request, opcode);
  ut_x11_buffer_append_card8(request, data0);
  ut_x11_buffer_append_card16(request, 1 + data_length / 4);
  if (data != NULL) {
    ut_list_append_list(ut_x11_buffer_get_data(request),
                        ut_x11_buffer_get_data(data));
    ut_list_append_list(ut_x11_buffer_get_fds(request),
                        ut_x11_buffer_get_fds(data));
  }

  self->sequence_number++;

  if (decode_reply_function != NULL) {
    UtObjectRef request =
        request_new(self->sequence_number, decode_reply_function,
                    handle_error_function, callback_object, cancel);
    ut_list_append(self->requests, request);
  }

  ut_output_stream_write(self->socket, request);
}

static size_t decode_setup_failed(UtX11Client *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);
  if (data_length < 8) {
    return 0;
  }

  size_t offset = 0;
  assert(ut_x11_buffer_get_card8(data, &offset) == 0);
  uint8_t reason_length = ut_x11_buffer_get_card8(data, &offset);
  /*uint16_t protocol_major_version = */ ut_x11_buffer_get_card16(data,
                                                                  &offset);
  /*uint16_t protocol_minor_version = */ ut_x11_buffer_get_card16(data,
                                                                  &offset);
  uint16_t length = ut_x11_buffer_get_card16(data, &offset);
  size_t message_length = (length + 2) * 4;
  if (data_length < message_length) {
    return 0;
  }
  ut_cstring_ref reason =
      ut_x11_buffer_get_string8(data, &offset, reason_length);

  UtObjectRef error =
      ut_general_error_new("Failed to connect to X server: %s", reason);
  self->connect_callback(self->connect_user_data, error);

  return offset;
}

static size_t decode_setup_success(UtX11Client *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);
  if (data_length < 8) {
    return 0;
  }

  size_t offset = 0;
  assert(ut_x11_buffer_get_card8(data, &offset) == 1);
  ut_x11_buffer_get_padding(data, &offset, 1);
  /*uint16_t protocol_major_version = */ ut_x11_buffer_get_card16(data,
                                                                  &offset);
  /*uint16_t protocol_minor_version = */ ut_x11_buffer_get_card16(data,
                                                                  &offset);
  uint16_t length = ut_x11_buffer_get_card16(data, &offset);
  size_t message_length = (length + 2) * 4;
  if (data_length < message_length) {
    return 0;
  }
  self->release_number = ut_x11_buffer_get_card32(data, &offset);
  self->resource_id_base = ut_x11_buffer_get_card32(data, &offset);
  self->resource_id_mask = ut_x11_buffer_get_card32(data, &offset);
  ut_x11_buffer_get_card32(data, &offset); // motion_buffer_size
  uint16_t vendor_length = ut_x11_buffer_get_card16(data, &offset);
  self->maximum_request_length = ut_x11_buffer_get_card16(data, &offset);
  size_t screens_length = ut_x11_buffer_get_card8(data, &offset);
  size_t pixmap_formats_length = ut_x11_buffer_get_card8(data, &offset);
  ut_x11_buffer_get_card8(data, &offset); // image_byte_order
  ut_x11_buffer_get_card8(data, &offset); // bitmap_format_bit_order
  ut_x11_buffer_get_card8(data, &offset); // bitmap_format_scanline_unit
  ut_x11_buffer_get_card8(data, &offset); // bitmap_format_scanline_pad
  ut_x11_buffer_get_card8(data, &offset); // min_keycode
  ut_x11_buffer_get_card8(data, &offset); // max_keycode
  ut_x11_buffer_get_padding(data, &offset, 4);
  self->vendor = ut_x11_buffer_get_string8(data, &offset, vendor_length);
  ut_x11_buffer_get_align_padding(data, &offset, 4);

  self->pixmap_formats = ut_object_list_new();
  for (size_t i = 0; i < pixmap_formats_length; i++) {
    uint8_t depth = ut_x11_buffer_get_card8(data, &offset);
     uint8_t bits_per_pixel =
        ut_x11_buffer_get_card8(data, &offset);
    uint8_t scanline_pad =
        ut_x11_buffer_get_card8(data, &offset);
    ut_x11_buffer_get_padding(data, &offset, 5);
    UtObjectRef pixmap_format = ut_x11_pixmap_format_new(depth, bits_per_pixel, scanline_pad);
     ut_list_append(self->pixmap_formats, pixmap_format);
  }

  self->screens = ut_object_list_new();
  for (size_t i = 0; i < screens_length; i++) {
    uint32_t root = ut_x11_buffer_get_card32(data, &offset);
    uint32_t default_colormap = ut_x11_buffer_get_card32(data, &offset);
    uint32_t white_pixel = ut_x11_buffer_get_card32(data, &offset);
    uint32_t black_pixel = ut_x11_buffer_get_card32(data, &offset);
    uint32_t current_input_masks = ut_x11_buffer_get_card32(data, &offset);
    uint16_t width_in_pixels = ut_x11_buffer_get_card16(data, &offset);
    uint16_t height_in_pixels = ut_x11_buffer_get_card16(data, &offset);
    uint16_t width_in_millimeters = ut_x11_buffer_get_card16(data, &offset);
    uint16_t height_in_millimeters = ut_x11_buffer_get_card16(data, &offset);
    ut_x11_buffer_get_card16(data, &offset); // min_installed_maps
    ut_x11_buffer_get_card16(data, &offset); // max_installed_maps
    uint32_t root_visual_id = ut_x11_buffer_get_card32(data, &offset);
    ut_x11_buffer_get_card8(data, &offset); // backing_stores
    ut_x11_buffer_get_card8(data, &offset); // save_unders
    ut_x11_buffer_get_card8(data, &offset); // root_depth

    UtObjectRef visuals = ut_object_list_new();
    UtObject *root_visual = NULL;
    size_t allowed_depths_length = ut_x11_buffer_get_card8(data, &offset);
    for (size_t j = 0; j < allowed_depths_length; j++) {
      uint8_t depth = ut_x11_buffer_get_card8(data, &offset);
      ut_x11_buffer_get_padding(data, &offset, 1);
      size_t visuals_length = ut_x11_buffer_get_card16(data, &offset);
      ut_x11_buffer_get_padding(data, &offset, 4);
      UtObjectRef visuals = ut_object_list_new();
      for (size_t k = 0; k < visuals_length; k++) {
        uint32_t id = ut_x11_buffer_get_card32(data, &offset);
        uint8_t class = ut_x11_buffer_get_card8(data, &offset);
        uint8_t bits_per_rgb_value = ut_x11_buffer_get_card8(data, &offset);
        uint16_t colormap_entries = ut_x11_buffer_get_card16(data, &offset);
        uint32_t red_mask = ut_x11_buffer_get_card32(data, &offset);
        uint32_t green_mask = ut_x11_buffer_get_card32(data, &offset);
        uint32_t blue_mask = ut_x11_buffer_get_card32(data, &offset);
        ut_x11_buffer_get_padding(data, &offset, 4);

        UtObjectRef visual = ut_x11_visual_new(
            id, depth, class, bits_per_rgb_value, colormap_entries, red_mask,
            green_mask, blue_mask);
        ut_list_append(visuals, visual);

        if (id == root_visual_id) {
          ut_object_set(&root_visual, visual);
        }
      }
    }

    UtObjectRef screen = ut_x11_screen_new(
        root, default_colormap, white_pixel, black_pixel, current_input_masks,
        width_in_pixels, height_in_pixels, width_in_millimeters,
        height_in_millimeters, root_visual, visuals);
    ut_list_append(self->screens, screen);
  }

  self->setup_complete = true;

  ut_x11_core_query_extension(self->core, "Generic Event Extension",
                              query_generic_event_cb, self, self->cancel);
  ut_x11_core_query_extension(self->core, "SHAPE", query_shape_cb, self,
                              self->cancel);
  ut_x11_core_query_extension(self->core, "MIT-SHM", query_shm_cb, self,
                              self->cancel);
  ut_x11_core_query_extension(self->core, "XInputExtension", query_xinput_cb,
                              self, self->cancel);
  ut_x11_core_query_extension(self->core, "BIG-REQUESTS", query_big_requests_cb,
                              self, self->cancel);
  ut_x11_core_query_extension(self->core, "SYNC", query_sync_cb, self,
                              self->cancel);
  ut_x11_core_query_extension(self->core, "XFIXES", query_xfixes_cb, self,
                              self->cancel);
  ut_x11_core_query_extension(self->core, "RANDR", query_randr_cb, self,
                              self->cancel);
  ut_x11_core_query_extension(self->core, "Present", query_present_cb, self,
                              self->cancel);
  ut_x11_core_query_extension(self->core, "DRI3", query_dri3_cb, self,
                              self->cancel);

  return offset;
}

static size_t decode_setup_authenticate(UtObject *message) { return 0; }

static size_t decode_setup_message(UtX11Client *self, UtObject *message) {
  uint8_t status = ut_uint8_list_get_element(message, 0);
  switch (status) {
  case 0:
    return decode_setup_failed(self, message);
  case 1:
    return decode_setup_success(self, message);
  case 2:
    return decode_setup_authenticate(message);
  default:
    assert(false);
  }
}

static size_t decode_error(UtX11Client *self, UtObject *data) {
  if (ut_list_get_length(data) < 32) {
    return false;
  }

  UtObjectRef error_data = ut_list_get_sublist(data, 0, 32);

  size_t offset = 0;
  assert(ut_x11_buffer_get_card8(error_data, &offset) == 0);
  uint8_t raw_code = ut_x11_buffer_get_card8(error_data, &offset);
  uint16_t sequence_number = ut_x11_buffer_get_card16(error_data, &offset);
  uint32_t value = ut_x11_buffer_get_card32(error_data, &offset);
  uint16_t minor_opcode = ut_x11_buffer_get_card16(error_data, &offset);
  uint8_t major_opcode = ut_x11_buffer_get_card8(error_data, &offset);

  UtX11ErrorCode code = UT_X11_ERROR_UNKNOWN;
  size_t extensions_length = ut_list_get_length(self->extensions);
  for (size_t i = 0; i < extensions_length; i++) {
    UtObject *extension = ut_object_list_get_element(self->extensions, i);
    uint8_t first_error = ut_x11_extension_get_first_error(extension);
    if (raw_code >= first_error) {
      code = ut_x11_extension_decode_error(extension, raw_code - first_error);
      if (code != UT_X11_ERROR_UNKNOWN) {
        break;
      }
    }
  }
  UtObjectRef error = ut_x11_error_new(code, value, major_opcode, minor_opcode);

  Request *request = find_request(self, sequence_number);
  if (request != NULL) {
    if (!ut_cancel_is_active(request->cancel)) {
      request->handle_error_function(request->callback_object, error);
    }
  } else if (self->error_callback != NULL) {
    self->error_callback(self->callback_user_data, error);
  }

  return 32;
}

static size_t decode_reply(UtX11Client *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);
  if (data_length < 4) {
    return 0;
  }

  size_t offset = 0;
  assert(ut_x11_buffer_get_card8(data, &offset) == 1);
  uint8_t data0 = ut_x11_buffer_get_card8(data, &offset);
  uint16_t sequence_number = ut_x11_buffer_get_card16(data, &offset);
  uint32_t length = ut_x11_buffer_get_card32(data, &offset);

  size_t payload_length = 32 + length * 4;
  if (data_length < payload_length) {
    return 0;
  }

  Request *request = find_request(self, sequence_number);
  if (request != NULL) {
    UtObjectRef payload =
        ut_list_get_sublist(data, offset, payload_length - offset);
    if (!ut_cancel_is_active(request->cancel)) {
      request->decode_reply_function(request->callback_object, data0, payload);
    }
  } else {
    // FIXME: Warn about unexpected reply.
  }

  return payload_length;
}

static size_t decode_generic_event(UtX11Client *self, uint8_t data0,
                                   UtObject *data) {
  size_t offset = 0;
  uint8_t major_opcode = data0;
  uint32_t extra_length = ut_x11_buffer_get_card32(data, &offset);
  uint16_t code = ut_x11_buffer_get_card16(data, &offset);

  size_t total_length = 32 + extra_length * 4;
  if (ut_list_get_length(data) < total_length) {
    return 0;
  }
  UtObjectRef event_data = ut_list_get_sublist(data, 10, total_length);

  UtObject *extension = get_extension_by_major_opcode(self, major_opcode);
  if (extension != NULL &&
      ut_x11_extension_decode_generic_event(extension, code, event_data)) {
    return total_length;
  }

  if (self->event_callbacks->unknown_generic_event != NULL &&
      !ut_cancel_is_active(self->callback_cancel)) {
    self->event_callbacks->unknown_generic_event(self->callback_user_data,
                                                 major_opcode, code);
  }

  return total_length;
}

static bool decode_extension_event(UtX11Client *self, uint8_t code,
                                   bool from_send_event,
                                   uint16_t sequence_number, uint8_t data0,
                                   UtObject *data) {
  size_t extensions_length = ut_list_get_length(self->extensions);
  for (size_t i = 0; i < extensions_length; i++) {
    UtObject *extension = ut_object_list_get_element(self->extensions, i);
    uint8_t first_event = ut_x11_extension_get_first_event(extension);
    if (code >= first_event &&
        ut_x11_extension_decode_event(extension, code - first_event,
                                      from_send_event, sequence_number, data0,
                                      data)) {
      return true;
    }
  }

  return false;
}

static size_t decode_event(UtX11Client *self, UtObject *data) {
  if (ut_list_get_length(data) < 32) {
    return 0;
  }

  size_t offset = 0;
  uint8_t code = ut_x11_buffer_get_card8(data, &offset);
  bool from_send_event = (code & 0x80) != 0;
  code &= 0x7f;
  uint8_t event_data0 = ut_x11_buffer_get_card8(data, &offset);
  uint16_t sequence_number = ut_x11_buffer_get_card16(data, &offset);
  UtObjectRef event_data = ut_list_get_sublist(data, offset, 32 - offset);

  switch (code) {
  case 35:
    return decode_generic_event(self, event_data0, event_data);
  default:
    if (!decode_extension_event(self, code, from_send_event, sequence_number,
                                event_data0, event_data)) {
      if (self->event_callbacks->unknown_event != NULL &&
          !ut_cancel_is_active(self->callback_cancel)) {
        self->event_callbacks->unknown_event(self->callback_user_data, code,
                                             from_send_event, sequence_number,
                                             event_data0, event_data);
      }
    }
  }

  return 32;
}

static size_t decode_message(UtX11Client *self, UtObject *data) {
  uint8_t code = ut_uint8_list_get_element(data, 0);
  if (code == 0) {
    return decode_error(self, data);
  } else if (code == 1) {
    return decode_reply(self, data);
  } else {
    return decode_event(self, data);
  }
}

static size_t read_cb(void *user_data, UtObject *data, bool complete) {
  UtX11Client *self = user_data;

  size_t offset = 0;
  if (!self->setup_complete) {
    UtObjectRef buffer = ut_x11_buffer_new_from_data(data);
    offset = decode_setup_message(self, buffer);
    if (offset == 0) {
      return 0;
    }
  }

  size_t data_length = ut_list_get_length(data);
  while (offset < data_length) {
    UtObjectRef message =
        ut_list_get_sublist(data, offset, data_length - offset);
    UtObjectRef buffer = ut_x11_buffer_new_from_data(message);
    size_t n_used = decode_message(self, buffer);
    if (n_used == 0) {
      return offset;
    }
    offset += n_used;
  }

  if (complete) {
    ut_cancel_activate(self->read_cancel);
  }

  return offset;
}

static void connect_cb(void *user_data) {
  UtX11Client *self = user_data;

  UtObjectRef setup = ut_x11_buffer_new();
  ut_x11_buffer_append_card8(setup, 0x6c); // Little endian.
  ut_x11_buffer_append_padding(setup, 1);
  ut_x11_buffer_append_card16(setup, 11); // Protocol major version.
  ut_x11_buffer_append_card16(setup, 0);  // Protocol minor version.
  ut_x11_buffer_append_card16(setup, 0);  // Authorizaton protocol name length.
  ut_x11_buffer_append_card16(setup, 0);  // Authorizaton protocol data length.
  ut_x11_buffer_append_padding(setup, 2);
  // Authorization protocol name.
  ut_x11_buffer_append_align_padding(setup, 4);
  // Authorization protocol data.
  ut_x11_buffer_append_align_padding(setup, 4);

  ut_output_stream_write(self->socket, setup);
}

static void ut_x11_client_init(UtObject *object) {
  UtX11Client *self = (UtX11Client *)object;
  self->cancel = ut_cancel_new();
  self->read_cancel = ut_cancel_new();
  self->extensions = ut_object_list_new();
  self->requests = ut_object_list_new();
}

static void ut_x11_client_cleanup(UtObject *object) {
  UtX11Client *self = (UtX11Client *)object;

  ut_cancel_activate(self->cancel);
  ut_cancel_activate(self->read_cancel);

  size_t extensions_length = ut_list_get_length(self->extensions);
  for (size_t i = 0; i < extensions_length; i++) {
    UtObject *extension = ut_object_list_get_element(self->extensions, i);
    ut_x11_extension_close(extension);
  }

  ut_object_unref(self->cancel);
  ut_object_unref(self->socket);
  ut_object_unref(self->read_cancel);
  ut_object_unref(self->extensions);
  ut_object_unref(self->core);
  ut_object_unref(self->shape_extension);
  ut_object_unref(self->shm_extension);
  ut_object_unref(self->xinput_extension);
  ut_object_unref(self->sync_extension);
  ut_object_unref(self->xfixes_extension);
  ut_object_unref(self->randr_extension);
  ut_object_unref(self->present_extension);
  ut_object_unref(self->dri3_extension);
  ut_object_unref(self->callback_cancel);
  ut_object_unref(self->connect_cancel);
  free(self->vendor);
  ut_object_unref(self->pixmap_formats);
  ut_object_unref(self->screens);
  ut_object_unref(self->requests);
}

static UtObjectInterface object_interface = {.type_name = "UtX11Client",
                                             .init = ut_x11_client_init,
                                             .cleanup = ut_x11_client_cleanup,
                                             .interfaces = {{NULL, NULL}}};

UtObject *ut_x11_client_new(const UtX11EventCallbacks *event_callbacks,
                            UtX11ClientErrorCallback error_callback,
                            void *user_data, UtObject *cancel) {
  UtObject *object = ut_object_new(sizeof(UtX11Client), &object_interface);
  UtX11Client *self = (UtX11Client *)object;
  self->event_callbacks = event_callbacks;
  self->error_callback = error_callback;
  self->callback_user_data = user_data;
  self->callback_cancel = ut_object_ref(cancel);
  return object;
}

void ut_x11_client_connect(UtObject *object,
                           UtX11ClientConnectCallback callback, void *user_data,
                           UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;

  const char *display = getenv("DISPLAY");
  if (display == NULL) {
    // FIXME: Generate error
    return;
  }

  char *divider = strchr(display, ':');
  if (divider == NULL) {
    // FIXME: Generate error
    return;
  }
  size_t divider_index = divider - display;

  ut_cstring_ref host = ut_cstring_substring(display, 0, divider_index);
  int display_number = atoi(divider + 1);

  assert(callback != NULL);

  assert(self->connect_callback == NULL);
  self->connect_callback = callback;
  self->connect_user_data = user_data;
  self->connect_cancel = ut_object_ref(cancel);

  self->core = ut_x11_core_new(object, self->event_callbacks,
                               self->callback_user_data, self->callback_cancel);
  ut_list_append(self->extensions, self->core);

  ut_cstring_ref socket_path =
      ut_cstring_new_printf("/tmp/.X11-unix/X%d", display_number);
  UtObjectRef address = ut_unix_socket_address_new(socket_path);
  self->socket = ut_tcp_socket_new(address, 0);
  ut_tcp_socket_connect(self->socket, connect_cb, self, self->cancel);
  ut_input_stream_read(self->socket, read_cb, self, self->read_cancel);
}

uint32_t ut_x11_client_create_resource_id(UtObject *object) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;

  return self->resource_id_base | (self->next_resource_id++);
}

void ut_x11_client_send_request(UtObject *object, uint8_t opcode, uint8_t data0,
                                UtObject *data) {
  send_request(object, opcode, data0, data, NULL, NULL, NULL, NULL);
}

void ut_x11_client_send_request_with_reply(
    UtObject *object, uint8_t opcode, uint8_t data0, UtObject *data,
    UtX11ClientDecodeReplyFunction decode_reply_function,
    UtX11ClientHandleErrorFunction handle_error_function,
    UtObject *callback_object, UtObject *cancel) {
  send_request(object, opcode, data0, data, decode_reply_function,
               handle_error_function, callback_object, cancel);
}

uint32_t ut_x11_client_create_window(UtObject *object, int16_t x, int16_t y,
                                     uint16_t width, uint16_t height,
                                     uint32_t event_mask) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;

  assert(ut_list_get_length(self->screens) > 0);
  UtObject *screen = ut_object_list_get_element(self->screens, 0);
  UtObject *root_visual = ut_x11_screen_get_root_visual(screen);
  return ut_x11_core_create_window(
      self->core, ut_x11_screen_get_root(screen), x, y, width, height,
      ut_x11_visual_get_depth(root_visual), ut_x11_visual_get_id(root_visual),
      event_mask);
}

void ut_x11_client_destroy_window(UtObject *object, uint32_t window) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_destroy_window(self->core, window);
}

void ut_x11_client_map_window(UtObject *object, uint32_t window) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_map_window(self->core, window);
}

void ut_x11_client_unmap_window(UtObject *object, uint32_t window) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_unmap_window(self->core, window);
}

void ut_x11_client_configure_window(UtObject *object, uint32_t window,
                                    int16_t x, int16_t y, uint16_t width,
                                    uint16_t height) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_configure_window(self->core, window, x, y, width, height);
}

void ut_x11_client_intern_atom(UtObject *object, const char *name,
                               bool only_if_exists,
                               UtX11InternAtomCallback callback,
                               void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_intern_atom(self->core, name, only_if_exists, callback, user_data,
                          cancel);
}

void ut_x11_client_get_atom_name(UtObject *object, uint32_t atom,
                                 UtX11GetAtomNameCallback callback,
                                 void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_get_atom_name(self->core, atom, callback, user_data, cancel);
}

void ut_x11_client_change_property_uint8(UtObject *object, uint32_t window,
                                         uint32_t property, uint32_t type,
                                         UtObject *data) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_change_property_uint8(self->core, window, property, type, data);
}

void ut_x11_client_change_property_uint16(UtObject *object, uint32_t window,
                                          uint32_t property, uint32_t type,
                                          UtObject *data) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_change_property_uint16(self->core, window, property, type, data);
}

void ut_x11_client_change_property_uint32(UtObject *object, uint32_t window,
                                          uint32_t property, uint32_t type,
                                          UtObject *data) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_change_property_uint32(self->core, window, property, type, data);
}

void ut_x11_client_change_property_atom(UtObject *object, uint32_t window,
                                        uint32_t property, UtObject *data) {
  ut_x11_client_change_property_uint32(object, window, property,
                                       UT_X11_ATOM_ATOM, data);
}

void ut_x11_client_change_property_string(UtObject *object, uint32_t window,
                                          uint32_t property,
                                          const char *value) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_change_property_string(self->core, window, property, value);
}

void ut_x11_client_delete_property(UtObject *object, uint32_t window,
                                   uint32_t property) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_delete_property(self->core, window, property);
}

void ut_x11_client_get_property(UtObject *object, uint32_t window,
                                uint32_t property,
                                UtX11GetPropertyCallback callback,
                                void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_get_property(self->core, window, property, 0, 0, 0xffffffff,
                           false, callback, user_data, cancel);
}

void ut_x11_client_get_property_full(UtObject *object, uint32_t window,
                                     uint32_t property, uint32_t type,
                                     uint32_t long_offset, uint32_t long_length,
                                     bool delete,
                                     UtX11GetPropertyCallback callback,
                                     void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_get_property(self->core, window, property, type, long_offset,
                           long_length, delete, callback, user_data, cancel);
}

void ut_x11_client_list_properties(UtObject *object, uint32_t window,
                                   UtX11ListPropertiesCallback callback,
                                   void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_list_properties(self->core, window, callback, user_data, cancel);
}

uint32_t ut_x11_client_create_pixmap(UtObject *object, uint32_t drawable,
                                     uint16_t width, uint16_t height,
                                     uint8_t depth) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_core_create_pixmap(self->core, drawable, width, height, depth);
}

void ut_x11_client_free_pixmap(UtObject *object, uint32_t pixmap) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_free_pixmap(self->core, pixmap);
}

uint32_t ut_x11_client_create_gc(UtObject *object, uint32_t drawable) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_core_create_gc(self->core, drawable);
}

void ut_x11_client_free_gc(UtObject *object, uint32_t gc) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_free_gc(self->core, gc);
}

void ut_x11_client_clear_area(UtObject *object, uint32_t window, int16_t x,
                              int16_t y, uint16_t width, uint16_t height,
                              bool exposures) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_clear_area(self->core, window, x, y, width, height, exposures);
}

void ut_x11_client_copy_area(UtObject *object, uint32_t src_drawable,
                             uint32_t dst_drawable, uint32_t gc, int16_t src_x,
                             int16_t src_y, int16_t dst_x, int16_t dst_y,
                             uint16_t width, uint16_t height) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_copy_area(self->core, src_drawable, dst_drawable, gc, src_x,
                        src_y, dst_y, dst_y, width, height);
}

void ut_x11_client_put_image(UtObject *object, uint32_t drawable, uint32_t gc,
                             UtX11ImageFormat format, uint16_t width,
                             uint16_t height, uint8_t depth, int16_t dst_x,
                             int16_t dst_y, uint8_t *data, size_t data_length) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_put_image(self->core, drawable, gc, format, width, height, depth,
                        dst_x, dst_y, data, data_length);
}

void ut_x11_client_query_extension(UtObject *object, const char *name,
                                   UtX11QueryExtensionCallback callback,
                                   void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_query_extension(self->core, name, callback, user_data, cancel);
}

void ut_x11_client_list_extensions(UtObject *object,
                                   UtX11ListExtensionsCallback callback,
                                   void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_list_extensions(self->core, callback, user_data, cancel);
}

void ut_x11_client_bell(UtObject *object) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_bell(self->core);
}

void ut_x11_client_kill_client(UtObject *object, uint32_t resource) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  ut_x11_core_kill_client(self->core, resource);
}

uint32_t ut_x11_client_shm_attach(UtObject *object, uint32_t shmid,
                                  bool read_only) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_shm_extension_attach(self->shm_extension, shmid, read_only);
}

void ut_x11_client_shm_detach(UtObject *object, uint32_t segment) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_shm_extension_detach(self->shm_extension, segment);
}

uint32_t ut_x11_client_shm_create_pixmap(UtObject *object, uint32_t drawable,
                                         uint16_t width, uint16_t height,
                                         uint8_t depth, uint32_t segment,
                                         uint32_t offset) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_shm_extension_create_pixmap(
      self->shm_extension, drawable, width, height, depth, segment, offset);
}

uint32_t ut_x11_client_shm_attach_fd(UtObject *object, UtObject *fd,
                                     bool read_only) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_shm_extension_attach_fd(self->shm_extension, fd, read_only);
}

uint32_t ut_x11_client_shm_create_segment(
    UtObject *object, uint32_t size, bool read_only,
    UtX11ShmCreateSegmentCallback callback, void *user_data, UtObject *cancel) {
  assert(ut_object_is_x11_client(object));
  UtX11Client *self = (UtX11Client *)object;
  return ut_x11_shm_extension_create_segment(
      self->shm_extension, size, read_only, callback, user_data, cancel);
}

bool ut_object_is_x11_client(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
