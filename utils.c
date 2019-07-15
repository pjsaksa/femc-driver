/* Femc Driver
 * Copyright (C) 2015-2019 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "utils.h"
#include "error_stack.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef UNIX_PATH_MAX
enum { UNIX_PATH_MAX = 108 };
#endif

// ------------------------------------------------------------

fdu_memory_area init_memory_area(unsigned char* begin, const uint32_t size)
{
    return (fdu_memory_area){begin, begin+size};
}

fdu_memory_area init_memory_area_cont(unsigned char** beginp, const uint32_t size)
{
    unsigned char* const begin = *beginp;
    unsigned char* const end   = (*beginp) += size;

    return (fdu_memory_area){begin, end};
}

/*------------------------------------------------------------
 *
 * Buffered I/O services
 *
 */

enum {
    MinimumBufferSize = 64,
    MaximumBufferSize = 1024*1024,
};

typedef enum {
    bufio_input,
    bufio_output,
} fdu_bufio_service_type;

enum {  // cs = callstack
    bufio_cs_active     = 1,
    bufio_cs_closed     = 1 << 1,
    bufio_cs_freed      = 1 << 2,
};

struct fdu_bufio_service_ {
    fdu_bufio_service_type type;
    fdu_bufio_buffer buffer;

    union {
        fdd_service_input input_service;
        fdd_service_output output_service;
    };

    void* context;
    fdu_bufio_notify_func notify;
    fdu_bufio_close_func close;

    int close_errno;
    unsigned int callstack;
};

const unsigned int sizeof_fdu_bufio_service = sizeof(fdu_bufio_service);

//

#define CALLSTACK   (service->callstack)
#define CAN_XFER    (service->buffer.can_xfer)
#define CONTEXT     (service->context)
#define DATA        (service->buffer.data)
#define FD          (service->buffer.fd)
#define FILLED      (service->buffer.filled)
#define NOTIFY      (service->notify)
#define SIZE        (service->buffer.size)

static bool fdu_bufio_got_input(void* service_v, int fd)
{
    fdu_bufio_service* service = (fdu_bufio_service*) service_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_bufio)))
        return false;
    //
    if (!service
        || fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    FDE_ASSERT( service->type == bufio_input , "invalid service type" , false );
    FDE_ASSERT( fd == FD , "fd corrupted" , false );
    FDE_ASSERT( FILLED <= SIZE , "filled > size (1)" , false );
    //

    if (FILLED == SIZE) {
        CAN_XFER = true;

        return fdd_remove_input(&service->input_service, fd)
            && fde_pop_context(fdu_context_bufio, ectx);
    }

    const int i = read(fd, &DATA[FILLED], SIZE - FILLED);

    if (CAN_XFER) {
        CAN_XFER = false;

        if (!fdd_add_input(&service->input_service, fd))
            return false;
    }

    bool lazy_close = false;

    if (i > 0) {
        FILLED += i;

        FDE_ASSERT_DEBUG( FILLED <= SIZE , "filled > size (2)" , false );

        if (NOTIFY) {
            CALLSTACK |= bufio_cs_active;

            lazy_close = (!NOTIFY(&service->buffer, CONTEXT)
                          || (CALLSTACK & (bufio_cs_closed | bufio_cs_freed)));

            CALLSTACK &= ~(bufio_cs_active | bufio_cs_closed);
        }
    }
    else if (!i) {
        lazy_close = true;
    }
    else if (errno != EINTR
             && errno != EAGAIN)
    {
        // assert: i < 0 && errno == something_serious

        lazy_close = true;
        service->close_errno = errno;
    }

    if (lazy_close)
        fdu_bufio_close(&service->buffer);

    return fde_safe_pop_context(fdu_context_bufio, ectx);
}

static bool fdu_bufio_got_output(void* service_v, int fd)
{
    fdu_bufio_service* service = (fdu_bufio_service*) service_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_bufio)))
        return false;
    //
    if (!service
        || fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    FDE_ASSERT( service->type == bufio_output , "invalid service type" , false );
    FDE_ASSERT( fd == FD , "fd corrupted" , false );
    FDE_ASSERT( FILLED <= SIZE , "filled > size" , false );
    //

    if (!FILLED) {
        CAN_XFER = true;

        return fdd_remove_output(&service->output_service, fd)
            && fde_pop_context(fdu_context_bufio, ectx);
    }

    const int i = write(fd, DATA, FILLED);

    if (CAN_XFER) {
        CAN_XFER = false;

        if (!fdd_add_output(&service->output_service, fd))
            return false;
    }

    bool lazy_close = false;

    if (i > 0) {
        FDE_ASSERT_DEBUG( (unsigned int)i <= FILLED , "i > filled" , false );

        FILLED -= i;

        if (FILLED)
            memmove(DATA, &DATA[i], FILLED);

        if (NOTIFY) {
            CALLSTACK |= bufio_cs_active;

            lazy_close = (!NOTIFY(&service->buffer, CONTEXT)
                          || (CALLSTACK & (bufio_cs_closed | bufio_cs_freed)));

            CALLSTACK &= ~(bufio_cs_active | bufio_cs_closed);
        }
    }
    else if (errno == EPIPE) {
        lazy_close = true;
    }
    else if (errno != EINTR
             && errno != EAGAIN)
    {
        lazy_close = true;
        service->close_errno = errno;
    }

    if (lazy_close)
        fdu_bufio_close(&service->buffer);

    return fde_safe_pop_context(fdu_context_bufio, ectx);
}

