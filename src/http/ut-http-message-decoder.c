#include <assert.h>

#include "ut-http-message-decoder.h"
#include "ut.h"

typedef enum {
  DECODER_STATE_REQUEST_LINE,
  DECODER_STATE_STATUS_LINE,
  DECODER_STATE_HEADER,
  DECODER_STATE_CHUNK_HEADER,
  DECODER_STATE_BODY,
  DECODER_STATE_ERROR,
  DECODER_STATE_DONE
} DecoderState;

typedef enum {
  BODY_LENGTH_FORMAT_EOF,
  BODY_LENGTH_FORMAT_FIXED,
  BODY_LENGTH_FORMAT_CHUNKED
} BodyLengthFormat;

typedef struct {
  UtObject object;

  // Stream reading HTTP messages from.
  UtObject *input_stream;

  // State of decoding.
  DecoderState state;

  // Requested method.
  char *method;

  // Requested path.
  char *path;

  // Received status code.
  unsigned int status_code;

  // Received reason phrase.
  char *reason_phrase;

  // Message headers.
  UtObject *headers;

  // True when have received all headers.
  bool headers_done;

  // Method of determining content length
  BodyLengthFormat body_length_format;

  // Length of body when [length_format] is FIXED.
  size_t content_length;

  // Number of bytes written to [body].
  size_t body_length;

  // Message body;
  UtObject *body;

  // Error that occurred during decoding.
  UtObject *error;
} UtHttpMessageDecoder;

static void set_error(UtHttpMessageDecoder *self, const char *description) {
  if (self->state == DECODER_STATE_ERROR) {
    return;
  }

  self->error = ut_http_error_new(description);
  self->state = DECODER_STATE_ERROR;
}

// Finds the first line end sequence (\r\n) in [data] and returns the offset to
// it.
static ssize_t find_line_end(UtObject *data) {
  size_t data_length = ut_list_get_length(data);
  if (data_length < 2) {
    return -1;
  }

  for (size_t i = 0; i < data_length - 1; i++) {
    if (ut_uint8_list_get_element(data, i) == '\r' &&
        ut_uint8_list_get_element(data, i + 1) == '\n') {
      return i;
    }
  }

  return -1;
}

static UtObject *get_line(UtObject *data, size_t *offset) {
  ssize_t line_end = find_line_end(data);
  if (line_end < 0) {
    return NULL;
  }

  *offset = line_end + 2;
  return ut_list_get_sublist(data, 0, line_end);
}

static ssize_t find_character(UtObject *data, size_t offset, char character) {
  size_t data_length = ut_list_get_length(data);
  for (size_t i = offset; i < data_length; i++) {
    if (ut_uint8_list_get_element(data, i) == character) {
      return i;
    }
  }

  return -1;
}

static char *get_string(UtObject *data, size_t start, size_t end) {
  while (start < end && ut_uint8_list_get_element(data, start) == ' ') {
    start++;
  }
  while (end > start && ut_uint8_list_get_element(data, end - 1) == ' ') {
    end--;
  }
  UtObjectRef string_data = ut_list_get_sublist(data, start, end - start);
  UtObjectRef string = ut_string_new_from_iso_8859_1(string_data);
  return ut_string_take_text(string);
}

static bool parse_request_line(UtHttpMessageDecoder *self, UtObject *data) {
  size_t method_start = 0;
  ssize_t method_end = find_character(data, method_start, ' ');
  if (method_end < 0) {
    set_error(self, "Invalid HTTP request line");
    return false;
  }

  size_t path_start = method_end + 1;
  ssize_t path_end = find_character(data, path_start, ' ');
  if (path_end < 0) {
    set_error(self, "Invalid HTTP request line");
    return false;
  }

  size_t protocol_version_start = path_end + 1;
  size_t protocol_version_end = ut_list_get_length(data);
  ut_cstring_ref protocol_version =
      get_string(data, protocol_version_start, protocol_version_end);
  if (!ut_cstring_equal(protocol_version, "HTTP/1.1")) {
    set_error(self, "Invalid HTTP version");
    return false;
  }

  self->method = get_string(data, method_start, method_end);
  self->path = get_string(data, path_start, path_end);

  return true;
}

static bool parse_status_line(UtHttpMessageDecoder *self, UtObject *data) {
  size_t protocol_version_start = 0;
  ssize_t protocol_version_end =
      find_character(data, protocol_version_start, ' ');
  if (protocol_version_end < 0) {
    set_error(self, "Invalid HTTP status line");
    return false;
  }
  ut_cstring_ref protocol_version =
      get_string(data, protocol_version_start, protocol_version_end);
  if (!ut_cstring_equal(protocol_version, "HTTP/1.1")) {
    set_error(self, "Invalid HTTP version");
    return false;
  }

  size_t status_code_start = protocol_version_end + 1;
  ssize_t status_code_end = find_character(data, status_code_start, ' ');
  if (status_code_end < 0) {
    set_error(self, "Invalid HTTP status line");
    return false;
  }

  size_t reason_phrase_start = status_code_end + 1;
  size_t reason_phrase_end = ut_list_get_length(data);

  ut_cstring_ref status_code =
      get_string(data, status_code_start, status_code_end);
  self->status_code = atoi(status_code);
  self->reason_phrase =
      get_string(data, reason_phrase_start, reason_phrase_end);

  return true;
}

