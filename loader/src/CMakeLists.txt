cmake_minimum_required(VERSION 3.22.1)
project("loader")

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DZKSU_VERSION=\"${ZKSU_VERSION}\")

aux_source_directory(common COMMON_SRC_LIST)
add_library(common STATIC ${COMMON_SRC_LIST} ../../native/empty_tmpfs.c)
target_include_directories(common PUBLIC ../../native)
target_include_directories(common PRIVATE include)
target_link_libraries(common log)

aux_source_directory(injector INJECTOR_SRC_LIST)
add_library(zygisk SHARED ${INJECTOR_SRC_LIST})
target_compile_options(zygisk PRIVATE -Wno-invalid-offsetof -Wno-unused-private-field)
target_include_directories(zygisk PRIVATE include)
target_link_libraries(zygisk log common lsplt_static)

aux_source_directory(ptracer PTRACER_SRC_LIST)
add_executable(libzygisk_ptrace.so ${PTRACER_SRC_LIST})
target_include_directories(libzygisk_ptrace.so PRIVATE include)
target_link_libraries(libzygisk_ptrace.so log common)

add_subdirectory(external)

# Separate opt-in target: no ptracer, daemon, mount or concealment sources.
option(NEO_BUILD_POCKET "Build the cooperative Pocket Android 9 provider" OFF)
if(NEO_BUILD_POCKET)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "Pocket adapter currently supports arm64-v8a only")
    endif()
    add_library(neozygisk_pocket SHARED pocket/pocket.cpp injector/module.cpp
                common/files.cpp common/misc.cpp)
    target_compile_definitions(neozygisk_pocket PRIVATE NEO_POCKET)
    target_include_directories(neozygisk_pocket PRIVATE include injector pocket)
    target_compile_options(neozygisk_pocket PRIVATE -Wall -Wextra -Werror
        -Wno-invalid-offsetof -Wno-unused-private-field -Wno-unused-parameter)
    target_link_libraries(neozygisk_pocket PRIVATE log dl lsplt_static)
    target_link_options(neozygisk_pocket PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
endif()
if(NEO_BUILD_POCKET)
    add_library(neo_pocket_fixture SHARED ../../tests/pocket/fixture.cpp)
    target_include_directories(neo_pocket_fixture PRIVATE include)
    target_compile_options(neo_pocket_fixture PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(neo_pocket_fixture PRIVATE dl)
endif()