// CALLSTACK undefined later
#undef CAN_XFER
#undef CONTEXT
#undef DATA
#undef FD
#undef FILLED
#undef NOTIFY
#undef SIZE

bool fdu_bufio_touch(fdu_bufio_buffer* buffer)
{
    fdu_bufio_service* service;

    if (!buffer
        || !(service = buffer->service)
        || (service->type != bufio_input
            && service->type != bufio_output))
    {
        fde_push_context(fdu_context_bufio);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    if (buffer->fd < 0)
        return false;
    if (!buffer->can_xfer)
        return true;

    const fde_node_t* ectx;

    if (!(ectx = fde_push_context(fdu_context_bufio)))
        return false;

    switch (service->type) {
    case bufio_input:
        if (buffer->filled < buffer->size)
            fdu_bufio_got_input(service, buffer->fd);
        break;
    case bufio_output:
        if (service->buffer.filled)
            fdu_bufio_got_output(service, buffer->fd);
        break;

    default: return false;
    }

    return fde_safe_pop_context(fdu_context_bufio, ectx);
}

void fdu_bufio_close(fdu_bufio_buffer* buffer)
{
    fdu_bufio_service* service;

    if (!buffer
        || !(service = buffer->service)
        || buffer->fd < 0)      // already closed
    {
        return;
    }

    FDE_ASSERT( service->type == bufio_input
                || service->type == bufio_output ,
                "invalid service type" , );

    if (CALLSTACK & bufio_cs_active) {
        CALLSTACK |= bufio_cs_closed;
        return;
    }

    const int fd = buffer->fd;

    buffer->fd = -1;

    const fde_node_t* ectx = fde_push_context(fdu_context_bufio);

    if (!buffer->can_xfer) {
        switch (service->type) {
        case bufio_input: fdd_remove_input(&service->input_service, fd); break;
        case bufio_output: fdd_remove_output(&service->output_service, fd); break;
        }
    }

    //

    CALLSTACK |= bufio_cs_active;

    service->close(&service->buffer, service->context, fd, service->close_errno);

    const bool lazy_free = (CALLSTACK & bufio_cs_freed);

    CALLSTACK &= ~(bufio_cs_active | bufio_cs_freed);

    //

    if (lazy_free)
        free(service);

    //

    if (ectx)
        fde_safe_pop_context(fdu_context_bufio, ectx);
}

void fdu_bufio_free(fdu_bufio_buffer* buffer)
{
    fdu_bufio_service* service;

    if (!buffer
        || !(service = buffer->service))
    {
        return;
    }

    if (CALLSTACK & bufio_cs_active) {
        CALLSTACK |= bufio_cs_freed;
        return;
    }

    //

    if (fdu_bufio_is_closed(buffer)) {
        free(service);
    }
    else {
        CALLSTACK |= bufio_cs_freed;
        fdu_bufio_close(buffer);       
    }
}

#undef CALLSTACK

unsigned int fdu_bufio_transfer(fdu_bufio_buffer* dst,
                                fdu_bufio_buffer* src)
{
    if (!dst
        || !src)
    {
        return 0;
    }

    const unsigned int offer = src->filled;
    const unsigned int space = dst->size - dst->filled;
    const unsigned int bytes = (offer < space) ? offer : space;

    if (!bytes)
        return 0;

    memcpy(&dst->data[dst->filled],
           src->data,
           bytes);

    dst->filled += bytes;
    src->filled -= bytes;

    if (src->filled)
        memmove(src->data, &src->data[bytes], src->filled);

    return bytes;
}

// ------------------------------------------------------------

fdu_bufio_buffer* fdu_new_input_bufio(const int fd,
                                      const unsigned int size,
                                      void* const context,
                                      const fdu_bufio_notify_func notify_callback,
                                      const fdu_bufio_close_func close_callback)
{
    const size_t total_size = sizeof_fdu_bufio_service + size;

    unsigned char* const allocated = malloc(total_size);

    if (!allocated) {
        fde_push_context(fdu_context_bufio);
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return 0;
    }

    unsigned char* counter = allocated;

    const fdu_memory_area
        service_memory  = init_memory_area_cont(&counter, sizeof_fdu_bufio_service),
        buffer_memory   = init_memory_area_cont(&counter, size);

    fdu_bufio_buffer* const bufio
        = fdu_new_input_bufio_inplace(fd,
                                      service_memory,
                                      buffer_memory,
                                      context,
                                      notify_callback,
                                      close_callback);

    if (!bufio)
        free(allocated);

    return bufio;
}

fdu_bufio_buffer* fdu_new_output_bufio(const int fd,
                                       const unsigned int size,
                                       void* const context,
                                       const fdu_bufio_notify_func notify_callback,
                                       const fdu_bufio_close_func close_callback)
{
    const size_t total_size = sizeof_fdu_bufio_service + size;

    unsigned char* const allocated = malloc(total_size);

    if (!allocated) {
        fde_push_context(fdu_context_bufio);
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return 0;
    }

    unsigned char* counter = allocated;

    const fdu_memory_area
        service_memory  = init_memory_area_cont(&counter, sizeof_fdu_bufio_service),
        buffer_memory   = init_memory_area_cont(&counter, size);

    fdu_bufio_buffer* const bufio
        = fdu_new_output_bufio_inplace(fd,
                                       service_memory,
                                       buffer_memory,
                                       context,
                                       notify_callback,
                                       close_callback);

    if (!bufio)
        free(allocated);

    return bufio;
}

fdu_bufio_buffer* fdu_new_input_bufio_inplace(const int fd,
                                              const fdu_memory_area service_memory,
                                              const fdu_memory_area buffer_memory,
                                              void* const context,
                                              const fdu_bufio_notify_func notify_callback,
                                              const fdu_bufio_close_func close_callback)
{
    const fde_node_t* ectx;
    if (!(ectx = fde_push_context(fdu_context_bufio)))
        return 0;
    //
    if (fd < 0
        || !service_memory.begin
        || !buffer_memory.begin
        || service_memory.begin > service_memory.end
        || buffer_memory.begin > buffer_memory.end)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return 0;
    }
    //

    const unsigned int service_size = service_memory.end - service_memory.begin;
    const unsigned int buffer_size  = buffer_memory.end - buffer_memory.begin;

    if (service_size < sizeof_fdu_bufio_service
        || (buffer_size
            && buffer_size < MinimumBufferSize)
        || buffer_size > MaximumBufferSize)
    {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return 0;
    }

    fdu_bufio_service* service = (fdu_bufio_service*) service_memory.begin;

    service->type        = bufio_input;
    service->context     = context;
    service->notify      = notify_callback;
    service->close       = close_callback;
    service->callstack   = 0;
    service->close_errno = 0;

    service->buffer.fd       = fd;
    service->buffer.can_xfer = false;
    service->buffer.data     = buffer_size ? buffer_memory.begin : 0;
    service->buffer.size     = buffer_size;
    service->buffer.filled   = 0;
    service->buffer.service  = service;

    fdd_init_service_input(&service->input_service,
                           service,
                           &fdu_bufio_got_input);

    if (fdd_add_input(&service->input_service, fd)) {
        if (fde_pop_context(fdu_context_bufio, ectx))
            return &service->buffer;    // <-- normal exit

        fdd_remove_input(&service->input_service, fd);
    }

    return 0;
}

