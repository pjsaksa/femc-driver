# Femc Driver
# Copyright (C) 2020 Pauli Saksa
#
# Licensed under The MIT License, see file LICENSE.txt in this source tree.

cmake_minimum_required(VERSION 3.0)
project(femc-driver)

#
# femc-driver
#

add_library(femc-driver STATIC
    can.c
    dispatcher.c
    dispatcher_select.c
    #dispatcher_zmq.c
    error_stack.c
    http.c
    s11n.c
    task_queue.c
    utils.c
)
target_compile_options(femc-driver PRIVATE -O2 -Wall -Wextra -Werror)
target_compile_definitions(femc-driver PUBLIC -DFD_DEBUG)
set_source_files_properties(dispatcher_zmq.c
    PROPERTIES
        INCLUDE_DIRECTORIES "???"
        COMPILE_DEFINITIONS "ZMQ_BUILD_DRAFT_API=1"
)

#
# dns-service
#

add_executable(dns-service
    dns-service.c
)
target_compile_options(dns-service PRIVATE -O2 -Wall -Wextra -Werror)

#
# femc-app
#

add_library(femc-app STATIC
    app/babysitter.c
    #app/site/parser.c
    #app/site/site.c
)
target_compile_options(femc-app PRIVATE -O2 -Wall -Wextra -Werror)

add_subdirectory(demo)
