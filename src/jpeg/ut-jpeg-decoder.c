#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "ut-jpeg.h"
#include "ut.h"

// https://www.w3.org/Graphics/JPEG/itu-t81.pdf
// https://www.w3.org/Graphics/JPEG/jfif3.pdf

// Supports baseline process

typedef enum {
  DECODER_STATE_MARKER,
  DECODER_STATE_START_OF_IMAGE,
  DECODER_STATE_END_OF_IMAGE,
  DECODER_STATE_DEFINE_QUANTIZATION_TABLE,
  DECODER_STATE_START_OF_FRAME,
  DECODER_STATE_DEFINE_HUFFMAN_TABLE,
  DECODER_STATE_START_OF_SCAN,
  DECODER_STATE_DC_COEFFICIENT1,
  DECODER_STATE_DC_COEFFICIENT2,
  DECODER_STATE_AC_COEFFICIENT1,
  DECODER_STATE_AC_COEFFICIENT2,
  DECODER_STATE_APP0,
  DECODER_STATE_COMMENT,
  DECODER_STATE_DONE,
  DECODER_STATE_ERROR
} DecoderState;

typedef struct {
  // ID assigned to this component.
  uint8_t id;

  // Index in the image.
  uint8_t index;

  // Sampling factors.
  uint8_t horizontal_sampling_factor;
  uint8_t vertical_sampling_factor;

  // The quantization table used by this component.
  UtObject *quantization_table;

  // The Huffman decoders this component uses.
  UtObject *dc_decoder;
  UtObject *dc_table;
  UtObject *ac_decoder;
  UtObject *ac_table;

  // DC value from previous data unit.
  int16_t previous_dc;

  // Number of data units decoded in the current MCU.
  size_t data_unit_count;
} JpegComponent;

typedef struct {
  UtObject object;

  // Input stream being read.
  UtObject *input_stream;
  UtObject *read_cancel;

  // Callback to notify when complete.
  UtJpegDecodeCallback callback;
  void *user_data;
  UtObject *cancel;

  // Current bits being written.
  uint8_t bit_buffer;
  uint8_t bit_count;

  // Current state of the decoder.
  DecoderState state;

  // Tables for coefficient quantization values.
  UtObject *quantization_tables[4];

  // Components provided in image.
  JpegComponent components[4];

  // Width of an MCU in data units.
  size_t mcu_width;

  // Height of an MCU in data units.
  size_t mcu_height;

  // Huffman decoders for DC coefficients.
  UtObject *dc_decoders[2];

  // Maps from DC Huffman symbols to coefficent values.
  UtObject *dc_tables[2];

  // Huffman decoders for AC coefficients.
  UtObject *ac_decoders[2];

  // Maps from AC Huffman symbols to coefficent values.
  UtObject *ac_tables[2];

  // Order that data unit values are written.
  uint8_t data_unit_order[64];

  // Precalculated cosines for DCT.
  float dct_alpha[8];
  float dct_cos[64];

  // Last read Huffman code.
  uint16_t code;
  uint8_t code_width;

  // First byte of decoded coefficient.
  uint8_t coefficient_start;

  // Encoded data unit coefficients.
  int16_t encoded_data_unit[64];

  // Components in the current scan.
  JpegComponent *scan_components[4];

  // Current component scan is processing.
  size_t scan_component_index;

  // Number of coefficients in current data unit.
  size_t data_unit_coefficient_count;

  // Number of MCUs processed.
  size_t mcu_count;

  // Image thumbnail.
  uint16_t thumbnail_width;
  uint16_t thumbnail_height;
  UtObject *thumbnail_data;

  // Image comment.
  char *comment;

  // Final image object.
  UtObject *image;

  // Error that occurred during decoding.
  UtObject *error;
} UtJpegDecoder;

static void notify_complete(UtJpegDecoder *self) {
  ut_cancel_activate(self->read_cancel);
  self->callback(self->user_data);
}

static void set_error(UtJpegDecoder *self, const char *description) {
  if (self->state == DECODER_STATE_ERROR) {
    return;
  }

  ut_object_unref(self->image);
  self->image = NULL;
  self->error = ut_jpeg_error_new(description);
  self->state = DECODER_STATE_ERROR;
  notify_complete(self);
}

static void set_done(UtJpegDecoder *self) {
  self->state = DECODER_STATE_DONE;
  notify_complete(self);
}

// Get the component with the given ID.
static JpegComponent *find_component(UtJpegDecoder *self, uint8_t id) {
  size_t n_components = ut_jpeg_image_get_n_components(self->image);
  for (size_t i = 0; i < n_components; i++) {
    if (self->components[i].id == id) {
      return &self->components[i];
    }
  }

  return NULL;
}

