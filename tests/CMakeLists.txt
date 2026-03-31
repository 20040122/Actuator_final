add_executable(condition_executor_test
    condition_executor_test.cpp
    ${CMAKE_SOURCE_DIR}/src/constraint/evaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/executor/generic_executor.cpp
    ${CMAKE_SOURCE_DIR}/src/executor/variable_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/behavior_parser.cpp
)

target_include_directories(condition_executor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)

add_test(
    NAME condition_executor_test
    COMMAND condition_executor_test
)
