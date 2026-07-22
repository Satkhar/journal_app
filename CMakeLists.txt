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

# Проект намеренно собирается GCC (проверка выше). Warnings включены для всех
# targets; -Werror оставляется CI, чтобы обновление Qt/GCC не ломало local build.
add_compile_options(
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -Wall
    -Wextra
    -Wpedantic)

add_executable(journal_app 
    ./App/Src/main.cpp
    ./App/Src/Domain/JournalApp.cpp
    ./App/Src/Domain/JournalModels.cpp
    ./App/Src/Domain/EventApp.cpp
    ./App/Src/Domain/EventModels.cpp
    ./App/Src/Storage/EventSqliteStorage.cpp
    ./App/Src/Storage/JournalLocal.cpp
    ./App/Src/Storage/JournalRemote.cpp
    ./App/Src/Storage/RemoteConnectionOptions.cpp
    ./App/Src/Storage/SqliteConnect.cpp
    ./App/Src/Sync/SyncService.cpp
    ./App/Src/Ui/mainwindow.cpp
    ./App/Src/Ui/AttendanceCellWidget.cpp
    ./App/Src/Ui/DayMarkerDialog.cpp
    ./App/Src/Ui/EventDialog.cpp
    ./App/Src/Ui/EventDirectoryDialog.cpp
    ./App/Src/Ui/ExistingParticipantDialog.cpp
    ./App/Src/Ui/MonthDaysDialog.cpp
    ./App/Src/Ui/CopyUsersDialog.cpp
    ./App/Src/Ui/ParticipantDialog.cpp
    ./App/Src/Ui/ParticipantDirectoryDialog.cpp
    ./App/Src/Ui/ParticipantStatisticsWidget.cpp
    ./App/Src/Ui/ParticipantEmblemWidget.cpp
    ./App/Src/Ui/ParticipantStrikeHistoryDialog.cpp

    ./App/Inc/Domain/JournalModels.hpp
    ./App/Inc/Domain/EventApp.hpp
    ./App/Inc/Domain/EventModels.hpp
    ./App/Inc/Domain/IEventStorage.hpp
    ./App/Inc/Domain/IJournalStorage.hpp
    ./App/Inc/Domain/IMonthSnapshotStore.hpp
    ./App/Inc/Domain/JournalApp.hpp
    ./App/Inc/Storage/JournalLocal.hpp
    ./App/Inc/Storage/JournalRemote.hpp
    ./App/Inc/Storage/RemoteConnectionOptions.hpp
    ./App/Inc/Storage/SqliteConnect.hpp
    ./App/Inc/Storage/EventSqliteStorage.hpp
    ./App/Inc/Sync/SyncService.hpp
    ./App/Inc/Ui/mainwindow.hpp
    ./App/Inc/Ui/AttendanceCellWidget.hpp
    ./App/Inc/Ui/DayMarkerDialog.hpp
    ./App/Inc/Ui/EventDialog.hpp
    ./App/Inc/Ui/EventDirectoryDialog.hpp
    ./App/Inc/Ui/ExistingParticipantDialog.hpp
    ./App/Inc/Ui/MonthDaysDialog.hpp
    ./App/Inc/Ui/CopyUsersDialog.hpp
    ./App/Inc/Ui/ParticipantDialog.hpp
    ./App/Inc/Ui/ParticipantDirectoryDialog.hpp
    ./App/Inc/Ui/ParticipantPresentation.hpp
    ./App/Inc/Ui/ParticipantStatisticsWidget.hpp
    ./App/Inc/Ui/ParticipantEmblemWidget.hpp
    ./App/Inc/Ui/ParticipantStrikeHistoryDialog.hpp

    ./journal_app.ui
    )

set_target_properties(journal_app PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}")

target_link_libraries(journal_app PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network)

target_include_directories(journal_app PRIVATE 
    ${CMAKE_SOURCE_DIR}/App/Inc
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Storage
    ${CMAKE_SOURCE_DIR}/App/Inc/Sync
    ${CMAKE_SOURCE_DIR}/App/Inc/Ui
    ${CMAKE_SOURCE_DIR}/App/Src
    ${CMAKE_SOURCE_DIR}/App/Src/Domain
    ${CMAKE_SOURCE_DIR}/App/Src/Storage
    ${CMAKE_SOURCE_DIR}/App/Src/Sync
    ${CMAKE_SOURCE_DIR}/App/Src/Ui)

include(CTest)
enable_testing()

add_executable(journal_storage_tests
    ./Test/JournalStorageTests.cpp
    ./App/Src/Domain/JournalModels.cpp
    ./App/Src/Storage/SqliteConnect.cpp
    ./App/Inc/Domain/JournalModels.hpp
    ./App/Inc/Domain/IJournalStorage.hpp
    ./App/Inc/Storage/SqliteConnect.hpp)
