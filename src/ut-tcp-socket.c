#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ut.h"

typedef struct {
  UtObject object;
  UtObject *address;
  uint16_t port;
  UtObject *fd;
  UtObject *write_watch;
  UtObject *read_watch;
  UtObject *connect_callback_object;
  UtTcpSocketConnectCallback connect_callback;
  UtObject *read_buffer;
  size_t n_read;
  bool is_complete;
  UtObject *read_callback_object;
  UtInputStreamCallback read_callback;
} UtTcpSocket;

static void ut_tcp_socket_cleanup(UtObject *object) {
  UtTcpSocket *self = (UtTcpSocket *)object;
  ut_object_unref(self->address);
  ut_object_unref(self->fd);
  ut_object_unref(self->write_watch);
  ut_object_weak_unref(&self->connect_callback_object);
  ut_object_unref(self->read_buffer);
  ut_object_unref(self->read_watch);
}

static void connect_cb(UtObject *object, UtObject *error) {}

static void write_cb(UtObject *object) {
  UtTcpSocket *self = (UtTcpSocket *)object;

  // Only one event required to detect connect.
  ut_event_loop_cancel_watch(self->write_watch);

  int error_code;
  socklen_t error_length = sizeof(error_code);
  getsockopt(ut_file_descriptor_get_fd(self->fd), SOL_SOCKET, SO_ERROR,
             &error_code, &error_length);
  UtObjectRef error = NULL;
  if (error_code != 0) {
    error = ut_system_error_new(error_code);
  }

  if (self->connect_callback_object != NULL && self->connect_callback != NULL) {
    self->connect_callback(self->connect_callback_object, error);
  }
}

static void read_cb(UtObject *object) {
  UtTcpSocket *self = (UtTcpSocket *)object;

  size_t block_size = 65535;
  if (ut_list_get_length(self->read_buffer) < self->n_read + block_size) {
    ut_list_resize(self->read_buffer, self->n_read + block_size);
  }

  struct iovec iov;
  iov.iov_base =
      ut_uint8_list_get_writable_data(self->read_buffer) + self->n_read;
  iov.iov_len = block_size;
  uint8_t control_data[CMSG_SPACE(sizeof(int) * 1024)];
  struct msghdr msg;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control_data;
  msg.msg_controllen = sizeof(control_data);
  msg.msg_flags = 0;
  ssize_t n_read = recvmsg(ut_file_descriptor_get_fd(self->fd), &msg, 0);
  assert(n_read >= 0);

  UtObjectRef fds = NULL;
  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
       cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      size_t data_length =
          cmsg->cmsg_len - ((uint8_t *)CMSG_DATA(cmsg) - (uint8_t *)cmsg);
      size_t cmsg_fds_length = data_length / sizeof(int);
      int cmsg_fds[cmsg_fds_length];
      memcpy(cmsg_fds, CMSG_DATA(cmsg), cmsg->cmsg_len);
      for (size_t i = 0; i < cmsg_fds_length; i++) {
        if (fds == NULL) {
          fds = ut_list_new();
        }
        ut_list_append_take(fds, ut_file_descriptor_new(cmsg_fds[i]));
      }
    }
  }

  if (n_read == 0) {
    self->is_complete = true;
  }

  self->n_read += n_read;
  UtObjectRef data = ut_list_get_sublist(self->read_buffer, 0, self->n_read);
  UtObjectRef data_with_fds = NULL;
  if (fds != NULL) {
    data_with_fds = ut_uint8_array_with_fds_new(data, fds);
  }
  size_t n_used =
      self->read_callback_object != NULL
          ? self->read_callback(self->read_callback_object,
                                data_with_fds != NULL ? data_with_fds : data,
                                self->is_complete)
          : 0;
  assert(n_used <= self->n_read);
  ut_list_remove(self->read_buffer, 0, n_used);
  self->n_read -= n_used;
}

static void ut_tcp_socket_read(UtObject *object, UtObject *callback_object,
                               UtInputStreamCallback callback) {
  UtTcpSocket *self = (UtTcpSocket *)object;

  assert(self->read_callback == NULL);
  assert(callback != NULL);

  ut_object_weak_ref(callback_object, &self->read_callback_object);
  self->read_callback = callback;

  self->read_buffer = ut_uint8_array_new();
  self->read_watch = ut_event_loop_add_read_watch(self->fd, object, read_cb);
}

static void ut_tcp_socket_close(UtObject *object) {
  UtTcpSocket *self = (UtTcpSocket *)object;
  if (self->read_watch != NULL) {
    ut_event_loop_cancel_watch(self->read_watch);
  } else {
    ut_file_descriptor_close(self->fd);
  }
}

