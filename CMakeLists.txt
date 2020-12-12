cmake_minimum_required(VERSION 3.14)
project(dabar
        LANGUAGES C
        )

add_compile_options(-Wall -Wextra)

add_executable(dabar dabar.c dabar-common.c)
target_link_libraries(dabar X11 Xi)

add_executable(dabar-autolock dabar-autolock.c dabar-common.c)
target_link_libraries(dabar-autolock X11 Xi)
