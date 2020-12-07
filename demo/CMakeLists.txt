#
# main-demo
#

add_executable(main-demo
    date.c
    echo1.c
    echo2.c
    echo3.c
    main.c
    route.c
    timer1.c
)
target_compile_options(main-demo PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(main-demo femc-driver)

#
# task_queue
#

add_executable(task_queue
    task_queue.c
)
target_compile_options(task_queue PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(task_queue femc-driver)

#
# endian
#

add_executable(endian
    endian.c
)
target_compile_options(endian PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(endian femc-driver)

#
# write2socket
#

add_executable(write2socket
    write2socket.c
)
target_compile_options(write2socket PRIVATE -O2 -Wall -Wextra -Werror)

#
# babysitter
#

add_executable(babysitter
    babysitter-main.c
)
target_compile_options(babysitter PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(babysitter femc-driver femc-app)

#
# babysitter-program
#

add_executable(babysitter-program
    babysitter-program.c
)
target_compile_options(babysitter-program PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(babysitter-program femc-driver)
