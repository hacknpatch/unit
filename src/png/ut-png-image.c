#include <assert.h>
#include <stdint.h>

#include "ut.h"

typedef struct {
  UtObject object;
  uint32_t width;
  uint32_t height;
  uint8_t bit_depth;
  UtPngColourType colour_type;
  UtPngInterlaceMethod interlace_method;
  UtObject *data;
} UtPngImage;

static const char *colour_type_to_string(UtPngColourType colour_type) {
  switch (colour_type) {
  case UT_PNG_COLOUR_TYPE_GREYSCALE:
    return "greyscale";
  case UT_PNG_COLOUR_TYPE_TRUECOLOUR:
    return "truecolour";
  case UT_PNG_COLOUR_TYPE_INDEXED_COLOUR:
    return "indexed-colour";
  case UT_PNG_COLOUR_TYPE_GREYSCALE_WITH_ALPHA:
    return "greyscale-with-alpha";
  case UT_PNG_COLOUR_TYPE_TRUECOLOUR_WITH_ALPHA:
    return "truecolour-with-alpha";
  default:
    return "?";
  }
}

static void ut_png_image_init(UtObject *object) {
  UtPngImage *self = (UtPngImage *)object;
  self->interlace_method = UT_PNG_INTERLACE_METHOD_NONE;
}

static void ut_png_image_cleanup(UtObject *object) {
  UtPngImage *self = (UtPngImage *)object;
  ut_object_unref(self->data);
}

static char *ut_png_image_to_string(UtObject *object) {
  UtPngImage *self = (UtPngImage *)object;
  return ut_cstring_new_printf(
      "<UtPngImage>(width: %d, height: %d, bit_depth: %d, colour_type: %s)",
      self->width, self->height, self->bit_depth,
      colour_type_to_string(self->colour_type));
}

static UtObjectInterface object_interface = {.type_name = "UtPngImage",
                                             .init = ut_png_image_init,
                                             .cleanup = ut_png_image_cleanup,
                                             .to_string =
                                                 ut_png_image_to_string,
                                             .interfaces = {{NULL, NULL}}};

UtObject *ut_png_image_new(uint32_t width, uint32_t height, uint8_t bit_depth,
                           UtPngColourType colour_type, UtObject *data) {
  UtObject *object = ut_object_new(sizeof(UtPngImage), &object_interface);
  UtPngImage *self = (UtPngImage *)object;

  assert(width > 0);
  assert(height > 0);
  switch (colour_type) {
  case UT_PNG_COLOUR_TYPE_GREYSCALE:
    assert(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
           bit_depth == 8 || bit_depth == 16);
    break;
  case UT_PNG_COLOUR_TYPE_TRUECOLOUR:
    assert(bit_depth == 8 || bit_depth == 16);
    break;
  case UT_PNG_COLOUR_TYPE_INDEXED_COLOUR:
    assert(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
           bit_depth == 8);
    break;
  case UT_PNG_COLOUR_TYPE_GREYSCALE_WITH_ALPHA:
    assert(bit_depth == 8 || bit_depth == 16);
    break;
  case UT_PNG_COLOUR_TYPE_TRUECOLOUR_WITH_ALPHA:
    assert(bit_depth == 8 || bit_depth == 16);
    break;
  default:
    assert(false);
  }
  // FIXME: Assert data is correct length

  self->width = width;
  self->height = height;
  self->bit_depth = bit_depth;
  self->colour_type = colour_type;
  self->data = ut_object_ref(data);
  return object;
}

void ut_png_image_set_interlace_method(UtObject *object,
                                       UtPngInterlaceMethod method) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  self->interlace_method = method;
}

UtPngInterlaceMethod ut_png_image_get_interlace_method(UtObject *object) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  return self->interlace_method;
}

uint32_t ut_png_image_get_width(UtObject *object) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  return self->width;
}

uint32_t ut_png_image_get_height(UtObject *object) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  return self->height;
}

uint8_t ut_png_image_get_bit_depth(UtObject *object) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  return self->bit_depth;
}

UtPngColourType ut_png_image_get_colour_type(UtObject *object) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  return self->colour_type;
}

UtObject *ut_png_image_get_data(UtObject *object) {
  assert(ut_object_is_png_image(object));
  UtPngImage *self = (UtPngImage *)object;
  return self->data;
}

bool ut_object_is_png_image(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
