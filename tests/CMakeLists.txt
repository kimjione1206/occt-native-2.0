# Test executables — standalone compilation (no Qt dependency)
# Each test is compiled independently with access to src/ headers

set(TEST_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src")

# Helper macro for adding standalone tests
macro(add_standalone_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE ${TEST_INCLUDE_DIR})
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

# Register test executables
add_standalone_test(test_bugfix_review test_bugfix_review.cpp)
add_standalone_test(test_phase1_bugfixes test_phase1_bugfixes.cpp)
add_standalone_test(test_phase2_features test_phase2_features.cpp)
add_standalone_test(test_phase3_features test_phase3_features.cpp)
add_standalone_test(test_phase4_features test_phase4_features.cpp)
add_standalone_test(test_storage_verify test_storage_verify.cpp)
