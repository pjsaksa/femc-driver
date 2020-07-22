# Femc Driver
# Copyright (C) 2020 Pauli Saksa
#
# Licensed under The MIT License, see file LICENSE.txt in this source tree.

set(CMAKE_CXX_STANDARD 17)

add_executable(sample1 sample1.cc)
target_compile_options(sample1 PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(sample1 femc-driver)

add_executable(sample2 sample2.cc)
target_compile_options(sample2 PRIVATE -O2 -Wall -Wextra -Werror)
target_link_libraries(sample2 femc-driver)

add_executable(sample3 sample3.cc)
target_compile_options(sample3 PRIVATE -O3 -Wall -Wextra -Werror -fdiagnostics-color=always)
target_link_libraries(sample3 femc-driver)
