# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.29

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Produce verbose output by default.
VERBOSE = 1

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/andrey/server_client

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/andrey/server_client/build

# Utility rule file for GENERATE_FlatBuffersGeneratedHeaders.

# Include any custom commands dependencies for this target.
include CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/progress.make

CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders: FlatBuffersGeneratedHeaders/reply_generated.h
CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders: FlatBuffersGeneratedHeaders/request_generated.h
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/home/andrey/server_client/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Generating flatbuffer target FlatBuffersGeneratedHeaders"

FlatBuffersGeneratedHeaders/reply_generated.h: external/flatbuffer-build/flatc
FlatBuffersGeneratedHeaders/reply_generated.h: /home/andrey/server_client/resources/reply.fbs
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/home/andrey/server_client/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building resources/reply.fbs flatbuffers..."
	cd /home/andrey/server_client && /home/andrey/server_client/build/external/flatbuffer-build/flatc -o /home/andrey/server_client/build/FlatBuffersGeneratedHeaders -c resources/reply.fbs

FlatBuffersGeneratedHeaders/request_generated.h: external/flatbuffer-build/flatc
FlatBuffersGeneratedHeaders/request_generated.h: /home/andrey/server_client/resources/request.fbs
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/home/andrey/server_client/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building resources/request.fbs flatbuffers..."
	cd /home/andrey/server_client && /home/andrey/server_client/build/external/flatbuffer-build/flatc -o /home/andrey/server_client/build/FlatBuffersGeneratedHeaders -c resources/request.fbs

GENERATE_FlatBuffersGeneratedHeaders: CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders
GENERATE_FlatBuffersGeneratedHeaders: FlatBuffersGeneratedHeaders/reply_generated.h
GENERATE_FlatBuffersGeneratedHeaders: FlatBuffersGeneratedHeaders/request_generated.h
GENERATE_FlatBuffersGeneratedHeaders: CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/build.make
.PHONY : GENERATE_FlatBuffersGeneratedHeaders

# Rule to build all files generated by this target.
CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/build: GENERATE_FlatBuffersGeneratedHeaders
.PHONY : CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/build

CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/cmake_clean.cmake
.PHONY : CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/clean

CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/depend:
	cd /home/andrey/server_client/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/andrey/server_client /home/andrey/server_client /home/andrey/server_client/build /home/andrey/server_client/build /home/andrey/server_client/build/CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/GENERATE_FlatBuffersGeneratedHeaders.dir/depend