static UtInputStreamInterface input_stream_interface = {
    .read = ut_tcp_socket_read, .close = ut_tcp_socket_close};

static void ut_tcp_socket_write(UtObject *object, UtObject *data,
                                UtObject *callback_object,
                                UtOutputStreamCallback callback) {
  UtTcpSocket *self = (UtTcpSocket *)object;

  UtObject *d;
  UtObject *file_descriptors = NULL;
  if (ut_object_is_uint8_array_with_fds(data)) {
    d = ut_uint8_array_with_fds_get_data(data);
    file_descriptors = ut_uint8_array_with_fds_get_fds(data);
  } else {
    d = data;
  }

  ssize_t data_length = ut_list_get_length(d);
  const uint8_t *buffer;
  uint8_t *allocated_buffer = NULL;
  buffer = ut_uint8_list_get_data(d);
  if (buffer == NULL) {
    allocated_buffer = ut_uint8_list_copy_data(d);
    buffer = allocated_buffer;
  }

  if (file_descriptors != NULL && ut_list_get_length(file_descriptors) > 0) {
    size_t file_descriptors_length = ut_list_get_length(file_descriptors);
    struct iovec iov;
    iov.iov_base = (void *)buffer;
    iov.iov_len = data_length;
    uint8_t control_data[CMSG_SPACE(sizeof(int) * file_descriptors_length)];
    memset(control_data, 0, sizeof(control_data));
    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_data;
    msg.msg_controllen = sizeof(control_data);
    msg.msg_flags = 0;
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * file_descriptors_length);
    int cmsg_fds[file_descriptors_length];
    for (size_t i = 0; i < file_descriptors_length; i++) {
      UtObjectRef fd = ut_list_get_element(file_descriptors, i);
      cmsg_fds[i] = ut_file_descriptor_get_fd(fd);
    }
    memcpy(CMSG_DATA(cmsg), cmsg_fds, sizeof(cmsg_fds));
    assert(sendmsg(ut_file_descriptor_get_fd(self->fd), &msg, 0) ==
           data_length);
  } else {
    assert(send(ut_file_descriptor_get_fd(self->fd), buffer, data_length, 0) ==
           data_length);
  }

  free(allocated_buffer);
}

static UtOutputStreamInterface output_stream_interface = {
    .write = ut_tcp_socket_write};

static UtObjectInterface object_interface = {
    .type_name = "UtTcpSocket",
    .cleanup = ut_tcp_socket_cleanup,
    .interfaces = {{&ut_input_stream_id, &input_stream_interface},
                   {&ut_output_stream_id, &output_stream_interface},
                   {NULL, NULL}}};

UtObject *ut_tcp_socket_new_from_fd(UtObject *fd) {
  UtObject *object = ut_object_new(sizeof(UtTcpSocket), &object_interface);
  UtTcpSocket *self = (UtTcpSocket *)object;

  struct sockaddr address;
  socklen_t address_length = sizeof(address);
  assert(getpeername(ut_file_descriptor_get_fd(fd), &address,
                     &address_length) == 0);
  sa_family_t family = address.sa_family;

  if (family == AF_INET) {
    struct sockaddr_in address4;
    address_length = sizeof(address4);
    assert(getpeername(ut_file_descriptor_get_fd(fd),
                       (struct sockaddr *)&address4, &address_length) == 0);
    self->address = ut_ipv4_address_new(ntohl(address4.sin_addr.s_addr));
    self->port = ntohs(address4.sin_port);
  } else if (family == AF_INET6) {
    struct sockaddr_in6 address6;
    address_length = sizeof(address6);
    assert(getpeername(ut_file_descriptor_get_fd(fd),
                       (struct sockaddr *)&address6, &address_length) == 0);
    self->address = ut_ipv6_address_new(address6.sin6_addr.s6_addr);
    self->port = ntohs(address6.sin6_port);
  } else if (family == AF_UNIX) {
    struct sockaddr_un address_unix;
    address_unix.sun_path[0] = '\0';
    address_length = sizeof(address_unix);
    assert(getpeername(ut_file_descriptor_get_fd(fd),
                       (struct sockaddr *)&address_unix, &address_length) == 0);
    self->address = ut_unix_socket_address_new(address_unix.sun_path);
    self->port = 0;
  } else {
    assert(false);
  }

  self->fd = ut_object_ref(fd);

  return object;
}

