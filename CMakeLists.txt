cmake_minimum_required(VERSION 3.20)
project(WolfensteinTheNewOrderHeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "WolfNewOrder_x64.exe is 64-bit. Configure with -A x64.")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/cameraunlock-core/vendor/minhook")
if(NOT EXISTS "${MINHOOK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "cameraunlock-core/vendor/minhook is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(${MINHOOK_DIR} minhook)

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

add_library(WolfensteinTheNewOrderHeadTracking SHARED
    src/pch.cpp
    src/dllmain.cpp
    src/logging.cpp
    src/config.cpp
    src/hotkeys.cpp
    src/tracker_feed.cpp
    src/headtracking_mod.cpp
    src/camera_hook.cpp
    src/reticle_hook.cpp
    src/game_state.cpp
    src/window_centering.cpp
    src/idtech/game_singletons.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
    src/builds/gdk_offsets.cpp
)

target_precompile_headers(WolfensteinTheNewOrderHeadTracking PRIVATE src/pch.h)

# The version the mod logs comes straight from the project version above, so a
# release stamps CMakeLists.txt and nothing else. A hand-kept copy in a header
# is not in release.ps1's stamp table, and the log then reports 0.0.0 for the
# life of the mod.
target_compile_definitions(WolfensteinTheNewOrderHeadTracking PRIVATE
    HEADTRACKING_VERSION_STRING="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(WolfensteinTheNewOrderHeadTracking PRIVATE /W4 /WX)
else()
    target_compile_options(WolfensteinTheNewOrderHeadTracking PRIVATE -Wall -Wextra -Werror)
endif()

set_target_properties(WolfensteinTheNewOrderHeadTracking PROPERTIES
    OUTPUT_NAME "WolfensteinTheNewOrderHeadTracking"
    SUFFIX ".asi"
)

target_include_directories(WolfensteinTheNewOrderHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(WolfensteinTheNewOrderHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
    opengl32
)

# The camera math is header-only, so its test needs no library: it is the one
# part of the mod that can be checked without a running game, and it is the part
# a refactor is most likely to reverse.
enable_testing()
add_executable(aim_projection_tests tests/aim_projection_tests.cpp)
target_include_directories(aim_projection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(aim_projection_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(aim_projection_tests PRIVATE /W4 /WX)
endif()
add_test(NAME aim_projection COMMAND aim_projection_tests)

add_executable(camera_math_tests tests/camera_math_tests.cpp)
target_include_directories(camera_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(camera_math_tests PRIVATE /W4 /WX)
endif()
add_test(NAME camera_math COMMAND camera_math_tests)

# Same argument for the window placement: the decision about whether a player's
# window gets moved is pure geometry, so it is checkable without a game even
# though the Win32 half of the centring is not.
add_executable(window_placement_tests tests/window_placement_tests.cpp)
target_include_directories(window_placement_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(window_placement_tests PRIVATE /W4 /WX)
endif()
add_test(NAME window_placement COMMAND window_placement_tests)

# The ADS behaviour is header-only for the same reason: the cycle, the fade, the
# entry-relative pose and the tracking-state walk are all pure, and every rule in
# them is invisible from a game session - the yaw seam needs the head within ten
# degrees of 180, the entry-capture rule needs a menu opened mid-aim, and the
# gate ordering needs a menu and the sights up at the same moment.
add_executable(ads_tests tests/ads_tests.cpp)
target_include_directories(ads_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ads_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(ads_tests PRIVATE /W4 /WX)
endif()
add_test(NAME ads COMMAND ads_tests)

# HeadTracking.ini is the only text the mod reads that it did not write, so the
# loader is its input boundary: what it accepts, what it refuses, what it puts
# in place of a refusal, and that the refusal is logged rather than swallowed.
# It also carries the config_defaults check src/config.cpp names - the shipped
# default file loaded back over a poisoned Config - which is what stops that
# text and the Config member initialisers drifting apart.
add_executable(config_tests tests/config_tests.cpp src/config.cpp)
target_include_directories(config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(config_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(config_tests PRIVATE /W4 /WX)
endif()
add_test(NAME config COMMAND config_tests)

# Reclaiming the tracker port from another game, through this mod's own feed and
# real sockets. It is the one behaviour whose acceptance criterion is a
# DURATION - a player who closes the other game has to get tracking back within
# about a second - and a duration cannot be read off the source: the retry
# period and the supervisor's wake-up granularity are constants in two different
# core files, and what the player waits for is their sum plus a bind, a datagram
# and a frame. So this one measures, and prints what it measured.
add_executable(tracker_feed_recovery_tests
    tests/tracker_feed_recovery_tests.cpp
    src/tracker_feed.cpp
)
target_include_directories(tracker_feed_recovery_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tracker_feed_recovery_tests PRIVATE cameraunlock ws2_32)
if(MSVC)
    target_compile_options(tracker_feed_recovery_tests PRIVATE /W4 /WX)
endif()
add_test(NAME tracker_feed_recovery COMMAND tracker_feed_recovery_tests)
