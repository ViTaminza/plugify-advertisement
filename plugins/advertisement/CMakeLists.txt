# advertisement — Plugify C++ plugin: periodic server messages from advertisement.ini.
# Port of the Metamod:Source plugin "advertisement".
#
#   advertisement_core        engine-free config/message model (also used by
#                             test/advert_core_tests.cpp)
#   plugify-advertisement     the plugin shared library (entry "bin/advertisement"
#                             -> bin/libadvertisement.so)

set(ADVERTISEMENT_VERSION "2.0.0")

# --- core ---------------------------------------------------------------------
add_library(advertisement_core STATIC src/advert_core.cpp)
target_include_directories(advertisement_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# pisexcompat brings the KeyValues parser (config files) and, through its own PUBLIC link, the
# s2sdk binding globals.
target_link_libraries(advertisement_core PUBLIC pisexcompat)
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
# Engine-free tests of the config reader against the shipped advertisement.ini. The binary is
# written into build/tests so ../test.sh runs it with the rest (*_tests convention).
add_executable(advertisement_core_tests test/advert_core_tests.cpp)
target_link_libraries(advertisement_core_tests PRIVATE advertisement_core -static-libstdc++ -static-libgcc)
target_compile_options(advertisement_core_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(advertisement_core_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        ADVERTISEMENT_TEST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/test/data/advertisement.ini")
set_target_properties(advertisement_core_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