UtObject *ut_tcp_socket_new(UtObject *address, uint16_t port) {
  UtObject *object = ut_object_new(sizeof(UtTcpSocket), &object_interface);
  UtTcpSocket *self = (UtTcpSocket *)object;

  assert(address != NULL);

  self->address = ut_object_ref(address);
  self->port = port;

  sa_family_t family;
  if (ut_object_is_ipv4_address(address)) {
    family = AF_INET;
    assert(port > 0);
  } else if (ut_object_is_ipv6_address(address)) {
    family = AF_INET6;
    assert(port > 0);
  } else if (ut_object_is_unix_socket_address(address)) {
    family = AF_UNIX;
  } else {
    assert(false);
  }

  int fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK, 0);
  assert(fd >= 0);
  self->fd = ut_file_descriptor_new(fd);

  return object;
}

void ut_tcp_socket_connect(UtObject *object, UtObject *callback_object,
                           UtTcpSocketConnectCallback callback) {
  assert(ut_object_is_tcp_socket(object));
  UtTcpSocket *self = (UtTcpSocket *)object;

  assert(self->connect_callback == NULL);
  if (callback == NULL) {
    callback = connect_cb;
  }

  ut_object_weak_ref(callback_object, &self->connect_callback_object);
  self->connect_callback = callback;

  self->write_watch = ut_event_loop_add_write_watch(self->fd, object, write_cb);

  struct sockaddr_in address4;
  struct sockaddr_in6 address6;
  struct sockaddr_un address_unix;
  struct sockaddr *socket_address;
  socklen_t socket_address_length;
  if (ut_object_is_ipv4_address(self->address)) {
    memset(&address4, 0, sizeof(address4));
    address4.sin_family = AF_INET;
    address4.sin_addr.s_addr =
        htonl(ut_ipv4_address_get_address(self->address));
    address4.sin_port = htons(self->port);
    socket_address = (struct sockaddr *)&address4;
    socket_address_length = sizeof(address4);
  } else if (ut_object_is_ipv6_address(self->address)) {
    memset(&address6, 0, sizeof(address6));
    address6.sin6_family = AF_INET6;
    const uint8_t *a6 = ut_ipv6_address_get_address(self->address);
    for (size_t i = 0; i < 16; i++) {
      address6.sin6_addr.s6_addr[i] = a6[i];
    }
    address6.sin6_port = htons(self->port);
    socket_address = (struct sockaddr *)&address6;
    socket_address_length = sizeof(address6);
  } else if (ut_object_is_unix_socket_address(self->address)) {
    memset(&address_unix, 0, sizeof(address_unix));
    address_unix.sun_family = AF_UNIX;
    snprintf(address_unix.sun_path, sizeof(address_unix.sun_path), "%s",
             ut_unix_socket_address_get_path(self->address));
    socket_address = (struct sockaddr *)&address_unix;
    socket_address_length = sizeof(address_unix);
  } else {
    assert(false);
  }
  int result = connect(ut_file_descriptor_get_fd(self->fd), socket_address,
                       socket_address_length);
  assert(result == 0 || errno == EINPROGRESS);
}

UtObject *ut_tcp_socket_get_address(UtObject *object) {
  assert(ut_object_is_tcp_socket(object));
  UtTcpSocket *self = (UtTcpSocket *)object;
  return self->address;
}

uint16_t ut_tcp_socket_get_port(UtObject *object) {
  assert(ut_object_is_tcp_socket(object));
  UtTcpSocket *self = (UtTcpSocket *)object;
  return self->port;
}

uint16_t ut_tcp_socket_get_local_port(UtObject *object) {
  assert(ut_object_is_tcp_socket(object));
  UtTcpSocket *self = (UtTcpSocket *)object;
  struct sockaddr_in address4;
  struct sockaddr_in6 address6;
  struct sockaddr *socket_address;
  socklen_t socket_address_length;
  if (ut_object_is_ipv4_address(self->address)) {
    socket_address = (struct sockaddr *)&address4;
    socket_address_length = sizeof(address4);
  } else if (ut_object_is_ipv6_address(self->address)) {
    socket_address = (struct sockaddr *)&address6;
    socket_address_length = sizeof(address6);
  } else if (ut_object_is_unix_socket_address(self->address)) {
    return 0;
  } else {
    assert(false);
  }
  assert(getsockname(ut_file_descriptor_get_fd(self->fd), socket_address,
                     &socket_address_length) == 0);
  sa_family_t family = socket_address->sa_family;
  if (family == AF_INET) {
    return ntohs(address4.sin_port);
  } else if (family == AF_INET6) {
    return ntohs(address6.sin6_port);
  } else if (family == AF_UNIX) {
    return 0;
  } else {
    assert(false);
  }
}

void ut_tcp_socket_send(UtObject *object, UtObject *data) {
  assert(ut_object_is_tcp_socket(object));
  ut_output_stream_write(object, data);
}

bool ut_object_is_tcp_socket(UtObject *object) {
  return ut_object_is_type(object, &object_interface);
}