static size_t decode_request_line(UtHttpMessageDecoder *self, UtObject *data) {
  size_t offset = 0;
  UtObjectRef line = get_line(data, &offset);
  if (line == NULL) {
    return 0;
  }

  if (!parse_request_line(self, line)) {
    return 0;
  }

  self->state = DECODER_STATE_HEADER;

  return offset;
}

static size_t decode_status_line(UtHttpMessageDecoder *self, UtObject *data) {
  size_t offset = 0;
  UtObjectRef line = get_line(data, &offset);
  if (line == NULL) {
    return 0;
  }

  if (!parse_status_line(self, line)) {
    return 0;
  }

  self->state = DECODER_STATE_HEADER;

  return offset;
}

static bool parse_header(UtHttpMessageDecoder *self, UtObject *data) {
  size_t name_start = 0;
  ssize_t name_end = find_character(data, name_start, ':');
  if (name_end < 0) {
    return false;
  }

  size_t value_start = name_end + 1;
  size_t value_end = ut_list_get_length(data);

  ut_cstring_ref name = get_string(data, name_start, name_end);
  ut_cstring_ref value = get_string(data, value_start, value_end);
  UtObjectRef header = ut_http_header_new(name, value);

  ut_list_append(self->headers, header);

  return true;
}

static UtObject *find_header(UtHttpMessageDecoder *self, const char *name) {
  size_t headers_length = ut_list_get_length(self->headers);
  for (size_t i = 0; i < headers_length; i++) {
    UtObject *header = ut_object_list_get_element(self->headers, i);
    // FIXME: Case insensiive
    if (ut_cstring_equal(ut_http_header_get_name(header), name)) {
      return header;
    }
  }

  return NULL;
}

static size_t decode_header(UtHttpMessageDecoder *self, UtObject *data) {
  size_t offset = 0;
  UtObjectRef line = get_line(data, &offset);
  if (line == NULL) {
    return 0;
  }

  // Ends on empty line.
  if (ut_list_get_length(line) == 0) {
    self->headers_done = true;

    // Determine length of body.
    UtObject *content_length_header = find_header(self, "Content-Length");
    // FIXME: Handle more complex transfer encodings
    // FIXME: Validate not both content-length and chunked transfer encoding.
    UtObject *transfer_encoding_header = find_header(self, "Transfer-Encoding");
    if (content_length_header != NULL) {
      self->body_length_format = BODY_LENGTH_FORMAT_FIXED;
      // FIXME: Handle format errors.
      self->content_length =
          strtoul(ut_http_header_get_value(content_length_header), NULL, 10);
      if (self->content_length == 0) {
        UtObjectRef d = ut_uint8_list_new();
        ut_buffered_input_stream_write(self->body, d, true);
        self->state = DECODER_STATE_DONE;
      } else {
        self->state = DECODER_STATE_BODY;
      }
    } else if (transfer_encoding_header != NULL &&
               ut_cstring_equal(
                   ut_http_header_get_value(transfer_encoding_header),
                   "chunked")) {
      self->body_length_format = BODY_LENGTH_FORMAT_CHUNKED;
      self->state = DECODER_STATE_CHUNK_HEADER;
    } else {
      self->body_length_format = BODY_LENGTH_FORMAT_EOF;
      self->state = DECODER_STATE_BODY;
    }

    return offset;
  }

  if (!parse_header(self, line)) {
    set_error(self, "Invalid HTTP header");
    return 0;
  }

  return offset;
}

static size_t decode_chunk_header(UtHttpMessageDecoder *self, UtObject *data) {
  ssize_t line_end = find_line_end(data);
  if (line_end < 0) {
    // FIXME: Abort on invalid characters
    return 0;
  }
  ut_cstring_ref chunk_header = get_string(data, 0, line_end);

  // FIXME: Handle format errors.
  self->content_length = strtoul(chunk_header, NULL, 16);
  self->state = DECODER_STATE_BODY;

  return line_end + 2;
}

static size_t decode_body_eof(UtHttpMessageDecoder *self, UtObject *data,
                              bool complete) {
  ut_buffered_input_stream_write(self->body, data, complete);
  if (complete) {
    self->state = DECODER_STATE_DONE;
  }

  return ut_list_get_length(data);
}

static size_t decode_body_fixed(UtHttpMessageDecoder *self, UtObject *data,
                                bool complete) {
  size_t data_length = ut_list_get_length(data);

  size_t remaining_length = self->content_length - self->body_length;
  size_t n_used =
      data_length < remaining_length ? data_length : remaining_length;
  if (n_used == 0) {
    return 0;
  }

  bool body_complete = complete || n_used == remaining_length;

  UtObjectRef d = ut_list_get_sublist(data, 0, n_used);
  ut_buffered_input_stream_write(self->body, d, body_complete);
  self->body_length += n_used;
  if (self->body_length >= self->content_length) {
    self->state = DECODER_STATE_DONE;
  }

  return n_used;
}

