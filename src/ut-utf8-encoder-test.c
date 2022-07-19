#include <assert.h>
#include <stdlib.h>

#include "ut.h"

static size_t read_cb(void *user_data, UtObject *data, bool complete) {
  assert(ut_list_get_length(data) == 6);
  assert(ut_uint8_list_get_element(data, 0) == 72);
  assert(ut_uint8_list_get_element(data, 1) == 105);
  assert(ut_uint8_list_get_element(data, 2) == 240);
  assert(ut_uint8_list_get_element(data, 3) == 159);
  assert(ut_uint8_list_get_element(data, 4) == 152);
  assert(ut_uint8_list_get_element(data, 5) == 128);
  ut_event_loop_return(NULL);
  return ut_list_get_length(data);
}

int main(int argc, char **argv) {
  UtObjectRef code_points =
      ut_uint32_array_new_with_elements(3, 'H', 'i', 0x1f600);
  UtObjectRef code_points_stream = ut_list_input_stream_new(code_points);
  UtObjectRef utf8_encoder = ut_utf8_encoder_new(code_points_stream);
  ut_input_stream_read_all(utf8_encoder, read_cb, NULL, NULL);

  ut_event_loop_run();

  return 0;
}
