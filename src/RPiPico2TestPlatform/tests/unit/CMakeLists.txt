# ---------------------------------------------------------------------------
# Host-side C++ unit tests (no Pico SDK dependency)
# Uses MockHAL so tests compile and run on x86/arm64.
# ---------------------------------------------------------------------------

# Shared library of production code (HAL-independent modules)
add_library(stimulus_core STATIC
    ${CMAKE_SOURCE_DIR}/src/protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/gpio_ctrl.cpp
    ${CMAKE_SOURCE_DIR}/src/capture.cpp
    ${CMAKE_SOURCE_DIR}/src/sequence.cpp
    ${CMAKE_SOURCE_DIR}/src/pwm_ctrl.cpp
    ${CMAKE_SOURCE_DIR}/src/command_handler.cpp
)
target_include_directories(stimulus_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}   # for mock_hal.hpp
)
target_compile_features(stimulus_core PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# Helper macro to register one test executable
# ---------------------------------------------------------------------------
macro(add_unit_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE stimulus_core unity)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${unity_SOURCE_DIR}/src
    )
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------
add_unit_test(test_protocol  test_protocol.cpp)
add_unit_test(test_gpio_ctrl test_gpio_ctrl.cpp)
add_unit_test(test_capture   test_capture.cpp)
add_unit_test(test_sequence  test_sequence.cpp)
add_unit_test(test_pwm_ctrl  test_pwm_ctrl.cpp)
add_unit_test(test_cmd_handler test_cmd_handler.cpp)
