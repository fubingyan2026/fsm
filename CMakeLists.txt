cmake_minimum_required(VERSION 3.15)
project(FSM C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default build type to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Enable debugging symbols in Debug mode
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Find all source files
set(FSM_SOURCES
    fsm.c
    fsm_example.c
)

# Create the executable
add_executable(fsm_example ${FSM_SOURCES})

# Optional: Define configuration options via command line
# Usage: cmake -B build -DFSM_ENABLE_DEBUG=0
# Usage: cmake -B build -DFSM_MAX_STATES=16
option(FSM_ENABLE_DEBUG "Enable debug features" ON)
option(FSM_ENABLE_STATS "Enable runtime statistics" ON)
option(FSM_ENABLE_CALLBACKS "Enable state callbacks" ON)
option(FSM_ENABLE_EVENT_QUEUE "Enable event queue" ON)
option(FSM_ENABLE_ASSERT "Enable assertions" ON)

# Set FSM_MAX_STATES for larger state machines
if(NOT FSM_MAX_STATES)
    set(FSM_MAX_STATES 16)
endif()

# Pass configuration to compiler
if(NOT FSM_ENABLE_DEBUG)
    target_compile_definitions(fsm_example PRIVATE FSM_ENABLE_DEBUG=0)
endif()

if(NOT FSM_ENABLE_STATS)
    target_compile_definitions(fsm_example PRIVATE FSM_ENABLE_STATS=0)
endif()

if(NOT FSM_ENABLE_CALLBACKS)
    target_compile_definitions(fsm_example PRIVATE FSM_ENABLE_CALLBACKS=0)
endif()

if(NOT FSM_ENABLE_EVENT_QUEUE)
    target_compile_definitions(fsm_example PRIVATE FSM_ENABLE_EVENT_QUEUE=0)
endif()

if(NOT FSM_ENABLE_ASSERT)
    target_compile_definitions(fsm_example PRIVATE FSM_ENABLE_ASSERT=0)
endif()

target_compile_definitions(fsm_example PRIVATE FSM_MAX_STATES=${FSM_MAX_STATES})

# Print configuration info
message(STATUS "FSM Configuration:")
message(STATUS "  FSM_ENABLE_DEBUG    = ${FSM_ENABLE_DEBUG}")
message(STATUS "  FSM_ENABLE_STATS    = ${FSM_ENABLE_STATS}")
message(STATUS "  FSM_ENABLE_CALLBACKS = ${FSM_ENABLE_CALLBACKS}")
message(STATUS "  FSM_ENABLE_EVENT_QUEUE = ${FSM_ENABLE_EVENT_QUEUE}")
message(STATUS "  FSM_ENABLE_ASSERT   = ${FSM_ENABLE_ASSERT}")