target_link_libraries(journal_storage_tests PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(journal_storage_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Storage)
add_test(NAME journal_storage_tests COMMAND journal_storage_tests)

# Legacy input is converted to a small JSON contract outside the application.
# The importer deliberately writes through SqliteConnect so schema constraints,
# transactions and future migrations remain owned by the storage adapter.
add_executable(journal_legacy_importer
    ./Tools/LegacyImportMain.cpp
    ./App/Src/Domain/JournalModels.cpp
    ./App/Src/Storage/SqliteConnect.cpp
    ./App/Inc/Domain/JournalModels.hpp
    ./App/Inc/Domain/IJournalStorage.hpp
    ./App/Inc/Storage/SqliteConnect.hpp)
target_link_libraries(journal_legacy_importer PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(journal_legacy_importer PRIVATE
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Storage)

add_executable(journal_event_storage_tests
    ./Test/EventStorageTests.cpp
    ./App/Src/Domain/EventApp.cpp
    ./App/Src/Domain/EventModels.cpp
    ./App/Src/Storage/EventSqliteStorage.cpp
    ./App/Inc/Domain/EventApp.hpp
    ./App/Inc/Domain/EventModels.hpp
    ./App/Inc/Domain/IEventStorage.hpp
    ./App/Inc/Storage/EventSqliteStorage.hpp)
target_link_libraries(journal_event_storage_tests PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(journal_event_storage_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Storage)
add_test(NAME journal_event_storage_tests COMMAND journal_event_storage_tests)

add_executable(journal_month_state_tests
    ./Test/month_state_tests.cpp
    ./App/Src/Domain/JournalApp.cpp
    ./App/Src/Domain/JournalModels.cpp
    ./App/Src/Storage/JournalLocal.cpp
    ./App/Src/Storage/SqliteConnect.cpp
    ./App/Inc/Domain/JournalModels.hpp
    ./App/Inc/Domain/IJournalStorage.hpp
    ./App/Inc/Storage/SqliteConnect.hpp)
target_link_libraries(journal_month_state_tests PRIVATE Qt6::Core Qt6::Sql)
target_include_directories(journal_month_state_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Storage)
add_test(NAME journal_month_state_tests COMMAND journal_month_state_tests)

add_executable(journal_sync_tests
    ./Test/SyncServiceTests.cpp
    ./App/Src/Storage/RemoteConnectionOptions.cpp
    ./App/Src/Sync/SyncService.cpp
    ./App/Inc/Domain/JournalModels.hpp
    ./App/Inc/Domain/IMonthSnapshotStore.hpp
    ./App/Inc/Storage/RemoteConnectionOptions.hpp
    ./App/Inc/Sync/SyncService.hpp)
target_link_libraries(journal_sync_tests PRIVATE Qt6::Core Qt6::Network)
target_include_directories(journal_sync_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Storage
    ${CMAKE_SOURCE_DIR}/App/Inc/Sync)
add_test(NAME journal_sync_tests COMMAND journal_sync_tests)

add_executable(journal_ui_tests
    ./Test/ui_dialog_tests.cpp
    ./App/Src/Domain/JournalApp.cpp
    ./App/Src/Domain/EventModels.cpp
    ./App/Src/Domain/JournalModels.cpp
    ./App/Src/Ui/AttendanceCellWidget.cpp
    ./App/Src/Ui/CopyUsersDialog.cpp
    ./App/Src/Ui/DayMarkerDialog.cpp
    ./App/Src/Ui/EventDialog.cpp
    ./App/Src/Ui/ExistingParticipantDialog.cpp
    ./App/Src/Ui/MonthDaysDialog.cpp
    ./App/Src/Ui/ParticipantDialog.cpp
    ./App/Src/Ui/ParticipantDirectoryDialog.cpp
    ./App/Src/Ui/ParticipantStatisticsWidget.cpp
    ./App/Src/Ui/ParticipantEmblemWidget.cpp
    ./App/Src/Ui/ParticipantStrikeHistoryDialog.cpp
    ./App/Inc/Domain/JournalModels.hpp
    ./App/Inc/Domain/EventModels.hpp
    ./App/Inc/Ui/AttendanceCellWidget.hpp
    ./App/Inc/Ui/CopyUsersDialog.hpp
    ./App/Inc/Ui/DayMarkerDialog.hpp
    ./App/Inc/Ui/EventDialog.hpp
    ./App/Inc/Ui/ExistingParticipantDialog.hpp
    ./App/Inc/Ui/MonthDaysDialog.hpp
    ./App/Inc/Ui/ParticipantDialog.hpp
    ./App/Inc/Ui/ParticipantDirectoryDialog.hpp
    ./App/Inc/Ui/ParticipantPresentation.hpp
    ./App/Inc/Ui/ParticipantStatisticsWidget.hpp
    ./App/Inc/Ui/ParticipantEmblemWidget.hpp
    ./App/Inc/Ui/ParticipantStrikeHistoryDialog.hpp)
target_link_libraries(journal_ui_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)
target_include_directories(journal_ui_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/App/Inc/Domain
    ${CMAKE_SOURCE_DIR}/App/Inc/Ui)
if(WIN32)
    add_test(NAME journal_ui_tests
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$ENV{PATH}"
            "QT_QPA_PLATFORM=offscreen"
            $<TARGET_FILE:journal_ui_tests>)
else()
    add_test(NAME journal_ui_tests COMMAND journal_ui_tests)
    set_tests_properties(journal_ui_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
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
