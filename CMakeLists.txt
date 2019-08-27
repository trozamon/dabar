cmake_minimum_required(VERSION 3.14)
project(dabar
        LANGUAGES C
        )

add_compile_options(-Wall -Wextra)

add_executable(dabar dabar.c)