fdu_bufio_buffer* fdu_new_output_bufio_inplace(const int fd,
                                               const fdu_memory_area service_memory,
                                               const fdu_memory_area buffer_memory,
                                               void* const context,
                                               const fdu_bufio_notify_func notify_callback,
                                               const fdu_bufio_close_func close_callback)
{
    const fde_node_t* ectx;
    if (!(ectx = fde_push_context(fdu_context_bufio)))
        return 0;
    //
    if (fd < 0
        || !service_memory.begin
        || !buffer_memory.begin
        || service_memory.begin > service_memory.end
        || buffer_memory.begin > buffer_memory.end)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return 0;
    }
    //

    const unsigned int service_size = service_memory.end - service_memory.begin;
    const unsigned int buffer_size  = buffer_memory.end - buffer_memory.begin;

    if (service_size < sizeof_fdu_bufio_service
        || (buffer_size
            && buffer_size < MinimumBufferSize)
        || buffer_size > MaximumBufferSize)
    {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return 0;
    }

    fdu_bufio_service* service = (fdu_bufio_service*) service_memory.begin;

    if (!service) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return 0;
    }

    service->type       = bufio_output;

    service->context    = context;
    service->notify     = notify_callback;
    service->close      = close_callback;

    service->buffer.fd          = fd;
    service->buffer.can_xfer    = false;
    service->buffer.data        = buffer_size ? buffer_memory.begin : 0;
    service->buffer.size        = buffer_size;
    service->buffer.filled      = 0;
    service->buffer.service     = service;

    fdd_init_service_output(&service->output_service,
                            service,
                            &fdu_bufio_got_output);

    if (fdd_add_output(&service->output_service, fd)) {
        if (fde_pop_context(fdu_context_bufio, ectx))
            return &service->buffer;    // <-- normal exit

        fdd_remove_output(&service->output_service, fd);
    }

    return 0;
}

