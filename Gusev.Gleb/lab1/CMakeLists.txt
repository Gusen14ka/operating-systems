cmake_minimum_required(VERSION 3.16)
project(reminder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Werror)

option(BUILD_TESTS "Собирать тесты" ON)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM CORE_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp)

add_library(reminder_core STATIC ${CORE_SOURCES})
target_include_directories(reminder_core PUBLIC ${PROJECT_SOURCE_DIR}/include)

# Демон
add_executable(reminder src/main.cpp)
target_link_libraries(reminder PRIVATE reminder_core)

# Тесты
if(BUILD_TESTS)
    enable_testing()

    # Unit-тесты:
    file(GLOB UNIT_TESTS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/unit/test_*.cpp)
    foreach(test_src ${UNIT_TESTS})
        get_filename_component(test_name ${test_src} NAME_WE)
        add_executable(${test_name} ${test_src})
        target_link_libraries(${test_name} PRIVATE reminder_core)
        add_test(NAME ${test_name} COMMAND ${test_name})
        # Фиксируем часовой пояс, чтобы тесты не зависели от машины
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "TZ=Europe/Moscow")
    endforeach()

    # Интеграционные тесты (bash):
    file(GLOB INTEGRATION_TESTS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tests/integration/test_*.sh)
    foreach(test_sh ${INTEGRATION_TESTS})
        get_filename_component(test_name ${test_sh} NAME_WE)
        add_test(NAME ${test_name} COMMAND bash ${test_sh} $<TARGET_FILE:reminder>)
    endforeach()
endif()
