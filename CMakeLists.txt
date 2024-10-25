cmake_minimum_required(VERSION 3.29)
project(serverclient LANGUAGES CXX)

include(lib_config.cmake)

add_subdirectory(external)

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  option(GIT_SUBMODULE "Check submodules during build" ON)
  if(GIT_SUBMODULE)
    message(STATUS "Submodule update")
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE GIT_SUBMOD_RESULT)
    if(NOT GIT_SUBMOD_RESULT EQUAL "0")
      message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}")
    endif()
  endif()
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/libuv/CMakeLists.txt")
  message(FATAL "The submodules were not downloaded! git submodule was turned off or failed.")
endif()

flatbuffers_generate_headers(
TARGET FlatBuffersGeneratedHeaders
SCHEMAS resources/reply.fbs;resources/request.fbs
)

add_executable(client src/client/main.cpp)
target_include_directories(client PUBLIC include ${CMAKE_BINARY_DIR}/FlatBuffersGeneratedHeaders)

add_executable(server src/server/main.cpp)
target_include_directories(server PUBLIC include ${CMAKE_BINARY_DIR}/FlatBuffersGeneratedHeaders)

target_link_libraries(client PRIVATE flatbuffers uv_a)
target_link_libraries(server PRIVATE flatbuffers uv_a)

function(PrintTargetLibraries target msg)
  get_target_property(linkedLibs ${target} LINK_LIBRARIES)
  get_target_property(interfaceLibs ${target} INTERFACE_LINK_LIBRARIES)
  message(STATUS "${msg}")
  message(STATUS "${target} LINK_LIBRARIES: ${linkedLibs}")
  message(STATUS "${target} INTERFACE_LINK_LIBRARIES: ${interfaceLibs}")
endfunction()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

PrintTargetLibraries(client "Before linking:")

# compiler options / macro hints
if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
    if(UNIX OR APPLE)
      target_compile_options(client
        PUBLIC
            -stdlib=libc++
            -fcoroutines-ts
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
  target_compile_options(client
    PUBLIC
        -fcoroutines
    )
    target_link_libraries(client
    PUBLIC
        stdc++
    )
endif()

PrintTargetLibraries(client "After linking")

configure_file(resources/request.fbs resources/request.fbs COPYONLY)
configure_file(resources/reply.fbs resources/reply.fbs COPYONLY)
configure_file(resources/requests.json resources/requests.json COPYONLY)