// Read a the next scan byte from [data] and write it to [value].
static bool read_scan_byte(UtJpegDecoder *self, UtObject *data, size_t *offset,
                           uint8_t *value) {
  size_t data_length = ut_list_get_length(data);

  size_t o = *offset;
  if (o >= data_length) {
    return false;
  }

  uint8_t byte1 = ut_uint8_list_get_element(data, o++);

  // Scan data terminates on a marker. If 0xff is in the scan data, 0x00 is
  // after it so it can't be a valid marker. The 0x00 is dropped.
  if (byte1 == 0xff) {
    if (o >= data_length) {
      return false;
    }
    uint8_t byte2 = ut_uint8_list_get_element(data, o++);
    if (byte2 != 0x00) {
      self->state = DECODER_STATE_MARKER;
      return false;
    }
  }

  *offset = o;
  *value = byte1;
  return true;
}

// Read the next bit from [data] and write it to [value].
static bool read_scan_bit(UtJpegDecoder *self, UtObject *data, size_t *offset,
                          uint8_t *value) {
  if (self->bit_count == 0) {
    if (!read_scan_byte(self, data, offset, &self->bit_buffer)) {
      return false;
    }
    self->bit_count = 8;
  }

  *value = self->bit_buffer >> 7;
  self->bit_buffer <<= 1;
  self->bit_count--;

  return true;
}

// Read the next Huffman symbol from [data] using [decoder] and write it to
// [symbol].
static bool read_huffman_symbol(UtJpegDecoder *self, UtObject *data,
                                size_t *offset, UtObject *decoder,
                                uint16_t *symbol) {
  while (true) {
    uint8_t bit;
    if (!read_scan_bit(self, data, offset, &bit)) {
      return false;
    }
    self->code = self->code << 1 | bit;
    self->code_width++;
    if (ut_huffman_decoder_get_symbol(decoder, self->code, self->code_width,
                                      symbol)) {
      self->code = 0;
      self->code_width = 0;
      return true;
    }
  }
}

// Read an integer of [length] bits from [data].
static bool read_int(UtJpegDecoder *self, UtObject *data, size_t *offset,
                     size_t length, uint16_t *value) {
  while (self->code_width < length) {
    uint8_t bit;
    if (!read_scan_bit(self, data, offset, &bit)) {
      return false;
    }
    // FIXME: Use bit_buffer instead of self->code
    self->code = self->code << 1 | bit;
    self->code_width++;
  }

  *value = self->code;
  self->code = 0;
  self->code_width = 0;

  return true;
}

static uint8_t clamp(float value) {
  if (value < 0.0) {
    return 0;
  } else if (value > 255.0) {
    return 255;
  } else {
    return floor(value);
  }
}

static void ycbcr_to_rgb(uint8_t *pixel) {
  float Y = pixel[0];
  float Cb = pixel[1] - 128.0;
  float Cr = pixel[2] - 128.0;

  pixel[0] = clamp(Y + 1.402 * Cr);
  pixel[1] = clamp(Y - 0.344136 * Cb - 0.714136 * Cr);
  pixel[2] = clamp(Y + 1.772 * Cb);
}

// Process a received data unit.
static void process_data_unit(UtJpegDecoder *self) {
  uint16_t image_width = ut_jpeg_image_get_width(self->image);
  uint16_t image_height = ut_jpeg_image_get_height(self->image);
  size_t n_components = ut_jpeg_image_get_n_components(self->image);
  size_t row_stride = image_width * n_components;
  uint8_t *image_data =
      ut_uint8_list_get_writable_data(ut_jpeg_image_get_data(self->image));

  // Get position of current MCU in image.
  size_t width_in_mcus =
      (image_width + (self->mcu_width * 8) - 1) / (self->mcu_width * 8);
  size_t mcu_x = (self->mcu_count % width_in_mcus) * self->mcu_width * 8;
  size_t mcu_y = (self->mcu_count / width_in_mcus) * self->mcu_height * 8;

  // Get position of current data unit in image.
  JpegComponent *component = self->scan_components[self->scan_component_index];
  size_t data_unit_x =
      mcu_x +
      (component->data_unit_count % component->horizontal_sampling_factor) * 8;
  size_t data_unit_y =
      mcu_y +
      (component->data_unit_count / component->horizontal_sampling_factor) * 8;

  // Do inverse DCT on data unit.
  int16_t decoded_data_unit[64];
  jpeg_inverse_dct(self->dct_alpha, self->dct_cos, self->encoded_data_unit,
                   decoded_data_unit);

  // Check if this is the final component being written.
  bool last_component = self->scan_component_index == n_components - 1;

  // Write values into image.
  size_t sample_width = self->mcu_width / component->horizontal_sampling_factor;
  size_t sample_height = self->mcu_height / component->vertical_sampling_factor;
  for (size_t y = 0; y < 8; y++) {
    size_t sy = data_unit_y + y * sample_height;
    for (size_t x = 0; x < 8; x++) {
      size_t sx = data_unit_x + x * sample_width;

      int16_t sample = decoded_data_unit[(y * 8) + x] + 128;
      if (sample < 0) {
        sample = 0;
      } else if (sample > 255) {
        sample = 255;
      }

      for (size_t v = 0; v < sample_height; v++) {
        size_t py = sy + v;
        if (py >= image_height) {
          break;
        }
        for (size_t u = 0; u < sample_width; u++) {
          size_t px = sx + u;
          if (px >= image_width) {
            break;
          }

          size_t pixel_index = (py * row_stride) + (px * n_components);
          image_data[pixel_index + component->index] = sample;
          // Last channel placed - do color conversion.
          if (last_component && n_components == 3) {
            ycbcr_to_rgb(image_data + pixel_index);
          }
        }
      }
    }
  }

  component->data_unit_count++;

  // All data units for this component in this MCU complete, move to the next
  // component.
  if (component->data_unit_count >= component->horizontal_sampling_factor *
                                        component->vertical_sampling_factor) {
    component->data_unit_count = 0;
    self->scan_component_index++;

    // All components in the MCU complete, move to the next MCU.
    if (self->scan_component_index >= n_components) {
      self->scan_component_index = 0;
      self->mcu_count++;
    }
  }

  self->state = DECODER_STATE_DC_COEFFICIENT1;
}

