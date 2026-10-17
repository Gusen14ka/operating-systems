cmake_minimum_required(VERSION 3.16)
project(reminder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # compile_commands.json для IDE/clangd

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(BUILD_TESTS "Собирать тесты" ON)

# --- Ядро: все src/*.cpp, кроме main.cpp -----------------------------------
# CONFIGURE_DEPENDS: новый .cpp в src/ подхватится без правки этого файла
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM CORE_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp)

add_library(reminder_core STATIC ${CORE_SOURCES})
target_include_directories(reminder_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# --- Демон ------------------------------------------------------------------
add_executable(reminder src/main.cpp)
target_link_libraries(reminder PRIVATE reminder_core)

# --- Тесты ------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()

    # Подробные логи и артефакты упавших кейсов (каталог в .gitignore)
    set(TEST_LOG_DIR ${PROJECT_SOURCE_DIR}/test-logs)

    # Общий main() для unit-тестов
    add_library(test_runner OBJECT ${PROJECT_SOURCE_DIR}/tests/unit/runner.cpp)

    # Unit-тесты: каждый tests/unit/test_*.cpp -> отдельный бинарник
    file(GLOB UNIT_TESTS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/unit/test_*.cpp)
    foreach(test_src ${UNIT_TESTS})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE reminder_core test_runner)
        add_test(NAME unit_${test_name} COMMAND ${test_name})
        # Часовой пояс фиксирован, чтобы результат не зависел от машины
        set_tests_properties(unit_${test_name} PROPERTIES
            LABELS unit
            ENVIRONMENT "TZ=Europe/Moscow;REMINDER_TEST_LOG_DIR=${TEST_LOG_DIR}")
    endforeach()

    # Интеграционные тесты (bash): каждый tests/integration/test_*.sh
    file(GLOB INTEGRATION_TESTS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/integration/test_*.sh)
    foreach(test_sh ${INTEGRATION_TESTS})
        get_filename_component(test_name ${test_sh} NAME_WE)
        add_test(NAME integration_${test_name} COMMAND bash ${test_sh} $<TARGET_FILE:reminder>)
        # Запускают реальные процессы и ждут по времени — по одному
        set_tests_properties(integration_${test_name} PROPERTIES
            LABELS integration
            RUN_SERIAL TRUE
            ENVIRONMENT "REMINDER_TEST_LOG_DIR=${TEST_LOG_DIR}")
    endforeach()
endif()