/*------------------------------------------------------------
 *
 * Pending connect()
 *
 */

typedef struct {
    fdd_service_output oserv;
    fdu_notify_connect_func connect_func;
    void* context;
} fdu_pending_connect_data;

static bool fdu_pending_connect_callback(void* pcd_v,
                                         int fd)
{
    fdu_pending_connect_data* pcd = (fdu_pending_connect_data*) pcd_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_connect)))
        return false;

#ifdef FD_DEBUG
    if (!pcd
        || fd < 0)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
#endif
    //

    int connect_error;
    socklen_t len = sizeof(connect_error);

    if (!fdd_remove_output(&pcd->oserv, fd))
        return false;

    // check the real status of connect() ...

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &connect_error, &len) < 0) {
        connect_error = -1;

        fde_push_stdlib_error("getsockopt", errno);
    }

    // ... and return it with the given callback

    fdu_notify_connect_func func = pcd->connect_func;
    void* context = pcd->context;

    free(pcd);

    return func(context, fd, connect_error)
        && fde_safe_pop_context(fdu_context_connect, ectx);
}

bool fdu_pending_connect(int fd, fdu_notify_connect_func callback, void* callback_context)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_connect)))
        return false;

    // allocate and initialize service

    fdu_pending_connect_data* pcd = malloc(sizeof(fdu_pending_connect_data));

    if (!pcd) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return false;
    }

    fdd_init_service_output(&pcd->oserv,
                            pcd,                                // context
                            &fdu_pending_connect_callback);     // notify
    pcd->connect_func   = callback;
    pcd->context        = callback_context;

    // start service

    return fdd_add_output(&pcd->oserv, fd)
        && fde_safe_pop_context(fdu_context_connect, ectx);
}

/*------------------------------------------------------------
 *
 * Non-blocking DNS lookup
 *
 */

typedef struct fdu_dns_service_query_v1 {
    struct fdu_dns_service_query_v1* next;

    fdu_dnsserv_notify_func notify;
    void* context;
} fdu_dns_service_query;

typedef struct {
    fdd_service_input iserv;
    int pid, fd_in, fd_out;

    fdu_dns_service_query* head;
    fdu_dns_service_query* tail;
} fdu_dns_service;

// -----

static fdu_dns_service_query* free_dns_query_nodes = 0;
static fdu_dns_service* dns_service = 0;

// -----

static fdu_dns_service_query* new_dns_query_node(fdu_dnsserv_notify_func notify, void* ctx)
{
    fdu_dns_service_query* node;

    if (!free_dns_query_nodes)
        node = malloc(sizeof(fdu_dns_service_query));
    else {
        node = free_dns_query_nodes;
        free_dns_query_nodes = free_dns_query_nodes->next;
    }

    if (!node)
        return 0;

    node->next = 0;
    node->notify = notify;
    node->context = ctx;

    return node;
}

// -----

static void free_dns_query_node(fdu_dns_service_query* tbd)
{
    tbd->next = free_dns_query_nodes;
    free_dns_query_nodes = tbd;
}

// -----

static void fdu_dns_service_shutdown(void)
{
    const fde_node_t* ectx = fde_push_context(fdu_context_dnsserv);

    //

    const int old_pid = dns_service->pid;

    if (kill(dns_service->pid, SIGKILL) < 0)
        fde_push_stdlib_error("kill", errno);

    if (dns_service->head) {
        fdd_remove_input(&dns_service->iserv, dns_service->fd_in);

        while (dns_service->head) {
            fdu_dns_service_query* tbd = dns_service->head;
            dns_service->head = dns_service->head->next;

            tbd->notify(tbd->context, 0);

            free_dns_query_node(tbd);
        }
    }

    fdu_safe_close(dns_service->fd_in);
    fdu_safe_close(dns_service->fd_out);

    free(dns_service);
    dns_service = 0;

    if (waitpid(old_pid, 0, 0) < -1)
        fde_push_stdlib_error("waitpid", errno);

    if (ectx)
        fde_safe_pop_context(fdu_context_dnsserv, ectx);
}

// -----

