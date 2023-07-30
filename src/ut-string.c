#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ut-string-private.h"
#include "ut.h"

int ut_string_id = 0;

UtObject *ut_string_new(const char *text) { return ut_utf8_string_new(text); }

UtObject *ut_string_new_printf(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  ut_cstring_ref text = ut_cstring_new_vprintf(format, ap);
  va_end(ap);
  return ut_string_new(text);
}

UtObject *ut_string_new_constant(const char *text) {
  return ut_constant_utf8_string_new(text);
}

UtObject *ut_string_new_sized(const char *text, size_t length) {
  return ut_utf8_string_new_sized(text, length);
}

UtObject *ut_string_new_from_utf8(UtObject *utf8) {
  return ut_utf8_string_new_from_data(utf8);
}

UtObject *ut_string_new_from_utf16(UtObject *code_units) {
  UtObject *string = ut_string_new("");
  size_t code_units_length = ut_list_get_length(code_units);
  for (size_t i = 0; i < code_units_length; i++) {
    uint16_t code_unit = ut_uint16_list_get_element(code_units, i);
    uint32_t code_point;
    if (code_unit <= 0xd7ff || code_unit >= 0xe000) {
      code_point = code_unit;
    } else if ((code_unit & 0xfc00) == 0xd800) {
      if (i + 1 >= code_units_length) {
        code_point = 0xfffd;
      } else {
        i++;
        uint16_t code_unit2 = ut_uint16_list_get_element(code_units, i);
        if ((code_unit2 & 0xfc00) == 0xdc00) {
          code_point =
              0x10000 + ((code_unit & 0x3ff) << 10 | (code_unit2 & 0x3ff));
        } else {
          code_point = 0xfffd;
        }
      }
    } else {
      code_point = 0xfffd;
    }
    ut_string_append_code_point(string, code_point);
  }
  return string;
}

UtObject *ut_string_new_from_ascii(UtObject *characters) {
  UtObject *string = ut_string_new("");
  size_t characters_length = ut_list_get_length(characters);
  for (size_t i = 0; i < characters_length; i++) {
    uint8_t character = ut_uint8_list_get_element(characters, i);
    if (character <= 127) {
      ut_string_append_code_point(string, character);
    } else {
      ut_string_append_code_point(string, 0xfffd);
    }
  }
  return string;
}

UtObject *ut_string_new_from_iso_8859_1(UtObject *characters) {
  UtObject *string = ut_string_new("");
  size_t characters_length = ut_list_get_length(characters);
  for (size_t i = 0; i < characters_length; i++) {
    uint8_t character = ut_uint8_list_get_element(characters, i);
    ut_string_append_code_point(string, character);
  }
  return string;
}

const char *ut_string_get_text(UtObject *object) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  return string_interface->get_text(object);
}

char *ut_string_take_text(UtObject *object) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  if (string_interface->take_text != NULL) {
    return string_interface->take_text(object);
  }

  return ut_cstring_new(ut_string_get_text(object));
}

UtObject *ut_string_get_code_points(UtObject *object) {
  const char *text = ut_string_get_text(object);
  size_t text_length = ut_cstring_get_length(text);
  UtObjectRef code_points = ut_uint32_list_new();
  size_t offset = 0;
  while (offset < text_length) {
    uint8_t byte1 = text[offset];
    if ((byte1 & 0x80) == 0) {
      ut_uint32_list_append(code_points, byte1);
      offset++;
    } else if ((byte1 & 0xe0) == 0xc0) {
      if (text_length - offset < 2) {
        return ut_general_error_new("Invalid UTF-8");
      }
      uint8_t byte2 = text[offset + 1];
      if ((byte2 & 0xc0) != 0x80) {
        return ut_general_error_new("Invalid UTF-8");
      }
      ut_uint32_list_append(code_points, (byte1 & 0x1f) << 6 | (byte2 & 0x3f));
      offset += 2;
    } else if ((byte1 & 0xf0) == 0xe0) {
      if (text_length - offset < 3) {
        return ut_general_error_new("Invalid UTF-8");
      }
      uint8_t byte2 = text[offset + 1];
      uint8_t byte3 = text[offset + 2];
      if ((byte2 & 0xc0) != 0x80 || (byte3 & 0xc0) != 0x80) {
        return ut_general_error_new("Invalid UTF-8");
      }
      ut_uint32_list_append(code_points, (byte1 & 0x0f) << 12 |
                                             (byte2 & 0x3f) << 6 |
                                             (byte3 & 0x3f));
      offset += 3;
    } else if ((byte1 & 0xf8) == 0xf0) {
      if (text_length - offset < 4) {
        return ut_general_error_new("Invalid UTF-8");
      }
      uint8_t byte2 = text[offset + 1];
      uint8_t byte3 = text[offset + 2];
      uint8_t byte4 = text[offset + 3];
      if ((byte2 & 0xc0) != 0x80 || (byte3 & 0xc0) != 0x80 ||
          (byte4 & 0xc0) != 0x80) {
        return ut_general_error_new("Invalid UTF-8");
      }
      ut_uint32_list_append(code_points,
                            (byte1 & 0x07) << 18 | (byte2 & 0x3f) << 12 |
                                (byte3 & 0x3f) << 6 | (byte4 & 0x3f));
      offset += 4;
    } else {
      return ut_general_error_new("Invalid UTF-8");
    }
  }

  return ut_object_ref(code_points);
}

UtObject *ut_string_get_utf8(UtObject *object) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  return string_interface->get_utf8(object);
}

