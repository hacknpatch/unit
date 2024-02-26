#include <stdio.h>

#include "ut.h"

static size_t body_cb(UtObject *object, UtObject *data, bool complete) {
  UtObjectRef data_string = ut_string_new_from_utf8(data);
  printf("%s", ut_string_get_text(data_string));

  UtObjectRef return_code = ut_int32_new(0);
  ut_event_loop_return(return_code);

  return ut_list_get_length(data);
}

static void response_cb(UtObject *object, UtObject *response) {

  if (ut_object_implements_error(response)) {
    fprintf(stderr, "%s\n", ut_error_get_description(response));
    ut_event_loop_return(ut_int32_new(1));
    return;
  }

  if (ut_http_response_get_status_code(response) != 200) {
    printf("%s\n", ut_http_response_get_reason_phrase(response));
    UtObjectRef return_code = ut_int32_new(1);
    ut_event_loop_return(return_code);
    return;
  }

  ut_input_stream_read_all(ut_http_response_get_body(response), object,
                           body_cb);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: http [uri]\n");
    return 1;
  }
  const char *uri = argv[1];

  UtObjectRef client = ut_http_client_new();

  UtObjectRef dummy_object = ut_null_new();
  ut_http_client_send_request(client, "GET", uri, NULL, dummy_object,
                              response_cb);

  UtObjectRef return_code = ut_event_loop_run();
  return ut_int32_get_value(return_code);
}