static bool fdu_dns_service_got_answer(void* context, int fd)
{
    //
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_dnsserv)))
        return false;
    //
    if (context != dns_service) {
        fde_push_consistency_failure("context != dns_service");
        return false;
    }
    //

    enum { buffer_size = 256 };

    static char buffer[buffer_size];
    static int filled = 0;

    const int i = read(fd, buffer + filled, buffer_size - filled);
    if (i <= 0) {
        fde_push_stdlib_error("pipe", errno);
        fdu_dns_service_shutdown();
        return false;
    }

    filled += i;

    //

    char* ptr             = buffer;
    const char* const end = buffer + filled;

    char* entry    = 0;          // start of entry
    char* consumed = buffer;     // start of next entry

    while ((ptr =memchr(ptr, '\n', end - ptr)))
    {
        *ptr++   = 0;
        entry    = consumed;
        consumed = ptr;

        if (!dns_service->head)
        {
#ifdef FD_DEBUG
            fde_push_consistency_failure("DNS query node list empty");
            return false;
#else
            continue;
#endif
        }

        fdu_dns_service_query* current = dns_service->head;

        fdu_dnsserv_notify_func local_notify = current->notify;
        void* local_context = current->context;

        dns_service->head = dns_service->head->next;
        if (!dns_service->head) {
            dns_service->tail = 0;

            fdd_remove_input(&dns_service->iserv, dns_service->fd_in);
        }

        free_dns_query_node(current);

        //

        local_notify(local_context, *entry ? entry : 0);
    }

    if (consumed
        && consumed > buffer)
    {
        filled -= (consumed-buffer);
        if (filled)
            memmove(buffer, consumed, filled);
    }

    FDE_ASSERT_DEBUG( filled == end - consumed , "pointer math is corrupted" , false );

    return fde_safe_pop_context(fdu_context_dnsserv, ectx);
}

// -----

static bool fdu_dns_service_start(void)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_dnsserv)))
        return false;
    //

    int pid = 0;
    int fds_read[2] = {-1, -1};
    int fds_write[2] = {-1, -1};

    // create pipes for communication

    if (pipe(fds_read) != 0
        || pipe(fds_write) != 0)
    {
        fde_push_stdlib_error("pipe", errno);

        if (fds_read[0] >= 0) fdu_safe_close(fds_read[0]);
        if (fds_read[1] >= 0) fdu_safe_close(fds_read[1]);
        if (fds_write[0] >= 0) fdu_safe_close(fds_write[0]);
        if (fds_write[1] >= 0) fdu_safe_close(fds_write[1]);
        return false;
    }

    // spawn process

    if ((pid =vfork()) < 0) {
        fde_push_stdlib_error("vfork", errno);

        fdu_safe_close(fds_read[0]);
        fdu_safe_close(fds_read[1]);
        fdu_safe_close(fds_write[0]);
        fdu_safe_close(fds_write[1]);
        return false;
    }
    else if (pid == 0) // child
    {
        fdu_safe_close(fds_read[0]);
        fdu_safe_close(fds_write[1]);

        if (fdu_move_fd(fds_write[0], STDIN_FILENO)
            && fdu_move_fd(fds_read[1], STDOUT_FILENO))
        {
            execl("./dns-service", "dns-service", (char*) 0);
        }

        _exit(-1);
    }

    // parent

    fdu_safe_close(fds_read[1]);
    fdu_safe_close(fds_write[0]);

    /* verify operation:
     * dns-service replies to invalid requests with empty line, so
     * simple '\n' should be replied with '\n'
     */

    {
        char buffer[2];

        if (write(fds_write[1], "\n", 1) != 1
            || read(fds_read[0], buffer, 1) != 1
            || buffer[0] != '\n')
        {
            fde_push_resource_failure("dns_service:startup test");

            fdu_safe_close(fds_read[0]);
            fdu_safe_close(fds_write[1]);

            kill(pid, SIGKILL);
            waitpid(pid, 0, 0);
            return false;
        }
    }

    // init dns_service struct

    dns_service = malloc(sizeof(fdu_dns_service));

    if (!dns_service) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_dns_service_shutdown();
        return false;
    }

    fdd_init_service_input(&dns_service->iserv, dns_service, &fdu_dns_service_got_answer);

    dns_service->head   = 0;
    dns_service->tail   = 0;
    dns_service->pid    = pid;
    dns_service->fd_in  = fds_read[0];
    dns_service->fd_out = fds_write[1];

    return fde_safe_pop_context(fdu_context_dnsserv, ectx);
}

