cmake_minimum_required(VERSION 3.16)
project(journal_app VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Этот проект требует GCC. Используйте GCC для сборки.")
endif()

# Установка зависимостей (Qt6, toolchain и т.д.) должна выполняться вне CMake.
# Например, через пакетный менеджер локально или в CI (GitHub Actions).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON) # Автоматическая обработка .ui файлов
set(CMAKE_AUTORCC ON)

add_executable(journal_app 
    ./App/Src/main.cpp
    ./App/Src/mainwindow.cpp
    ./App/Src/JournalApp.cpp
    ./App/Src/JournalLocal.cpp
    ./App/Src/JournalRemote.cpp
    ./App/Src/SyncService.cpp
    ./App/Src/SqliteConnect.cpp
    ./App/Src/checkTableManager.cpp
    ./App/Src/dbManager.cpp
    ./App/Src/mainTableManager.cpp



    ./App/Inc/journal_app.h
    ./App/Inc/mainwindow.hpp
    ./App/Inc/JournalApp.hpp
    ./App/Inc/JournalLocal.hpp
    ./App/Inc/JournalRemote.hpp
    ./App/Inc/SyncService.hpp
    ./App/Inc/IJournalStorage.hpp
    ./App/Inc/SqliteConnect.hpp
    ./App/Inc/dbManager.hpp
    ./App/Inc/checkTableManager.hpp
    ./App/Inc/mainTableManager.hpp

    ./journal_app.ui
    )

target_link_libraries(journal_app PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network)

target_include_directories(journal_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/App/Inc
    ${CMAKE_SOURCE_DIR}/App/Src)

# Указываем флаги компилятора для поддержки UTF-8
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
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
