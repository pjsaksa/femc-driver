cmake_minimum_required(VERSION 3.0)
project(femc-driver)

add_library(femc-driver STATIC
    can.c
    dispatcher.c
    dispatcher_select.c
    error_stack.c
    http.c
    s11n.c
    utils.c
)
target_compile_options(femc-driver PRIVATE -O2 -Wall -Wextra -Werror)

add_executable(dns-service
    dns-service.c
)
target_compile_options(dns-service PRIVATE -O2 -Wall -Wextra -Werror)

add_library(femc-app STATIC
    app/babysitter.c
    #app/site/parser.c
    #app/site/site.c
)
target_compile_options(femc-app PRIVATE -O2 -Wall -Wextra -Werror)

add_subdirectory(demo)
