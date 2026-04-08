cmake_minimum_required(VERSION 3.16)

project(Codex)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

file(GLOB_RECURSE CODEX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM CODEX_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")

add_executable(main
    src/main.cpp
    ${CODEX_SOURCES}
)

target_include_directories(main PRIVATE
    ${CMAKE_SOURCE_DIR}/src/third_party
)

if (WIN32)
    target_link_libraries(main PRIVATE ws2_32)
endif()

