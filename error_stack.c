/* Femc Driver
 * Copyright (C) 2013-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#include "error_stack.h"
#include "generic.h"

#include <string.h>

#ifdef FD_TRACE
#include <errno.h>
#endif

enum { error_stack_size =64 };

static fde_node_t error_stack[error_stack_size];
static fde_node_t *top =error_stack;

static uint32_t errors =0;
static uint32_t meta_errors =0;
static bool error_stack_full =false;

// *****

static bool internal_push(fde_node_type_t type, const char *par1, uint32_t par2)
{
    if (top - error_stack >= error_stack_size) {
        ++meta_errors;
        error_stack_full =true;
        return false;
    }

    top->type =type;
    top->message =par1;
    top->id =par2;
    ++top;

    //

    switch (type) {
    case fde_node_context:
    case fde_node_message:
        break;

    case fde_node_stdlib_error:
    case fde_node_consistency_failure:
    case fde_node_data_corruption:
    case fde_node_resource_failure:
        //
    case fde_node_http_error:
        ++errors;
        break;

    case fde_node_meta_error:
        ++meta_errors;
        break;

    default:
        internal_push(fde_node_meta_error, "invalid node type", 0);
        return false;
    }

    return true;
}

// *****

#ifdef FD_TRACE
static unsigned int indent =1;
#endif

const fde_node_t *fde_push_context_(uint32_t context, const char *function)
{
    const fde_node_t *ptr =top;

#ifdef FD_TRACE
    const int tmp_errno =errno;
    fprintf(FDD_ACTIVE_LOGFILE, "%*c> %s\n", 2*indent++, ' ', function);
    errno =tmp_errno;
#endif

    return internal_push(fde_node_context, function, context) ? ptr : 0;
}

bool fde_push_stdlib_error(const char *function, int err)
{
    return internal_push(fde_node_stdlib_error, function, err);
}

bool fde_push_consistency_failure(const char *label)
{
    return internal_push(fde_node_consistency_failure, label, 0);
}

bool fde_push_consistency_failure_id(uint32_t id)
{
    return internal_push(fde_node_consistency_failure, 0, id);
}

bool fde_push_data_corruption(const char *label)
{
    return internal_push(fde_node_data_corruption, label, 0);
}

bool fde_push_resource_failure(const char *label)
{
    return internal_push(fde_node_resource_failure, label, 0);
}

bool fde_push_resource_failure_id(uint32_t res)
{
    return internal_push(fde_node_resource_failure, 0, res);
}

bool fde_push_message(const char *message)
{
    return internal_push(fde_node_message, message, 0);
}

bool fde_push_node(const fde_node_t *node)
{
    return internal_push(node->type, node->message, node->id);
}

// custom errors

bool fde_push_http_error(const char *message, int code)
{
    return internal_push(fde_node_http_error, message, code);
}

// *****

uint32_t fde_errors(void)       { return errors + meta_errors; }
uint32_t fde_meta_errors(void)  { return meta_errors; }

// *****

bool fde_pop_context(uint32_t context, const fde_node_t *stack_ptr)
{
    fde_node_t *new_top =top;

#ifdef FD_TRACE
    fprintf(FDD_ACTIVE_LOGFILE, "%*c<\n", 2*--indent, ' ');
    if (!indent) {
        fprintf(FDD_ACTIVE_LOGFILE, "TRACE: indent == 0\n");
        ++errors;
    }
#endif

    while ((new_top =fde_get_next_error(fde_node_context_b, new_top)))
    {
        if (new_top->type != fde_node_context)
        {
            internal_push(fde_node_meta_error, "fde_get_next_error() returned node of invalid type", 0);
            return false;
        }

        if (new_top->id == context
            && (!stack_ptr
                || new_top == stack_ptr))
        {
            for (fde_node_t *ptr =new_top;
                 ptr < top;
                 ++ptr)
            {
                switch (ptr->type) {
                case fde_node_stdlib_error:
                case fde_node_consistency_failure:
                case fde_node_data_corruption:
                case fde_node_resource_failure:
                    //
                case fde_node_http_error:
                    --errors;
                    break;

                case fde_node_meta_error:
                    --meta_errors;
                    break;
                }
            }

            top =new_top;
            return true;
        }
    }

    internal_push(fde_node_meta_error, "pop_context called with invalid context", 0);
    return false;
}

bool fde_reset_context(uint32_t context, const fde_node_t *stack_ptr)
{
#ifdef FD_TRACE
    fprintf(FDD_ACTIVE_LOGFILE, "%*c+\n", 2*indent++, ' ');
#endif

    if (fde_pop_context(context, stack_ptr)) {
        ++top;
        return true;
    }

    return false;
}

fde_node_t *fde_get_last_error(uint32_t types)
{
    return fde_get_next_error(types, top);
}

fde_node_t *fde_get_next_error(uint32_t types, fde_node_t *last)
{
    fde_node_t *ptr =last;

    while (true) {
        if (ptr == error_stack)
            return 0;

        --ptr;

        if (types & (1 << ptr->type))
            return ptr;
    }
}

void fde_for_each_node(uint32_t types,
                       fde_node_callback_func callback,
                       void *callback_context)
{
    fde_node_t *ptr =error_stack;

    while (ptr < top) {
        if (types & (1 << ptr->type))
            callback(ptr, callback_context);

        ++ptr;
    }
}

const char *fde_custom_context_id_to_name(fde_context_id_t) WEAK_LINKAGE;
const char *fde_custom_context_id_to_name(fde_context_id_t UNUSED(context))
{
    return "define: const char *fde_custom_context_id_to_name(fde_context_id_t)";
}

const char *fde_context_id_to_name(fde_context_id_t context)
{
    if (context >= fde_first_custom_context
        && context <= fde_last_custom_context)
    {
        return fde_custom_context_id_to_name(context);
    }

#ifdef FD_DEBUG
    switch (context) {
    case fdd_context_main:        return "driver dispatcher";
        //
    case fdu_context_aac:         return "utils auto-accept connection";
    case fdu_context_bufio:       return "utils buf-io";
    case fdu_context_can:         return "driver CANbus";
    case fdu_context_connect:     return "utils connect";
    case fdu_context_dnsserv:     return "utils dns service";
    case fdu_context_http:        return "driver HTTP";
    case fdu_context_listen:      return "utils listen";
    case fdu_context_pidfile:     return "utils pid file";
    case fdu_context_s11n:        return "driver s11n";
    case fdu_context_safe:        return "utils safe functions";
        //
    case fd_app_babysitter:       return "app babysitter";
        //
    case fd_demo_context_date:    return "demo date";
    case fd_demo_context_echo1:   return "demo echo1";
    case fd_demo_context_echo2:   return "demo echo2";
    case fd_demo_context_echo3:   return "demo echo3";
    case fd_demo_context_route:   return "demo route";
    case fd_demo_context_timer1:  return "demo timer1";
        //
    case fdb_context_base:              return "DBMS base";
    case fdb_context_s11n:              return "DBMS s11n";
    case fdb_context_filter:            return "DBMS filter";
    case fdb_context_storage_file:      return "DBMS storage - file";
    case fdb_context_storage_file_mmap: return "DBMS storage - file mmap";
    case fdb_context_storage_tcp:       return "DBMS storage - tcp";
        //
    default: break;
    }
#endif

    return 0;
}

static void print_node(const fde_node_t *node, FILE *output)
{
    switch (node->type) {
    case fde_node_stdlib_error:
        fprintf(output, "<stdlib error> '%s' returned \"%s\"\n", node->message, strerror(node->id));
        break;

    case fde_node_consistency_failure:
        fprintf(output, "<consistency failure> ");

        switch (node->id) {
        case fde_consistency_invalid_arguments: fprintf(output, "invalid arguments\n"); break;
        case fde_consistency_io_handler_corrupted: fprintf(output, "dispatcher: io handler corrupted\n"); break;
        case 0: fprintf(output, "%s\n", node->message); break;
        default: fprintf(output, "(unknown consistency id: %u)\n", node->id); break;
        }
        break;


    case fde_node_resource_failure:
        fprintf(output, "<resource failure> ");

        switch (node->id) {
        case fde_resource_memory_allocation: fprintf(output, "memory allocation failure\n"); break;
        case fde_resource_buffer_overflow: fprintf(output, "buffer overflow\n"); break;
        case fde_resource_buffer_underflow: fprintf(output, "buffer underflow\n"); break;
        case 0: fprintf(output, "%s\n", node->message); break;
        default: fprintf(output, "unknown\n"); break;
        }
        break;

    case fde_node_context:
        {
            const char *ctx_name =fde_context_id_to_name(node->id);

            if (ctx_name) {
                fprintf(output, "In '%s'", ctx_name);
            }
            else {
                fprintf(output, "In context #%u", node->id);
            }

            fprintf(output, " function '%s'\n", node->message);
        }
        break;

    case fde_node_data_corruption:      fprintf(output, "<data corruption> %s\n", node->message);       break;
    case fde_node_message:              fprintf(output, "<message> %s\n", node->message);               break;
    case fde_node_meta_error:           fprintf(output, "<meta error> %s\n", node->message);            break;
    default:                            fprintf(output, "<unknown>\n");                                 break;
        //
    case fde_node_http_error:           fprintf(output, "<http error> %u %s\n", node->id, node->message); break;
    }
}

void fde_print_stack(FILE *output)
{
    fde_for_each_node(fde_node_all_b, (fde_node_callback_func)print_node, output);
}
