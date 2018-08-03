/* Femc Driver
 * Copyright (C) 2015-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_UTILS_HEADER
#define FEMC_DRIVER_UTILS_HEADER

#include "dispatcher.h"
#include "utils.fwd.h"

struct fdu_memory_area_ {
    unsigned char* begin;
    unsigned char* end;
};

fdu_memory_area init_memory_area(unsigned char* begin, uint32_t size);
fdu_memory_area init_memory_area_cont(unsigned char** begin_counter, uint32_t size);

/*------------------------------------------------------------
 *
 * Buffered I/O services
 *
 */

/*
  'fdu_bufio_buffer' is created with fdu_new_*put_bufio().

  FULLY OPERATIONAL STATE: 'notify_callback' will be called whenever there is a
  movement in data. The buffer is changed to reflect the data and user can
  operate on that.

  CLOSED STATE: When the service changes to closed state, 'close_callback' is
  called. This can happen either during 'notify_callback' or by user calling
  'fdu_bufio_close'. In closed state fd is set to <0 and callbacks will no
  longer be called.

  FREED: Whenever the user is finished with the buffer, fdu_bufio_free() should
  be called to release the service resources. (Doesn't apply to inplace-bufios.)

  The user is always responsible for closing fd. The bufio service will only do
  read/write operations on it, never close/shutdown.
*/

typedef struct fdu_bufio_service_ fdu_bufio_service;

extern const unsigned int sizeof_fdu_bufio_service;     // = sizeof(fdu_bufio_service);

//

typedef struct fdu_bufio_buffer_ {
    int fd;
    bool can_xfer;
    //
    unsigned char* data;
    unsigned int size;
    unsigned int filled;
    //
    fdu_bufio_service* service;
} fdu_bufio_buffer;

typedef bool (*fdu_bufio_notify_func)(fdu_bufio_buffer*, void* context);
typedef void (*fdu_bufio_close_func)(fdu_bufio_buffer*, void* context, int fd, int error);

//

static inline bool fdu_bufio_is_closed(fdu_bufio_buffer* bufio) { return bufio->fd < 0; }
static inline bool fdu_bufio_is_empty(fdu_bufio_buffer* bufio) { return !bufio->filled; }

//

bool fdu_bufio_touch(fdu_bufio_buffer*);
void fdu_bufio_close(fdu_bufio_buffer*);
void fdu_bufio_free(fdu_bufio_buffer*);

unsigned int fdu_bufio_transfer(fdu_bufio_buffer*, fdu_bufio_buffer*);

//

fdu_bufio_buffer* fdu_new_input_bufio(int fd,
                                      unsigned int buffer_size,
                                      void* context,
                                      fdu_bufio_notify_func notify_callback,
                                      fdu_bufio_close_func close_callback);

fdu_bufio_buffer* fdu_new_output_bufio(int fd,
                                       unsigned int buffer_size,
                                       void* context,
                                       fdu_bufio_notify_func notify_callback,
                                       fdu_bufio_close_func close_callback);

fdu_bufio_buffer* fdu_new_input_bufio_inplace(int fd,
                                              fdu_memory_area service_memory,
                                              fdu_memory_area buffer_memory,
                                              void* context,
                                              fdu_bufio_notify_func notify_callback,
                                              fdu_bufio_close_func close_callback);

fdu_bufio_buffer* fdu_new_output_bufio_inplace(int fd,
                                               fdu_memory_area service_memory,
                                               fdu_memory_area buffer_memory,
                                               void* context,
                                               fdu_bufio_notify_func notify_callback,
                                               fdu_bufio_close_func close_callback);

/*------------------------------------------------------------
 *
 * Pending connect()
 *
 */

typedef bool (*fdu_notify_connect_func)(void* context, int fd, int errno);

bool fdu_pending_connect(int fd, fdu_notify_connect_func callback, void* context);

/*------------------------------------------------------------
 *
 * Non-blocking DNS lookup
 *
 */

typedef void (*fdu_dnsserv_notify_func)(void* context, const char* address);    // address can be 0

bool fdu_dnsserv_lookup(const char* name, fdu_dnsserv_notify_func callback, void* context);

/*------------------------------------------------------------
 *
 * Auto-accept connection
 *
 */

bool fdu_auto_accept_connection(int fd, fdd_notify_func callback, void* callback_context);

/*------------------------------------------------------------
 *
 * General utilities
 *
 */

enum {
    // fdu_listen_inet4() & fdu_lazy_connect():
    FDU_SOCKET_UDP       =0x1,          // default: TCP

    // fdu_listen_inet4():
    FDU_SOCKET_LOCAL     =0x2,          // default: any interface
    FDU_SOCKET_NOREUSE   =0x4,
    FDU_SOCKET_BROADCAST =0x8,
};

int fdu_listen_inet4(unsigned short port, unsigned int options);
int fdu_listen_unix(const char* path, unsigned int options);
// >=0 (fd)

struct sockaddr_in;

bool fdu_lazy_connect(struct sockaddr_in*,              // IPv4 address
                      fdu_notify_connect_func,          // call when rdy
                      void*,                            // context
                      unsigned int);                    // options (UDP)
// =true : callback will be called
// =false: connect attempt failed instantly

// ------------------------------------------------------------

bool fdu_safe_read(int fd, unsigned char* start, const unsigned char* end);
bool fdu_safe_write(int fd, const unsigned char* start, const unsigned char* end);
bool fdu_safe_write_str(int fd, const unsigned char* buffer);

bool fdu_safe_close(int fd);
bool fdu_safe_chdir(const char*);
bool fdu_copy_fd(int oldfd, int newfd);
bool fdu_move_fd(int oldfd, int newfd);

enum { FDU_PIDFILE_ONLYCHECK = 0x1 };                   // don't hold lock, don't write pid

bool fdu_pidfile(const char* filename, int options); // >=0

#endif