bool fdu_dnsserv_lookup(const char* name, fdu_dnsserv_notify_func notify, void* ctx)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_dnsserv)))
        return false;
    //

    const unsigned int name_len = name ? strlen(name) : 0;

    if (!name
        || name_len <= 2
        || !notify)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    //

    if (!dns_service
        && !fdu_dns_service_start()
        && !dns_service)
    {
        fde_push_resource_failure("dns_service:startup");
        return false;
    }

    // send dns-name to dns-service

    enum { tmp_buffer_size = 255 };

    if (name_len >= tmp_buffer_size) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    unsigned char buffer[tmp_buffer_size +1];

    memcpy(buffer, name, name_len);
    buffer[name_len] = '\n';

    const bool write_ok = fdu_safe_write(dns_service->fd_out, buffer, buffer + name_len + 1);

    //

    if (!write_ok) {
        fde_push_resource_failure("dns_service:write");
        fdu_dns_service_shutdown();
        return false;
    }

    // create new query

    fdu_dns_service_query* new_query = new_dns_query_node(notify, ctx);

    if (!new_query) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        return false;
    }

    // add it to the list

    if (dns_service->tail)
    {
        dns_service->tail->next = new_query;
        dns_service->tail = new_query;
    }
    else {
        dns_service->head = new_query;
        dns_service->tail = new_query;

        fdd_add_input(&dns_service->iserv, dns_service->fd_in);
    }

    return fde_safe_pop_context(fdu_context_dnsserv, ectx);
}

/*------------------------------------------------------------
 *
 * Auto-accept connection (aac)
 *
 */

struct aac_service_s {
    fdd_service_input listening_service;
    fdd_notify_func callback;
    void* callback_context;
    int server_fd;
};

static bool fdu_aac_new_connection(void* service_v,
                                   int server_fd)
{
    aac_service_t* service = (aac_service_t*) service_v;

    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_aac)))
        return false;
    //

    int new_socket;

 retry:
    new_socket = accept(server_fd, 0, 0);

    if (new_socket < 0) {
        if (errno == EINTR
            || errno == EAGAIN)
        {
            goto retry;
        }

        fde_push_stdlib_error("accept", errno);

        fdd_remove_input(&service->listening_service, server_fd);
        fdu_safe_close(server_fd);
        free(service);
        return false;
    }

    return service->callback(service->callback_context, new_socket)
        && fde_pop_context(fdu_context_aac, ectx);
}

aac_service_t* fdu_auto_accept_connection(int server_fd,
                                          fdd_notify_func callback,
                                          void* callback_context)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_aac)))
        return 0;
    //
    if (server_fd < 0
        || !callback)
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        fdu_safe_close(server_fd);
        return 0;
    }
    //

    aac_service_t* service = malloc(sizeof(aac_service_t));

    if (!service) {
        fde_push_resource_failure_id(fde_resource_memory_allocation);
        fdu_safe_close(server_fd);
        return 0;
    }

    service->callback         = callback;
    service->callback_context = callback_context;
    service->server_fd        = server_fd;

    fdd_init_service_input(&service->listening_service,
                           service,
                           &fdu_aac_new_connection);

    if (!fdd_add_input(&service->listening_service, server_fd)) {
        free(service);
        fdu_safe_close(server_fd);
        return 0;
    }

    fde_pop_context(fdu_context_aac, ectx);
    return service;
}

bool fdu_close_auto_accept(aac_service_t* service)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_aac)))
        return false;
    //
    if (!service) {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    fdd_remove_input(&service->listening_service, service->server_fd);
    free(service);
    fdu_safe_close(service->server_fd);

    return fde_safe_pop_context(fdu_context_aac, ectx);
}

/*------------------------------------------------------------
 *
 * General utilities
 *
 */