// Add a coefficient [value] to the current data unit. Add [run_length] zeros
// before this coefficient.
static void add_coefficient(UtJpegDecoder *self, size_t run_length,
                            int16_t value) {
  JpegComponent *component = self->scan_components[self->scan_component_index];
  const uint8_t *quantization_table_data =
      ut_uint8_list_get_data(component->quantization_table);

  if (self->data_unit_coefficient_count + run_length + 1 > 64) {
    set_error(self, "Too many coefficients in data unit");
    return;
  }

  // Pad with zeros.
  for (size_t i = 0; i < run_length; i++) {
    uint8_t index = self->data_unit_order[self->data_unit_coefficient_count];
    self->encoded_data_unit[index] = 0;
    self->data_unit_coefficient_count++;
  }

  // Put cofficient into data unit in zig-zag order.
  uint8_t index = self->data_unit_order[self->data_unit_coefficient_count];
  self->encoded_data_unit[index] = value * quantization_table_data[index];
  self->data_unit_coefficient_count++;

  if (self->data_unit_coefficient_count < 64) {
    self->state = DECODER_STATE_AC_COEFFICIENT1;
  } else {
    process_data_unit(self);
    self->data_unit_coefficient_count = 0;
  }
}

// Read an amplitude of [length] bits.
static bool read_amplitude(UtJpegDecoder *self, UtObject *data, size_t *offset,
                           size_t length, int16_t *amplitude) {
  if (length == 0) {
    *amplitude = 0;
    return true;
  }

  uint16_t value;
  if (!read_int(self, data, offset, length, &value)) {
    return false;
  }

  // Upper half of values are positive, lower half are negative, i.e.
  // 0 bits:  0
  // 1 bit:  -1, 1
  // 2 bits: -3,-2, 2, 3
  // 3 bits: -7,-6,-5,-4, 4, 5, 6, 7
  // ...
  int16_t min_amplitude = 1 << (length - 1);
  if (value >= min_amplitude) {
    *amplitude = value;
  } else {
    *amplitude = value - (min_amplitude * 2) + 1;
  }

  return true;
}

static size_t decode_start_of_image(UtJpegDecoder *self, UtObject *data) {
  self->state = DECODER_STATE_MARKER;
  return 0;
}

static size_t decode_end_of_image(UtJpegDecoder *self, UtObject *data) {
  set_done(self);
  return 0;
}

