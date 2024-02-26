#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>

#include "ut-http-message-decoder.h"
#include "ut-http-message-encoder.h"
#include "ut.h"

typedef struct {
  UtObject object;
  UtObject *client;
  UtObject *tcp_socket;
  char *host;
  uint16_t port;
  char *method;
  char *path;
  UtObject *body;
  UtObject *callback_object;
  UtHttpResponseCallback callback;
  UtObject *message_encoder;
  UtObject *message_decoder_input_stream;
  UtObject *message_decoder;
} HttpRequest;

static void http_request_init(UtObject *object) {
  HttpRequest *self = (HttpRequest *)object;
  self->message_decoder_input_stream = ut_writable_input_stream_new();
  self->message_decoder =
      ut_http_message_decoder_new_response(self->message_decoder_input_stream);
}

static void http_request_cleanup(UtObject *object) {
  HttpRequest *self = (HttpRequest *)object;

  if (self->tcp_socket != NULL) {
    ut_input_stream_close(self->tcp_socket);
  }

  ut_object_unref(self->tcp_socket);
  free(self->host);
  free(self->method);
  free(self->path);
  ut_object_unref(self->body);
  ut_object_weak_unref(&self->callback_object);
  ut_object_unref(self->message_encoder);
  ut_object_unref(self->message_decoder_input_stream);
  ut_object_unref(self->message_decoder);
}

static UtObjectInterface request_object_interface = {.type_name = "HttpRequest",
                                                     .init = http_request_init,
                                                     .cleanup =
                                                         http_request_cleanup};

UtObject *http_request_new(const char *host, uint16_t port, const char *method,
                           const char *path, UtObject *body, UtObject *client,
                           UtObject *callback_object,
                           UtHttpResponseCallback callback) {
  UtObject *object =
      ut_object_new(sizeof(HttpRequest), &request_object_interface);
  HttpRequest *self = (HttpRequest *)object;
  self->client = client;
  self->host = ut_cstring_new(host);
  self->port = port;
  self->method = ut_cstring_new(method);
  self->path = ut_cstring_new(path);
  self->body = ut_object_ref(body);
  ut_object_weak_ref(callback_object, &self->callback_object);
  self->callback = callback;
  return object;
}

typedef struct {
  UtObject object;
  UtObject *ip_address_resolver;
  UtObject *requests;
} UtHttpClient;

static void ut_http_client_init(UtObject *object) {
  UtHttpClient *self = (UtHttpClient *)object;
  self->ip_address_resolver = ut_ip_address_resolver_new();
  self->requests = ut_object_list_new();
}

static void ut_http_client_cleanup(UtObject *object) {
  UtHttpClient *self = (UtHttpClient *)object;
  ut_object_unref(self->ip_address_resolver);
  ut_object_unref(self->requests);
}

static void ut_request_unref(UtObject *req_object) {
  HttpRequest *request = (HttpRequest *)req_object;
  UtHttpClient *client = (UtHttpClient *)request->client;

  for (size_t i = 0; i < ut_list_get_length(client->requests); i++) {
    UtObject *req = ut_list_get_element(client->requests, i);
    if (req == req_object) {
      ut_list_remove(client->requests, i, 1);
      break;
    }
  }
  ut_object_unref(req_object);
}

static size_t read_cb(UtObject *object, UtObject *data, bool complete) {
  HttpRequest *request = (HttpRequest *)object;

  bool headers_done =
      ut_http_message_decoder_get_headers_done(request->message_decoder);

  size_t n_used = ut_writable_input_stream_write(
      request->message_decoder_input_stream, data, complete);

  if (!headers_done &&
      ut_http_message_decoder_get_headers_done(request->message_decoder)) {
    UtObjectRef response = ut_http_response_new(
        ut_http_message_decoder_get_status_code(request->message_decoder),
        ut_http_message_decoder_get_reason_phrase(request->message_decoder),
        ut_http_message_decoder_get_headers(request->message_decoder),
        ut_http_message_decoder_get_body(request->message_decoder));
    if (request->callback_object != NULL && request->callback != NULL) {
      request->callback(request->callback_object, response);
    }
  }

  if (ut_http_message_decoder_get_done(request->message_decoder)) {
      ut_request_unref(object);
  }

  return n_used;
}

static void connect_cb(UtObject *object, UtObject *error) {
  HttpRequest *request = (HttpRequest *)object;

  if (error != NULL) {
    if (request->callback_object != NULL && request->callback != NULL) {
      request->callback(request->callback_object, error);
    }
    ut_request_unref(object);
    return;
  }

  UtObjectRef headers = ut_list_new();
  ut_list_append_take(headers, ut_http_header_new("Host", request->host));
  request->message_encoder = ut_http_message_encoder_new_request(
      request->tcp_socket, request->method, request->path, headers,
      request->body);
  ut_http_message_encoder_encode(request->message_encoder);
  ut_http_message_decoder_read(request->message_decoder);
  ut_input_stream_read(request->tcp_socket, object, read_cb);
}

static void lookup_cb(UtObject *object, UtObject *addresses) {
  HttpRequest *request = (HttpRequest *)object;

  UtObjectRef address = ut_list_get_first(addresses);
  request->tcp_socket = ut_tcp_socket_new(address, request->port);
  ut_tcp_socket_connect(request->tcp_socket, object, connect_cb);
}

static UtObjectInterface object_interface = {.type_name = "UtHttpClient",
                                             .init = ut_http_client_init,
                                             .cleanup = ut_http_client_cleanup};

UtObject *ut_http_client_new() {
  return ut_object_new(sizeof(UtHttpClient), &object_interface);
}

void ut_http_client_send_request(UtObject *object, const char *method,
                                 const char *uri, UtObject *body,
                                 UtObject *callback_object,
                                 UtHttpResponseCallback callback) {
  assert(ut_object_is_http_client(object));
  UtHttpClient *self = (UtHttpClient *)object;

  UtObjectRef uri_object = ut_uri_new_from_string(uri);
  assert(!ut_object_implements_error(uri_object));
  assert(ut_cstring_equal(ut_uri_get_scheme(uri_object), "http"));
  const char *host = ut_uri_get_host(uri_object);
  assert(host != NULL);
  const char *path = ut_uri_get_path(uri_object);
  if (path == NULL || ut_cstring_equal(path, "")) {
    path = "/";
  }
  uint16_t port = ut_uri_get_port(uri_object);
  if (port == 0) {
    port = 80;
  }

  UtObjectRef request = http_request_new(host, port, method, path, body, object,
                                         callback_object, callback);
  ut_list_append(self->requests, request);
  ut_ip_address_resolver_lookup(self->ip_address_resolver, host, request,
                                lookup_cb);
}

bool ut_object_is_http_client(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