int fdu_listen_inet4(unsigned short port, unsigned int options)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_listen)))
        return -1;
    //

    if (!port
        || (options & ~(FDU_SOCKET_UDP
                        |FDU_SOCKET_LOCAL
                        |FDU_SOCKET_NOREUSE
                        |FDU_SOCKET_BROADCAST))
        || (options & FDU_SOCKET_BROADCAST
            && (options & (FDU_SOCKET_UDP|FDU_SOCKET_LOCAL)) != FDU_SOCKET_UDP))
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return -1;
    }

    // open socket

    const int socketfd = socket(PF_INET,
                                (options & FDU_SOCKET_UDP) ? SOCK_DGRAM : SOCK_STREAM,
                                0);
    if (socketfd < 0) {
        fde_push_stdlib_error("socket", errno);
        return -1;
    }

    // optionally set SO_REUSEADDR for faster restart

    if (!(options & FDU_SOCKET_NOREUSE))
    {
        int parameter = 1;
        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR,
                       &parameter, sizeof(parameter)) < 0)
        {
            fde_push_stdlib_error("setsockopt(SO_REUSEADDR)", errno);
            fdu_safe_close(socketfd);
            return -1;
        }
    }

    // optionally set SO_BROADCAST

    if (options & FDU_SOCKET_BROADCAST)
    {
        int parameter = 1;
        if (setsockopt(socketfd, SOL_SOCKET, SO_BROADCAST,
                       &parameter, sizeof(parameter)) < 0)
        {
            fde_push_stdlib_error("setsockopt(SO_BROADCAST)", errno);
            fdu_safe_close(socketfd);
            return -1;
        }
    }

    // bind

    {
        struct sockaddr_in sa;

        memset(&sa, 0, sizeof(struct sockaddr_in));
        sa.sin_family      = AF_INET;
        sa.sin_port        = htons(port);
        sa.sin_addr.s_addr = (options & FDU_SOCKET_LOCAL) ? htonl(INADDR_LOOPBACK) : INADDR_ANY;

        if (bind(socketfd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
            fde_push_stdlib_error("bind", errno);
            fdu_safe_close(socketfd);
            return -1;
        }
    }

    // listen

    if (!(options & FDU_SOCKET_UDP)) {
        if (listen(socketfd, 64) == -1) {
            fde_push_stdlib_error("listen", errno);
            fdu_safe_close(socketfd);
            return -1;
        }
    }

    if (!fde_safe_pop_context(fdu_context_listen, ectx)) {
        fdu_safe_close(socketfd);
        return -1;
    }

    return socketfd;
}

int fdu_listen_unix(const char* path, unsigned int options)
{
    const fde_node_t* ectx = 0;
    if (!(ectx =fde_push_context(fdu_context_listen)))
        return -1;
    //
    if (!path
        || !path[0]
        || (options & ~(FDU_SOCKET_UDP)))
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return -1;
    }
    //

    const int socketfd = socket(PF_UNIX,
                                (options & FDU_SOCKET_UDP) ? SOCK_DGRAM : SOCK_STREAM,
                                0);
    if (socketfd < 0) {
        fde_push_stdlib_error("socket", errno);
        return -1;
    }

    // bind

    {
        struct sockaddr_un sa;

        memset(&sa, 0, sizeof(struct sockaddr_un));
        sa.sun_family = AF_UNIX;

        strncpy(sa.sun_path, path, UNIX_PATH_MAX);
        sa.sun_path[UNIX_PATH_MAX-1] = 0;

        if (bind(socketfd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
            fde_push_stdlib_error("bind", errno);
            fdu_safe_close(socketfd);
            return -1;
        }
    }

    // listen

    if (!(options & FDU_SOCKET_UDP)) {
        if (listen(socketfd, 64) == -1) {
            fde_push_stdlib_error("listen", errno);
            fdu_safe_close(socketfd);
            return -1;
        }
    }

    if (!fde_safe_pop_context(fdu_context_listen, ectx)) {
        fdu_safe_close(socketfd);
        return -1;
    }

    return socketfd;
}

bool fdu_lazy_connect(struct sockaddr_in* addr,
                      fdu_notify_connect_func connect_func,
                      void* context,
                      unsigned int options)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_connect)))
        return false;
    //
    if (!addr
        || !connect_func
        || (options & ~(FDU_SOCKET_UDP)))
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    int socketfd = socket(PF_INET,
                          (options & FDU_SOCKET_UDP) ? SOCK_DGRAM : SOCK_STREAM,
                          0);
    if (socketfd < 0) {
        fde_push_stdlib_error("socket", errno);
        return false;
    }

    // set O_NONBLOCK

    int flags;

    if ((flags =fcntl(socketfd, F_GETFL)) == -1) {
        fde_push_stdlib_error("fcntl(F_GETFL)", errno);
        fdu_safe_close(socketfd);
        return false;
    }

    if ((flags & O_NONBLOCK) == 0) {
        if (fcntl(socketfd, F_SETFL, flags|O_NONBLOCK) == -1) {
            fde_push_stdlib_error("fcntl(F_SETFL)", errno);
            fdu_safe_close(socketfd);
            return false;
        }
    }

    // the actual connect

    int connect_errno  = 0;
    bool close_succeed = true;

    if (connect(socketfd, (struct sockaddr*) addr, sizeof(struct sockaddr_in)) != 0) {
        if (errno == EINPROGRESS) {
            fdu_pending_connect(socketfd, connect_func, context);
            return true;
        }
        connect_errno = errno;

        close_succeed = fdu_safe_close(socketfd);
        socketfd = -1;
    }

    return connect_func(context, socketfd, connect_errno)
        && close_succeed
        && fde_pop_context(fdu_context_connect, ectx);
}

// ------------------------------------------------------------