static size_t decode_app0(UtJpegDecoder *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  if (data_length < 2) {
    return 0;
  }
  uint16_t length = ut_uint8_list_get_uint16_be(data, 0);
  if (data_length < length) {
    return 0;
  }

  if (length < 16) {
    set_error(self, "Insufficient data for JPEG app0 block header");
    return length;
  }

  // FIXME: Also support 'JFXX'
  if (ut_uint8_list_get_element(data, 2) != 'J' ||
      ut_uint8_list_get_element(data, 3) != 'F' ||
      ut_uint8_list_get_element(data, 4) != 'I' ||
      ut_uint8_list_get_element(data, 5) != 'F' ||
      ut_uint8_list_get_element(data, 6) != '\0') {
    set_error(self, "Invalid signature for JPEG app0 block");
    return length;
  }

  uint8_t jpeg_version_major = ut_uint8_list_get_element(data, 7);
  uint8_t jpeg_version_minor = ut_uint8_list_get_element(data, 8);
  if (jpeg_version_major != 1) {
    ut_cstring_ref description =
        ut_cstring_new_printf("Unsupported JPEG version %d.%d",
                              jpeg_version_major, jpeg_version_minor);
    set_error(self, description);
    return length;
  }

  /*uint8_t density_units =*/ut_uint8_list_get_element(data, 9);
  /*uint16_t x_density=*/ut_uint8_list_get_uint16_be(data, 10);
  /*uint16_t x_density=*/ut_uint8_list_get_uint16_be(data, 12);
  self->thumbnail_width = ut_uint8_list_get_element(data, 14);
  self->thumbnail_height = ut_uint8_list_get_element(data, 15);
  size_t thumbnail_size = self->thumbnail_width * self->thumbnail_height * 3;
  if (length < 16 + thumbnail_size) {
    set_error(self, "Insufficient space for JPEG thumbnail");
    return length;
  }
  ut_object_unref(self->thumbnail_data);
  self->thumbnail_data = thumbnail_size > 0 ? ut_uint8_array_new() : NULL;
  for (size_t i = 0; i < thumbnail_size; i++) {
    ut_uint8_list_append(self->thumbnail_data,
                         ut_uint8_list_get_element(data, 16 + i));
  }

  self->state = DECODER_STATE_MARKER;

  return length;
}

static size_t decode_define_quantization_table(UtJpegDecoder *self,
                                               UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  size_t offset = 0;
  if (data_length < 2) {
    return 0;
  }
  uint16_t length = ut_uint8_list_get_uint16_be(data, offset);
  offset += 2;
  if (data_length < length) {
    return 0;
  }

  while (offset < length) {
    if (length < offset + 65) {
      set_error(self, "Invalid JPEG quantization table size");
      return length;
    }

    uint8_t precision_and_destination =
        ut_uint8_list_get_element(data, offset++);
    uint8_t precision = precision_and_destination >> 4;
    uint8_t destination = precision_and_destination & 0xf;

    if (precision != 0) {
      set_error(self, "Invalid JPEG quantization table precision");
      return length;
    }
    if (destination > 3) {
      set_error(self, "Invalid JPEG quantization table destination");
      return length;
    }

    // Table is in same zig-zag order as the data units.
    ut_object_unref(self->quantization_tables[destination]);
    self->quantization_tables[destination] = ut_uint8_array_new_sized(64);
    uint8_t *quantization_table_data =
        ut_uint8_list_get_writable_data(self->quantization_tables[destination]);
    for (size_t i = 0; i < 64; i++) {
      quantization_table_data[self->data_unit_order[i]] =
          ut_uint8_list_get_element(data, offset++);
    }
  }

  self->state = DECODER_STATE_MARKER;

  return length;
}

