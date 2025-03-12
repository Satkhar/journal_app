cmake_minimum_required(VERSION 3.10.0)
project(journal_app VERSION 0.1.0 LANGUAGES C CXX)

add_executable(journal_app main.cpp)

include(CTest)
enable_testing()

