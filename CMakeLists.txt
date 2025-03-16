cmake_minimum_required(VERSION 3.10.0)
project(journal_app VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON) # Автоматическая обработка .ui файлов
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS 
    Core 
    Gui 
    Widgets
    Sql
    )

add_executable(journal_app 
    ./App/Src/main.cpp
    ./App/Src/mainwindow.cpp

    ./App/Inc/journal_app.h
    ./App/Inc/mainwindow.hpp
    ./journal_app.ui)

target_include_directories(journal_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/App/Inc
    ${CMAKE_SOURCE_DIR}/App/Src)
    
target_link_libraries(journal_app PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets
    Qt6::Sql
    )

    # Указываем флаги компилятора для поддержки UTF-8
if(MSVC)
    add_compile_options(/utf-8)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

include(CTest)
enable_testing()