static size_t decode_start_of_frame(UtJpegDecoder *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  if (data_length < 2) {
    return 0;
  }
  uint16_t length = ut_uint8_list_get_uint16_be(data, 0);
  if (data_length < length) {
    return 0;
  }

  if (length < 8) {
    set_error(self, "Insufficient space for JPEG start of frame");
    return length;
  }
  uint8_t precision = ut_uint8_list_get_element(data, 2);
  uint16_t height = ut_uint8_list_get_uint16_be(data, 3);
  uint16_t width = ut_uint8_list_get_uint16_be(data, 5);
  uint8_t n_components = ut_uint8_list_get_element(data, 7);

  // FIXME: Support height 0 - height determined by amount of data
  if (width == 0 || height == 0) {
    set_error(self, "Invalid JPEG width");
    return length;
  }

  if (n_components < 1 || n_components > 4) {
    ut_cstring_ref description = ut_cstring_new_printf(
        "Unsupported number of JPEG components %d", n_components);
    set_error(self, description);
    return length;
  }

  if (length < 8 + 3 * n_components) {
    set_error(self, "Insufficient space for JPEG start of frame");
    return length;
  }
  size_t offset = 8;
  uint8_t mcu_width = 0;
  uint8_t mcu_height = 0;
  for (size_t i = 0; i < n_components; i++) {
    uint8_t id = ut_uint8_list_get_element(data, offset++);
    uint8_t sampling_factor = ut_uint8_list_get_element(data, offset++);
    uint8_t horizontal_sampling_factor = sampling_factor >> 4;
    uint8_t vertical_sampling_factor = sampling_factor & 0xf;
    uint8_t quantization_table_selector =
        ut_uint8_list_get_element(data, offset++);

    for (size_t j = 0; j < i; j++) {
      if (self->components[j].id == id) {
        set_error(self, "Duplicate JPEG component ID");
        return length;
      }
    }
    if (horizontal_sampling_factor < 1 || horizontal_sampling_factor > 4 ||
        vertical_sampling_factor < 1 || vertical_sampling_factor > 4) {
      ut_cstring_ref description = ut_cstring_new_printf(
          "Invalid JPEG sampling factor %dx%d for component %zi",
          horizontal_sampling_factor, vertical_sampling_factor, i);
      set_error(self, description);
      return length;
    }
    if (quantization_table_selector > 3) {
      set_error(self, "Invalid JPEG component quantization table");
      return length;
    }
    UtObject *quantization_table =
        self->quantization_tables[quantization_table_selector];
    if (quantization_table == NULL) {
      set_error(self, "Missing JPEG quantization table");
      return length;
    }

    if (horizontal_sampling_factor == 3 || vertical_sampling_factor == 3) {
      ut_cstring_ref description = ut_cstring_new_printf(
          "Unsupported sampling factor %dx%d for component %zi",
          horizontal_sampling_factor, vertical_sampling_factor, i);
      set_error(self, description);
      return length;
    }
    if (horizontal_sampling_factor > mcu_width) {
      mcu_width = horizontal_sampling_factor;
    }
    if (vertical_sampling_factor > mcu_height) {
      mcu_height = vertical_sampling_factor;
    }

    self->components[i].id = id;
    self->components[i].index = i;
    self->components[i].horizontal_sampling_factor = horizontal_sampling_factor;
    self->components[i].vertical_sampling_factor = vertical_sampling_factor;
    self->components[i].quantization_table = quantization_table;
  }
  self->mcu_width = mcu_width;
  self->mcu_height = mcu_height;

  if (precision != 8) {
    ut_cstring_ref description =
        ut_cstring_new_printf("Unsupported JPEG precision %d", precision);
    set_error(self, description);
    return length;
  }

  UtObjectRef image_data =
      ut_uint8_array_new_sized(height * width * n_components);
  self->image = ut_jpeg_image_new(width, height, n_components, image_data);

  self->state = DECODER_STATE_MARKER;

  return length;
}

static size_t decode_define_huffman_table(UtJpegDecoder *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  size_t offset = 0;
  if (data_length < 2) {
    return 0;
  }
  uint16_t length = ut_uint8_list_get_uint16_be(data, offset);
  offset += 2;
  if (data_length < length) {
    return 0;
  }

  while (offset < length) {
    if (length < offset + 17) {
      set_error(self, "Insufficient space for JPEG Huffman table header");
      return length;
    }

    uint8_t class_and_destination = ut_uint8_list_get_element(data, offset++);

    // Huffman code is defined by the number of codes of with 1-16.
    UtObjectRef code_widths = ut_uint8_list_new();
    size_t last_code_width = 0;
    for (size_t code_width = 1; code_width <= 16; code_width++) {
      size_t count = ut_uint8_list_get_element(data, offset++);
      if (count > 0) {
        for (size_t j = 0; j < count; j++) {
          ut_uint8_list_append(code_widths, code_width);
        }
        last_code_width = code_width;
      }
    }
    size_t symbols_length = ut_list_get_length(code_widths);

    // The final Huffman code (all ones) is reserved and not given in the above
    // table.
    ut_uint8_list_append(code_widths, last_code_width);

    uint8_t class = class_and_destination >> 4;
    uint8_t destination = class_and_destination & 0xf;

    if (class > 1) {
      set_error(self, "Unsupported JPEG Huffman table class");
      return offset;
    }
    if (destination > 1) {
      set_error(self, "Unsupported JPEG Huffman table destination");
      return offset;
    }

    if (length < offset + symbols_length) {
      set_error(self, "Insufficient space for JPEG Huffman table data");
      return offset;
    }
    UtObjectRef symbols = ut_list_get_sublist(data, offset, symbols_length);
    offset += symbols_length;

    UtObjectRef decoder = ut_huffman_decoder_new_canonical(code_widths);
    if (ut_object_implements_error(decoder)) {
      ut_cstring_ref description = ut_cstring_new_printf(
          "Invalid JPEG Huffman table: %s", ut_error_get_description(decoder));
      set_error(self, description);
      return offset;
    }
    if (class == 0) {
      ut_object_unref(self->dc_decoders[destination]);
      self->dc_decoders[destination] = ut_object_ref(decoder);
      ut_object_unref(self->dc_tables[destination]);
      self->dc_tables[destination] = ut_list_copy(symbols);
    } else {
      ut_object_unref(self->ac_decoders[destination]);
      self->ac_decoders[destination] = ut_object_ref(decoder);
      ut_object_unref(self->ac_tables[destination]);
      self->ac_tables[destination] = ut_list_copy(symbols);
    }
  }

  self->state = DECODER_STATE_MARKER;

  return length;
}