UtObject *ut_string_get_utf16(UtObject *object) {
  UtObjectRef code_points = ut_string_get_code_points(object);
  UtObjectRef utf16 = ut_uint16_list_new();
  size_t code_points_length = ut_list_get_length(code_points);
  for (size_t i = 0; i < code_points_length; i++) {
    uint32_t code_point = ut_uint32_list_get_element(code_points, i);
    if (code_point <= 0xffff) {
      if (code_point >= 0xd800 && code_point <= 0xdfff) {
        return ut_general_error_new("Invalid code points");
      }
      ut_uint16_list_append(utf16, code_point);
    } else if (code_point <= 0x10ffff) {
      uint32_t u = code_point - 0x10000;
      ut_uint16_list_append(utf16, 0xd800 + (u >> 10));
      ut_uint16_list_append(utf16, 0xdc00 + (u & 0x3ff));
    } else {
      return ut_general_error_new("Invalid code points");
    }
  }

  return ut_object_ref(utf16);
}

UtObject *ut_string_get_ascii(UtObject *object) {
  UtObjectRef code_points = ut_string_get_code_points(object);
  UtObjectRef characters = ut_uint8_list_new();
  size_t code_points_length = ut_list_get_length(code_points);
  for (size_t i = 0; i < code_points_length; i++) {
    uint32_t code_point = ut_uint32_list_get_element(code_points, i);
    if (code_point <= 127) {
      ut_uint8_list_append(characters, code_point);
    } else {
      return ut_general_error_new("Invalid code points");
    }
  }

  return ut_object_ref(characters);
}

UtObject *ut_string_get_iso_8859_1(UtObject *object) {
  UtObjectRef code_points = ut_string_get_code_points(object);
  UtObjectRef characters = ut_uint8_list_new();
  size_t code_points_length = ut_list_get_length(code_points);
  for (size_t i = 0; i < code_points_length; i++) {
    uint32_t code_point = ut_uint32_list_get_element(code_points, i);
    if (code_point <= 0xff) {
      ut_uint8_list_append(characters, code_point);
    } else {
      return ut_general_error_new("Invalid code points");
    }
  }

  return ut_object_ref(characters);
}

bool ut_string_is_mutable(UtObject *object) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  return string_interface->is_mutable;
}

void ut_string_clear(UtObject *object) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  assert(string_interface->is_mutable);
  string_interface->clear(object);
}

void ut_string_prepend(UtObject *object, const char *text) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  assert(string_interface->is_mutable);
  string_interface->prepend(object, text);
}

void ut_string_prepend_printf(UtObject *object, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  ut_cstring_ref text = ut_cstring_new_vprintf(format, ap);
  va_end(ap);
  ut_string_prepend(object, text);
}

void ut_string_prepend_code_point(UtObject *object, uint32_t code_point) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  assert(string_interface->is_mutable);
  string_interface->prepend_code_point(object, code_point);
}

void ut_string_append(UtObject *object, const char *text) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  assert(string_interface->is_mutable);
  string_interface->append(object, text, ut_cstring_get_length(text));
}

void ut_string_append_sized(UtObject *object, const char *text, size_t length) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  assert(string_interface->is_mutable);
  string_interface->append(object, text, length);
}

void ut_string_append_printf(UtObject *object, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  ut_cstring_ref text = ut_cstring_new_vprintf(format, ap);
  va_end(ap);
  ut_string_append(object, text);
}

void ut_string_append_code_point(UtObject *object, uint32_t code_point) {
  UtStringInterface *string_interface =
      ut_object_get_interface(object, &ut_string_id);
  assert(string_interface != NULL);
  assert(string_interface->is_mutable);
  string_interface->append_code_point(object, code_point);
}

char *_ut_string_to_string(UtObject *object) {
  UtObjectRef string = ut_string_new("\"");
  for (const char *c = ut_string_get_text(object); *c != '\0'; c++) {
    if (*c == 0x7) {
      ut_string_append(string, "\\a");
    } else if (*c == 0x8) {
      ut_string_append(string, "\\b");
    } else if (*c == 0x9) {
      ut_string_append(string, "\\t");
    } else if (*c == 0xa) {
      ut_string_append(string, "\\n");
    } else if (*c == 0xb) {
      ut_string_append(string, "\\v");
    } else if (*c == 0xc) {
      ut_string_append(string, "\\f");
    } else if (*c == 0xd) {
      ut_string_append(string, "\\r");
    } else if (*c == 0x1b) {
      ut_string_append(string, "\\e");
    } else if (*c == 0x22) {
      ut_string_append(string, "\\\"");
    } else if (*c == 0x5c) {
      ut_string_append(string, "\\\\");
    } else if (*c == 0x7f || *c <= 0x1f) {
      ut_string_append(string, "\\x");
      char hex_value[3];
      snprintf(hex_value, 3, "%02x", *c);
      ut_string_append(string, hex_value);
    } else {
      ut_string_append_code_point(string, *c);
    }
  }
  ut_string_append(string, "\"");

  return ut_string_take_text(string);
}

bool _ut_string_equal(UtObject *object, UtObject *other) {
  if (!ut_object_implements_string(other)) {
    return false;
  }
  return ut_cstring_equal(ut_string_get_text(object),
                          ut_string_get_text(other));
}

int _ut_string_hash(UtObject *object) {
  int hash = 0;
  for (const char *c = ut_string_get_text(object); *c != '\0'; c++) {
    hash = hash * 31 + *c;
  }
  return hash;
}

bool ut_object_implements_string(UtObject *object) {
  return ut_object_get_interface(object, &ut_string_id) != NULL;
}
