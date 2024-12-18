cmake_minimum_required(VERSION 3.29)
project(serverclient LANGUAGES CXX)

# To make neovim happy about headers that end up in build directory
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(lib_config.cmake)


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

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/libuv/CMakeLists.txt" OR NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/flatbuffers/CMakeLists.txt")
    message(FATAL "The submodules were not downloaded! git submodule was turned off or failed.")
endif()

add_subdirectory(extern/flatbuffers EXCLUDE_FROM_ALL)
add_subdirectory(extern/libuv EXCLUDE_FROM_ALL)
add_subdirectory(extern/boost EXCLUDE_FROM_ALL)

flatbuffers_generate_headers(
    TARGET FlatBuffersGeneratedHeaders
    SCHEMAS resources/reply.fbs;resources/request.fbs
)

add_executable(client_async
    src/client_async/main.cpp
)
target_include_directories(client_async PUBLIC include ${CMAKE_BINARY_DIR}/FlatBuffersGeneratedHeaders)

add_executable(client 
    src/client/main.cpp
)
target_include_directories(client PUBLIC include ${CMAKE_BINARY_DIR}/FlatBuffersGeneratedHeaders)

add_executable(server 
    src/server/main.cpp 
    src/server/daemonize.cpp
    src/server/hashtable.cpp
)
target_include_directories(server PUBLIC include ${CMAKE_BINARY_DIR}/FlatBuffersGeneratedHeaders)

target_link_libraries(client_async PRIVATE flatbuffers uv_a Boost::system Boost::asio)
target_link_libraries(client PRIVATE flatbuffers uv_a)
target_link_libraries(server PRIVATE flatbuffers uv_a)

target_link_options(client PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=address>")
target_link_options(client_async PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=address>")
target_link_options(server PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=address>")
target_compile_options(client PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=address>")
target_compile_options(client_async PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=address>")
target_compile_options(server PRIVATE "$<$<CONFIG:DEBUG>:-fsanitize=address>")

target_compile_features(client_async PRIVATE cxx_std_20)

# compiler options / macro hints
if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
    if(UNIX OR APPLE)
      target_compile_options(client_async
        PUBLIC
            -stdlib=libc++
            -fcoroutines-ts
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
  target_compile_options(client_async
    PUBLIC
        -fcoroutines
        -pthread
    )
    target_link_libraries(client_async
    PUBLIC
        stdc++
    )
endif()

configure_file(resources/request.fbs resources/request.fbs COPYONLY)
configure_file(resources/reply.fbs resources/reply.fbs COPYONLY)
configure_file(resources/requests.json resources/requests.json COPYONLY)

macro(package_add_test TESTNAME)
    add_executable(${TESTNAME} tests/${ARGN})
    target_link_libraries(${TESTNAME} gtest gmock gtest_main)
    gtest_discover_tests(${TESTNAME} 
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    )
    set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
endmacro()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    message(STATUS "CTest is enabled")
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )

    FetchContent_MakeAvailable(googletest)

    include(GoogleTest)
    # add_test(NAME EmptyTest COMMAND testbinary)
    # package_add_test(testbinary test1.cpp)

    configure_file(resources/ctest_requests.json resources/ctest_requests.json COPYONLY)
    add_test(NAME ServerTest COMMAND server -d)
    add_test(NAME ClientTest COMMAND client --json_commands ./resources/ctest_requests.json)
endif()

