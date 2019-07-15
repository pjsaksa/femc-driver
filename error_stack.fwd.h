/* Femc Driver
 * Copyright (C) 2013-2018 Pauli Saksa
 *
 * Licensed under The MIT License, see file LICENSE.txt in this source tree.
 */

#ifndef FEMC_DRIVER_ERROR_STACK_FWD_HEADER
#define FEMC_DRIVER_ERROR_STACK_FWD_HEADER

typedef enum {
    fdd_context_main = 1,
    //
    fdu_context_aac,
    fdu_context_bufio,
    fdu_context_can,
    fdu_context_connect,
    fdu_context_dnsserv,
    fdu_context_http,
    fdu_context_listen,
    fdu_context_pidfile,
    fdu_context_s11n,
    fdu_context_safe,
    //
    fda_babysitter,
    fda_site_main,
    fda_site_parser,
    //
    fd_demo_context_date,
    fd_demo_context_echo1,
    fd_demo_context_echo2,
    fd_demo_context_echo3,
    fd_demo_context_route,
    fd_demo_context_timer1,
    //
    fdb_context_base,
    fdb_context_s11n,
    fdb_context_filter,
    fdb_context_storage_file,
    fdb_context_storage_file_mmap,
    fdb_context_storage_tcp,
    //
    fde_first_custom_context = 1024,
    fde_last_custom_context  = 65535,
} fde_context_id_t;

// -----

enum {                                  // 'message' and 'id' contain:
    fde_node_context,                   // function     context
    fde_node_stdlib_error,              // function     errno
    fde_node_consistency_failure,       // message      id      (EITHER/OR)
    fde_node_data_corruption,           // message      -
    fde_node_resource_failure,          // message      id      (EITHER/OR)
    fde_node_message,                   // message      -
    fde_node_meta_error,                // message      -
    //
    fde_node_http_error,                // status message and code
};

enum {
    fde_node_context_b                  = 1 << fde_node_context,
    fde_node_stdlib_error_b             = 1 << fde_node_stdlib_error,
    fde_node_consistency_failure_b      = 1 << fde_node_consistency_failure,
    fde_node_data_corruption_b          = 1 << fde_node_data_corruption,
    fde_node_resource_failure_b         = 1 << fde_node_resource_failure,
    fde_node_message_b                  = 1 << fde_node_message,
    fde_node_meta_error_b               = 1 << fde_node_meta_error,
    //
    fde_node_http_error_b               = 1 << fde_node_http_error,

    fde_node_errors_b           = (fde_node_stdlib_error_b
                                   |fde_node_consistency_failure_b
                                   |fde_node_data_corruption_b
                                   |fde_node_resource_failure_b
                                   |fde_node_message_b
                                   |fde_node_meta_error_b
                                   |fde_node_http_error_b),
    fde_node_all_b              = (fde_node_context_b
                                   |fde_node_errors_b),
};

enum {
    fde_consistency_invalid_arguments = 1,

    fde_consistency_io_handler_corrupted,       // dispatcher
    fde_consistency_kill_recurring_timer,

    //
    fde_first_local_consistency_id,
};

enum {
    fde_resource_memory_allocation = 1,
    fde_resource_buffer_overflow,
    fde_resource_buffer_underflow,
};

#endif
