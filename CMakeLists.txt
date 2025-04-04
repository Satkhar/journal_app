cmake_minimum_required(VERSION 3.10.0)
project(journal_app VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Этот проект требует GCC. Используйте GCC для сборки.")
endif()

# Указываем использование vcpkg
# set(CMAKE_TOOLCHAIN_FILE "/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
# "-DCMAKE_TOOLCHAIN_FILE=D:/Work/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"

# Проверяем наличие Qt6
# find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Sql)
find_package(Qt6 COMPONENTS Core Gui Widgets Sql)

if(NOT Qt6_FOUND)
    message(STATUS "Qt6 not found. Installing via vcpkg...")

    # Вызываем vcpkg для установки Qt6
    execute_process(
        # COMMAND /path/to/vcpkg/vcpkg install qt6  # или qt?
        # COMMAND vcpkg install qt
        COMMAND vcpkg install qt6-base qt6-tools
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE result
    )

    # if(NOT result EQUAL 0)
    #     message(FATAL_ERROR "Failed to install Qt6 via vcpkg.")
    # endif()

    # Повторно ищем Qt6 после установки
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON) # Автоматическая обработка .ui файлов
set(CMAKE_AUTORCC ON)

add_executable(journal_app 
    ./App/Src/main.cpp
    ./App/Src/mainwindow.cpp


    ./App/Inc/journal_app.h
    ./App/Inc/mainwindow.hpp
    ./App/Inc/dbManager.hpp
    ./App/Inc/checkTableManager.hpp
    ./App/Inc/mainTableManager.hpp

    ./journal_app.ui
    )

target_include_directories(journal_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/App/Inc
    ${CMAKE_SOURCE_DIR}/App/Src)
    
target_link_libraries(journal_app PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets
    Qt6::Sql)

target_include_directories(journal_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/App/Inc
    ${CMAKE_SOURCE_DIR}/App/Src)

    # Указываем флаги компилятора для поддержки UTF-8
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

include(CTest)
enable_testing()

# option(USE_CLANG_TIDY "Enable clang-tidy for static analysis" ON)	#OFF ON

# if(USE_CLANG_TIDY)
#     find_program(CLANG-TIDY_PATH NAMES clang-tidy REQUIRED)
#     message(STATUS "Found clang-tidy: ${CLANG-TIDY_PATH}")
#     # set_target_properties(${OAM_CONFIGURATOR_CLIENT}
#     set_target_properties(journal_app
#         PROPERTIES
#             # CXX_CLANG_TIDY ${CLANG-TIDY_PATH} #;"--config-file=.clang-tidy;--warnings-as-errors=*"
#             CXX_CLANG_TIDY "${CLANG-TIDY_PATH};--checks=*;"

#     )
# endif()