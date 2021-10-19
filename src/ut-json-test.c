#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ut.h"

int main(int argc, char **argv) {
  UtObject *empty = ut_json_decode("");
  assert(empty == NULL);

  UtObject *unknown_keyword = ut_json_decode("foo");
  assert(unknown_keyword == NULL);

  UtObject *unknown_container = ut_json_decode("<>");
  assert(unknown_container == NULL);

  UtObject *null = ut_json_decode("null");
  assert(null != NULL);
  assert(ut_object_is_null(null));

  UtObject *true_keyword = ut_json_decode("true");
  assert(true_keyword != NULL);
  assert(ut_object_is_boolean(true_keyword));
  assert(ut_boolean_get_value(true_keyword) == true);

  UtObject *false_keyword = ut_json_decode("false");
  assert(false_keyword != NULL);
  assert(ut_object_is_boolean(false_keyword));
  assert(ut_boolean_get_value(false_keyword) == false);

  UtObject *zero = ut_json_decode("0");
  assert(zero != NULL);
  assert(ut_object_is_int64(zero));
  assert(ut_int64_get_value(zero) == 0);

  UtObject *one = ut_json_decode("1");
  assert(one != NULL);
  assert(ut_object_is_int64(one));
  assert(ut_int64_get_value(one) == 1);

  UtObject *minus_one = ut_json_decode("-1");
  assert(minus_one != NULL);
  assert(ut_object_is_int64(minus_one));
  assert(ut_int64_get_value(minus_one) == -1);

  UtObject *one_k = ut_json_decode("1024");
  assert(one_k != NULL);
  assert(ut_object_is_int64(one_k));
  assert(ut_int64_get_value(one_k) == 1024);

  UtObject *empty_string = ut_json_decode("\"\"");
  assert(empty_string != NULL);
  assert(ut_object_implements_string(empty_string));
  assert(strcmp(ut_string_get_text(empty_string), "") == 0);

  UtObject *string = ut_json_decode("\"Hello World!\"");
  assert(string != NULL);
  assert(ut_object_implements_string(string));
  assert(strcmp(ut_string_get_text(string), "Hello World!") == 0);

  UtObject *escaped_string =
      ut_json_decode("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0012\"");
  assert(escaped_string != NULL);
  assert(ut_object_implements_string(escaped_string));
  assert(strcmp(ut_string_get_text(escaped_string), "\"\\/\b\f\n\r\t\x12") ==
         0);

  UtObject *emoji_string = ut_json_decode("\"😀\"");
  assert(emoji_string != NULL);
  assert(ut_object_implements_string(emoji_string));
  assert(strcmp(ut_string_get_text(emoji_string), "😀") == 0);

  UtObject *unterminated_array = ut_json_decode("[");
  assert(unterminated_array == NULL);

  UtObject *empty_array = ut_json_decode("[]");
  assert(empty_array != NULL);
  assert(ut_object_implements_list(empty_array));
  assert(ut_list_get_length(empty_array) == 0);

  UtObject *unterminated_object = ut_json_decode("{");
  assert(unterminated_object == NULL);

  UtObject *empty_object = ut_json_decode("{}");
  assert(empty_object != NULL);
  assert(ut_object_implements_map(empty_object));
  assert(ut_map_get_length(empty_object) == 0);
}