bool fdu_safe_read(int fd, unsigned char* start, const unsigned char* const end)
{
    if (fd < 0
        || !start
        || !end)
    {
        fde_push_context(fdu_context_safe);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    while (start < end) //sic
    {
        const int i = read(fd, start, end - start);

        if (i > 0) {
            start += i;
        }
        else if (!i
                 || (errno != EINTR
                     && errno != EAGAIN))
        {
            fde_push_context(fdu_context_safe);
            fde_push_stdlib_error("read", errno);
            return false;
        }
    }

    return true;
}

bool fdu_safe_write(int fd, const unsigned char* start, const unsigned char* const end)
{
    if (fd < 0
        || !start
        || !end)
    {
        fde_push_context(fdu_context_safe);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

    while (start < end)
    {
        const int i = write(fd, start, end-start);

        if (i > 0) {
            start += i;
        }
        else if (errno != EINTR
                 && errno != EAGAIN)
        {
            fde_push_context(fdu_context_safe);
            fde_push_stdlib_error("write", errno);
            return false;
        }
    }

    return true;
}

bool fdu_safe_write_str(int fd, const unsigned char* buffer)
{
    return fdu_safe_write(fd,
                          buffer,
                          buffer ? (buffer+strlen((const char*) buffer)) : 0);
}

bool fdu_safe_close(int fd)
{
    if (fd < 0) {
        fde_push_context(fdu_context_safe);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }
    //

 retry:
    if (close(fd) == 0)
        return true;
    else if (errno == EINTR)
        goto retry;
    else {
        fde_push_context(fdu_context_safe);
        fde_push_stdlib_error("close", errno);
        return false;
    }
}

bool fdu_safe_chdir(const char* new_dir)
{
    if (!new_dir
        || !*new_dir)
    {
        fde_push_context(fdu_context_safe);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    if (chdir(new_dir) != 0) {
        fde_push_context(fdu_context_safe);
        fde_push_stdlib_error("chdir", errno);
        return false;
    }

    return true;
}

bool fdu_copy_fd(int oldfd, int newfd)
{
    if (oldfd < 0
        || newfd < 0)
    {
        fde_push_context(fdu_context_safe);
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    //

    if (oldfd == newfd)
        return true;

    for (;;) {
        if (dup2(oldfd, newfd) >= 0)
            return true;
        else if (errno != EINTR) {
            fde_push_context(fdu_context_safe);
            fde_push_stdlib_error("dup2", errno);
            return false;
        }
    }
}

bool fdu_move_fd(int oldfd, int newfd)
{
    return oldfd == newfd
        || (fdu_copy_fd(oldfd, newfd)
            && fdu_safe_close(oldfd));
}

bool fdu_pidfile(const char* filename, int options)
{
    const fde_node_t* ectx;
    if (!(ectx =fde_push_context(fdu_context_pidfile)))
        return false;
    //
    if (!filename
        || !filename[0]
        || (options & ~(FDU_PIDFILE_ONLYCHECK)))
    {
        fde_push_consistency_failure_id(fde_consistency_invalid_arguments);
        return false;
    }

    /* open file
     *
     * flags must not modify the file if it exists
     * (advisory lock doesn't apply yet)
     */

    const int fd = open(filename, O_RDWR|O_CREAT|O_CLOEXEC, 0600);
    if (fd < 0) {
        fde_push_stdlib_error("open", errno);
        return false;
    }

    // lock the file

    {
        struct flock flock;

        flock.l_type   = F_WRLCK;
        flock.l_whence = SEEK_SET;
        flock.l_start  = 0;
        flock.l_len    = 0;

        if (fcntl(fd, F_SETLK, &flock) < 0) {
            fde_push_stdlib_error("fcntl", errno);
            fdu_safe_close(fd);
            return false;
        }
    }

    if (options & FDU_PIDFILE_ONLYCHECK) {
        return fdu_safe_close(fd)
            && fde_safe_pop_context(fdu_context_pidfile, ectx);
    }

    // write pid and truncate

    {
        enum { pid_buffer_size = 20 };

        unsigned char buf[pid_buffer_size];
        struct stat st;

        const int len = snprintf((char*) buf, pid_buffer_size, "%u\n", getpid());
        if (len <= 1) {
            fde_push_stdlib_error("snprintf", 0);
            fdu_safe_close(fd);
            return false;
        }

        if (!fdu_safe_write(fd, buf, buf+len)) {
            fdu_safe_close(fd);
            return false;
        }

        if (fstat(fd, &st) != 0) {
            fde_push_stdlib_error("fstat", errno);
            fdu_safe_close(fd);
            return false;
        }

        if (st.st_size != len
            && ftruncate(fd, len) < 0)
        {
            fde_push_stdlib_error("ftruncate", errno);
            fdu_safe_close(fd);
            return false;
        }
    }

    return fde_pop_context(fdu_context_pidfile, ectx);      // don't close fd
}