static size_t decode_body_chunked(UtHttpMessageDecoder *self, UtObject *data,
                                  bool complete) {
  size_t data_length = ut_list_get_length(data);

  size_t chunk_length = self->content_length + 2;
  if (data_length < chunk_length) {
    return 0;
  }

  if (ut_uint8_list_get_element(data, self->content_length) != '\r' ||
      ut_uint8_list_get_element(data, self->content_length + 1) != '\n') {
    set_error(self, "Invalid HTTP chunk");
    return 0;
  }

  bool done = self->content_length == 0;

  UtObjectRef d = ut_list_get_sublist(data, 0, self->content_length);
  ut_buffered_input_stream_write(self->body, d, done);

  if (done) {
    self->state = DECODER_STATE_DONE;
  } else {
    self->state = DECODER_STATE_CHUNK_HEADER;
  }

  return chunk_length;
}

static size_t decode_body(UtHttpMessageDecoder *self, UtObject *data,
                          bool complete) {
  switch (self->body_length_format) {
  case BODY_LENGTH_FORMAT_EOF:
    return decode_body_eof(self, data, complete);
  case BODY_LENGTH_FORMAT_FIXED:
    return decode_body_fixed(self, data, complete);
  case BODY_LENGTH_FORMAT_CHUNKED:
    return decode_body_chunked(self, data, complete);
  default:
    return 0;
  }
}

static size_t read_cb(UtObject *object, UtObject *data, bool complete) {
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;

  size_t data_length = ut_list_get_length(data);
  size_t offset = 0;
  while (true) {
    size_t n_used;
    UtObjectRef d = ut_list_get_sublist(data, offset, data_length - offset);
    DecoderState old_state = self->state;
    switch (self->state) {
    case DECODER_STATE_REQUEST_LINE:
      n_used = decode_request_line(self, d);
      break;
    case DECODER_STATE_STATUS_LINE:
      n_used = decode_status_line(self, d);
      break;
    case DECODER_STATE_HEADER:
      n_used = decode_header(self, d);
      break;
    case DECODER_STATE_CHUNK_HEADER:
      n_used = decode_chunk_header(self, d);
      break;
    case DECODER_STATE_BODY:
      n_used = decode_body(self, d, complete);
      break;
    case DECODER_STATE_ERROR:
    case DECODER_STATE_DONE:
      return offset;
    default:
      assert(false);
    }

    if (self->state == old_state && n_used == 0) {
      if (complete && self->state != DECODER_STATE_DONE) {
        set_error(self, "Incomplete HTTP message");
      }
      return offset;
    }

    offset += n_used;
  }
}

static void ut_http_message_decoder_init(UtObject *object) {
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  self->headers = ut_list_new();
  self->body = ut_buffered_input_stream_new();
}

static void ut_http_message_decoder_cleanup(UtObject *object) {
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;

  ut_input_stream_close(self->input_stream);

  ut_object_unref(self->input_stream);
  free(self->method);
  free(self->path);
  free(self->reason_phrase);
  ut_object_unref(self->headers);
  ut_object_unref(self->body);
  ut_object_unref(self->error);
}

static UtObjectInterface object_interface = {
    .type_name = "UtHttpMessageDecoder",
    .init = ut_http_message_decoder_init,
    .cleanup = ut_http_message_decoder_cleanup};

UtObject *ut_http_message_decoder_new_request(UtObject *input_stream) {
  UtObject *object =
      ut_object_new(sizeof(UtHttpMessageDecoder), &object_interface);
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  self->input_stream = ut_object_ref(input_stream);
  self->state = DECODER_STATE_REQUEST_LINE;
  return object;
}

UtObject *ut_http_message_decoder_new_response(UtObject *input_stream) {
  UtObject *object =
      ut_object_new(sizeof(UtHttpMessageDecoder), &object_interface);
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  self->input_stream = ut_object_ref(input_stream);
  self->state = DECODER_STATE_STATUS_LINE;
  return object;
}

void ut_http_message_decoder_read(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  ut_input_stream_read(self->input_stream, object, read_cb);
}

const char *ut_http_message_decoder_get_method(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->method;
}

const char *ut_http_message_decoder_get_path(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->path;
}

unsigned int ut_http_message_decoder_get_status_code(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->status_code;
}

const char *ut_http_message_decoder_get_reason_phrase(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->reason_phrase;
}

UtObject *ut_http_message_decoder_get_headers(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->headers;
}

bool ut_http_message_decoder_get_headers_done(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->headers_done;
}

UtObject *ut_http_message_decoder_get_body(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->body;
}

bool ut_http_message_decoder_get_done(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->state == DECODER_STATE_DONE ||
         self->state == DECODER_STATE_ERROR;
}

UtObject *ut_http_message_decoder_get_error(UtObject *object) {
  assert(ut_object_is_http_message_decoder(object));
  UtHttpMessageDecoder *self = (UtHttpMessageDecoder *)object;
  return self->error;
}

bool ut_object_is_http_message_decoder(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