static size_t decode_start_of_scan(UtJpegDecoder *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  if (data_length < 3) {
    return 0;
  }
  size_t offset = 0;
  uint16_t length = ut_uint8_list_get_uint16_be(data, offset);
  offset += 2;
  if (data_length < length) {
    return 0;
  }

  uint8_t n_scan_components = ut_uint8_list_get_element(data, offset++);
  if (length != 6 + n_scan_components * 2) {
    set_error(self, "Insufficient data for JPEG start of scan");
    return length;
  }
  if (n_scan_components != ut_jpeg_image_get_n_components(self->image)) {
    set_error(self,
              "Mismatched number of scan components in JPEG start of scan");
    return length;
  }

  for (size_t i = 0; i < n_scan_components; i++) {
    uint8_t component_selector = ut_uint8_list_get_element(data, offset++);
    uint8_t table_selectors = ut_uint8_list_get_element(data, offset++);
    uint8_t dc_table = table_selectors >> 4;
    uint8_t ac_table = table_selectors & 0xf;

    JpegComponent *component = find_component(self, component_selector);
    if (component == NULL) {
      set_error(self, "Invalid component selector in JPEG start of scan");
      return length;
    }
    self->scan_components[i] = component;

    if (dc_table > 3) {
      set_error(self, "Invalid DC table selector in JPEG start of scan");
      return length;
    }
    if (ac_table > 3) {
      set_error(self, "Invalid AC table selector in JPEG start of scan");
      return length;
    }
    component->dc_decoder = self->dc_decoders[dc_table];
    component->dc_table = self->dc_tables[dc_table];
    if (component->dc_decoder == NULL) {
      set_error(self, "Missing DC table in JPEG start of scan");
      return length;
    }
    component->ac_decoder = self->ac_decoders[ac_table];
    component->ac_table = self->ac_tables[ac_table];
    if (component->ac_decoder == NULL) {
      set_error(self, "Missing AC table in JPEG start of scan");
      return length;
    }
  }
  uint8_t selection_start = ut_uint8_list_get_element(data, offset++);
  uint8_t selection_end = ut_uint8_list_get_element(data, offset++);
  uint8_t successive_approximation = ut_uint8_list_get_element(data, offset++);

  if (selection_start != 0 || selection_end != 63) {
    set_error(self, "Invalid scan selection range in JPEG start of scan");
    return length;
  }
  if (successive_approximation != 0) {
    set_error(self, "Invalid successive approximation in JPEG start of scan");
    return length;
  }

  self->data_unit_coefficient_count = 0;
  self->mcu_count = 0;
  self->scan_component_index = 0;
  for (size_t i = 0; i < n_scan_components; i++) {
    self->components[i].previous_dc = 0;
    self->components[i].data_unit_count = 0;
  }
  self->state = DECODER_STATE_DC_COEFFICIENT1;

  return offset;
}

static size_t decode_dc_coefficient1(UtJpegDecoder *self, UtObject *data,
                                     bool complete) {
  JpegComponent *component = self->scan_components[self->scan_component_index];

  size_t offset = 0;
  uint16_t symbol;
  if (!read_huffman_symbol(self, data, &offset, component->dc_decoder,
                           &symbol)) {
    return offset;
  }

  self->coefficient_start =
      ut_uint8_list_get_element(component->dc_table, symbol);

  self->state = DECODER_STATE_DC_COEFFICIENT2;

  return offset;
}

static size_t decode_dc_coefficient2(UtJpegDecoder *self, UtObject *data,
                                     bool complete) {
  JpegComponent *component = self->scan_components[self->scan_component_index];

  size_t offset = 0;
  int16_t diff = 0;
  size_t coefficient_length = self->coefficient_start;
  if (!read_amplitude(self, data, &offset, coefficient_length, &diff)) {
    return false;
  }

  int16_t dc = component->previous_dc + diff;
  component->previous_dc = dc;
  add_coefficient(self, 0, dc);

  return offset;
}

static size_t decode_ac_coefficient1(UtJpegDecoder *self, UtObject *data,
                                     bool complete) {
  JpegComponent *component = self->scan_components[self->scan_component_index];

  size_t offset = 0;
  uint16_t symbol;
  if (!read_huffman_symbol(self, data, &offset, component->ac_decoder,
                           &symbol)) {
    return offset;
  }

  self->coefficient_start =
      ut_uint8_list_get_element(component->ac_table, symbol);

  size_t coefficient_length = self->coefficient_start & 0xf;
  if (coefficient_length == 0) {
    size_t run_length = self->coefficient_start >> 4;

    // Special cases of fill to end of data unit, and fill with 16 zeros
    if (run_length == 0) {
      add_coefficient(self, 64 - self->data_unit_coefficient_count - 1, 0);
    } else if (run_length == 15) {
      add_coefficient(self, 15, 0);
    } else {
      // FIXME: undefined
      assert(false);
    }
  } else {
    self->state = DECODER_STATE_AC_COEFFICIENT2;
  }

  return offset;
}

