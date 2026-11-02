# advertisement — Plugify C++ plugin: periodic server messages from advertisement.ini.
# Port of the Metamod:Source plugin "advertisement".
#
#   advertisement_core     engine-free config/message model plus the plugin-local KV1 reader
#                          (also used by tests/advertisement_tests.cpp)
#   plugify-advertisement  the plugin shared library (entry "bin/advertisement"
#                          -> bin/libadvertisement.so)
#
# Pure s2sdk (docs/PLUGIN_STANDARD.md §7): no pisexcompat. The map gate (PITFALLS B1/B2), the
# update-driven interval (C9) and the five output helpers live in src/plugin.cpp itself.

set(ADVERTISEMENT_VERSION "2.1.0")

# --- core ---------------------------------------------------------------------
add_library(advertisement_core STATIC src/advert_core.cpp)
target_include_directories(advertisement_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(advertisement_core PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(advertisement_core PUBLIC _GLIBCXX_USE_CXX11_ABI=1)

# --- plugin -------------------------------------------------------------------
add_library(plugify-advertisement SHARED
        src/plugin.cpp
        # Binding globals (__s2sdk_*) filled by the plugify module from the s2sdk dependency.
        $<TARGET_OBJECTS:s2sdk_bindings>
)
target_include_directories(plugify-advertisement PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(plugify-advertisement SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-advertisement PRIVATE
        plugify_sdk
        advertisement_core
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-advertisement PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-advertisement PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="advertisement"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-advertisement PROPERTIES OUTPUT_NAME advertisement)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-advertisement PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/advertisement.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/advertisement.pplugin"
)

# --- tests ---------------------------------------------------------------------
# advertisement_tests: engine-free. The core is linked from advertisement_core; the plugin TU is
# compiled into the test (its handlers live in an anonymous namespace) and the __s2sdk_* binding
# globals are recording fakes: the KV1 reader and the shipped advertisement.ini, the config
# lookup, the map gate (B1/B2), the update-driven interval with its 1/64 s floor (C9), the five
# output sinks and a clean unload. Written into build/tests so ../test.sh runs it with the rest
# (*_tests convention).
#
# The top-level CMakeLists declares the PLUGIFY_PORT_TESTS option *after* the plugin directories,
# so on a first configure it is not defined here yet: default it to the same ON. An explicit
# -DPLUGIFY_PORT_TESTS=OFF is already in the cache and still wins.
if(NOT DEFINED PLUGIFY_PORT_TESTS)
    set(PLUGIFY_PORT_TESTS ON)
endif()
if(PLUGIFY_PORT_TESTS)
    add_executable(advertisement_tests
            tests/advertisement_tests.cpp
            $<TARGET_OBJECTS:s2sdk_bindings>
    )
    target_include_directories(advertisement_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(advertisement_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
    target_link_libraries(advertisement_tests PRIVATE
            plugify_sdk
            advertisement_core
            -static-libstdc++ -static-libgcc
    )
    target_compile_options(advertisement_tests PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(advertisement_tests PRIVATE
            _GLIBCXX_USE_CXX11_ABI=1
            PLUGIFY_MODULE="advertisement"
            PLUGIFY_IS_DEBUG=0
            PLUGIFY_IS_RELEASE=1
            ADVERTISEMENT_TEST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/advertisement.ini"
    )
    set_target_properties(advertisement_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endif()