static size_t decode_ac_coefficient2(UtJpegDecoder *self, UtObject *data,
                                     bool complete) {
  size_t offset = 0;
  size_t run_length = self->coefficient_start >> 4;
  size_t coefficient_length = self->coefficient_start & 0xf;
  int16_t ac;
  if (!read_amplitude(self, data, &offset, coefficient_length, &ac)) {
    return false;
  }
  add_coefficient(self, run_length, ac);

  return offset;
}

static size_t decode_comment(UtJpegDecoder *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  if (data_length < 2) {
    return 0;
  }
  uint16_t length = ut_uint8_list_get_uint16_be(data, 0);
  if (data_length < length) {
    return 0;
  }

  UtObjectRef comment_data = ut_uint8_array_new();
  for (size_t i = 2; i < length; i++) {
    ut_uint8_list_append(comment_data, ut_uint8_list_get_element(data, i));
  }
  ut_uint8_list_append(comment_data, '\0');
  free(self->comment);
  self->comment = (char *)ut_uint8_list_take_data(comment_data);

  self->state = DECODER_STATE_MARKER;

  return length;
}

static size_t decode_marker(UtJpegDecoder *self, UtObject *data) {
  size_t data_length = ut_list_get_length(data);

  if (data_length < 2) {
    return 0;
  }
  uint8_t marker_signature = ut_uint8_list_get_element(data, 0);
  uint8_t marker_id = ut_uint8_list_get_element(data, 1);
  if (marker_signature != 0xff) {
    set_error(self, "Invalid JPEG marker signature");
    return 0;
  }

  switch (marker_id) {
  case 0xd8:
    self->state = DECODER_STATE_START_OF_IMAGE;
    break;
  case 0xd9:
    self->state = DECODER_STATE_END_OF_IMAGE;
    break;
  case 0xdb:
    self->state = DECODER_STATE_DEFINE_QUANTIZATION_TABLE;
    break;
  case 0xc0:
    self->state = DECODER_STATE_START_OF_FRAME;
    break;
  case 0xc1:
    set_error(self, "Extended sequential DCT JPEG not supported");
    break;
  case 0xc2:
    set_error(self, "Progressive DCT JPEG not supported");
    break;
  case 0xc3:
    set_error(self, "Lossless JPEG not supported");
    break;
  case 0xc4:
    self->state = DECODER_STATE_DEFINE_HUFFMAN_TABLE;
    break;
  case 0xc9:
  case 0xca:
  case 0xcb:
  case 0xcc:
  case 0xcd:
  case 0xce:
  case 0xcf:
    set_error(self, "Arithmentic JPEG not supported");
    break;
  case 0xda:
    self->state = DECODER_STATE_START_OF_SCAN;
    break;
  case 0xe0:
    self->state = DECODER_STATE_APP0;
    break;
  case 0xfe:
    self->state = DECODER_STATE_COMMENT;
    break;
  default:
    ut_cstring_ref description =
        ut_cstring_new_printf("Unknown JPEG marker %02x", marker_id);
    set_error(self, description);
    break;
  }

  return 2;
}

static size_t read_cb(void *user_data, UtObject *data, bool complete) {
  UtJpegDecoder *self = user_data;

  if (ut_cancel_is_active(self->cancel)) {
    ut_cancel_activate(self->read_cancel);
    return 0;
  }

  if (ut_object_implements_error(data)) {
    ut_cstring_ref description = ut_cstring_new_printf(
        "Failed to read JPEG data: %s", ut_error_get_description(data));
    set_error(self, description);
    return 0;
  }

  size_t data_length = ut_list_get_length(data);
  size_t offset = 0;
  while (true) {
    size_t n_used;
    UtObjectRef d = ut_list_get_sublist(data, offset, data_length - offset);
    DecoderState old_state = self->state;
    switch (self->state) {
    case DECODER_STATE_MARKER:
      n_used = decode_marker(self, d);
      break;
    case DECODER_STATE_START_OF_IMAGE:
      n_used = decode_start_of_image(self, d);
      break;
    case DECODER_STATE_END_OF_IMAGE:
      n_used = decode_end_of_image(self, d);
      break;
    case DECODER_STATE_DEFINE_QUANTIZATION_TABLE:
      n_used = decode_define_quantization_table(self, d);
      break;
    case DECODER_STATE_START_OF_FRAME:
      n_used = decode_start_of_frame(self, d);
      break;
    case DECODER_STATE_DEFINE_HUFFMAN_TABLE:
      n_used = decode_define_huffman_table(self, d);
      break;
    case DECODER_STATE_START_OF_SCAN:
      n_used = decode_start_of_scan(self, d);
      break;
    case DECODER_STATE_DC_COEFFICIENT1:
      n_used = decode_dc_coefficient1(self, d, complete);
      break;
    case DECODER_STATE_DC_COEFFICIENT2:
      n_used = decode_dc_coefficient2(self, d, complete);
      break;
    case DECODER_STATE_AC_COEFFICIENT1:
      n_used = decode_ac_coefficient1(self, d, complete);
      break;
    case DECODER_STATE_AC_COEFFICIENT2:
      n_used = decode_ac_coefficient2(self, d, complete);
      break;
    case DECODER_STATE_APP0:
      n_used = decode_app0(self, d);
      break;
    case DECODER_STATE_COMMENT:
      n_used = decode_comment(self, d);
      break;
    case DECODER_STATE_ERROR:
    case DECODER_STATE_DONE:
      return offset;
    default:
      assert(false);
    }

    offset += n_used;
    if (self->state == old_state && n_used == 0) {
      if (complete && self->state != DECODER_STATE_DONE) {
        set_error(self, "Incomplete JPEG");
      }
      return offset;
    }
  }
}

static void done_cb(void *user_data) {}

static void ut_jpeg_decoder_init(UtObject *object) {
  UtJpegDecoder *self = (UtJpegDecoder *)object;
  self->read_cancel = ut_cancel_new();
  jpeg_build_data_unit_order(self->data_unit_order);
  jpeg_build_dct_values(self->dct_alpha, self->dct_cos);
}

static void ut_jpeg_decoder_cleanup(UtObject *object) {
  UtJpegDecoder *self = (UtJpegDecoder *)object;
  ut_cancel_activate(self->read_cancel);
  ut_object_unref(self->input_stream);
  ut_object_unref(self->read_cancel);
  ut_object_unref(self->cancel);
  for (size_t i = 0; i < 4; i++) {
    ut_object_unref(self->quantization_tables[i]);
  }
  for (size_t i = 0; i < 2; i++) {
    ut_object_unref(self->dc_decoders[i]);
    ut_object_unref(self->dc_tables[i]);
    ut_object_unref(self->ac_decoders[i]);
    ut_object_unref(self->ac_tables[i]);
  }
  free(self->comment);
  ut_object_unref(self->image);
  ut_object_unref(self->error);
}

static UtObjectInterface object_interface = {.type_name = "UtJpegDecoder",
                                             .init = ut_jpeg_decoder_init,
                                             .cleanup = ut_jpeg_decoder_cleanup,
                                             .interfaces = {{NULL, NULL}}};

UtObject *ut_jpeg_decoder_new(UtObject *input_stream) {
  UtObject *object = ut_object_new(sizeof(UtJpegDecoder), &object_interface);
  UtJpegDecoder *self = (UtJpegDecoder *)object;
  self->input_stream = ut_object_ref(input_stream);
  return object;
}

void ut_jpeg_decoder_decode(UtObject *object, UtJpegDecodeCallback callback,
                            void *user_data, UtObject *cancel) {
  assert(ut_object_is_jpeg_decoder(object));
  UtJpegDecoder *self = (UtJpegDecoder *)object;

  assert(self->callback == NULL);
  assert(callback != NULL);

  self->callback = callback;
  self->user_data = user_data;
  self->cancel = ut_object_ref(cancel);

  ut_input_stream_read(self->input_stream, read_cb, self, self->read_cancel);
}

UtObject *ut_jpeg_decoder_decode_sync(UtObject *object) {
  assert(ut_object_is_jpeg_decoder(object));
  UtJpegDecoder *self = (UtJpegDecoder *)object;

  ut_jpeg_decoder_decode(object, done_cb, NULL, NULL);
  if (self->error != NULL) {
    return ut_object_ref(self->error);
  }

  return ut_object_ref(self->image);
}

UtObject *ut_jpeg_decoder_get_error(UtObject *object) {
  assert(ut_object_is_jpeg_decoder(object));
  UtJpegDecoder *self = (UtJpegDecoder *)object;
  return self->error;
}

UtObject *ut_jpeg_decoder_get_image(UtObject *object) {
  assert(ut_object_is_jpeg_decoder(object));
  UtJpegDecoder *self = (UtJpegDecoder *)object;
  return self->image;
}

bool ut_object_is_jpeg_decoder(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
