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
    src/legacy_config/legacy_config.cpp
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

# The default HeadTracking.ini the mod writes on a first launch and the runtime
# Config it loads into. It carries the config_defaults check src/config.cpp
# names - the shipped default file read back through the frozen reader over a
# poisoned Config - which is what stops that text and the Config member
# initialisers drifting apart.
add_executable(config_tests tests/config_tests.cpp src/config.cpp src/legacy_config/legacy_config.cpp)
target_include_directories(config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(config_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(config_tests PRIVATE /W4 /WX)
endif()
add_test(NAME config COMMAND config_tests)

# The frozen HeadTracking.ini reader is the input boundary for every file an
# earlier build left: what it accepts, what it refuses, what it puts in place of
# a refusal, and that the refusal is logged rather than swallowed. It reads real
# files through the same GetPrivateProfile* path every published build used.
add_executable(legacy_reader_tests
    tests/config_differential/legacy_reader_tests.cpp
    src/legacy_config/legacy_config.cpp
)
target_include_directories(legacy_reader_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(legacy_reader_tests PRIVATE cameraunlock)
if(MSVC)
    target_compile_options(legacy_reader_tests PRIVATE /W4 /WX)
endif()
add_test(NAME legacy_reader COMMAND legacy_reader_tests)

# The config differential test. The oracle is the newest published build's reader and
# Hotkeys::Start with the core sources they compiled, copied verbatim into
# tests/config_differential/oracle. It is two libraries because Hotkeys::Start has to run on
# oracle_fake's recording poller, keyboard and mod while the reader runs on the real published
# IniReader; each renames the two namespaces its own way, so neither collides with the other or
# with the current core and mod. Neither links the current core, so no current header reaches
# them.
set(WOLF_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle)
add_library(wolf_config_oracle STATIC
    ${WOLF_ORACLE_DIR}/src/config.cpp
    ${WOLF_ORACLE_DIR}/core/src/config/ini_reader.cpp
    ${WOLF_ORACLE_DIR}/core/src/config/value_guards.cpp
    ${WOLF_ORACLE_DIR}/core/src/logging/file_log.cpp
    tests/config_differential/oracle_config_adapter.cpp
)
target_include_directories(wolf_config_oracle PRIVATE
    ${WOLF_ORACLE_DIR}/src
    ${WOLF_ORACLE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
target_compile_definitions(wolf_config_oracle PRIVATE
    cameraunlock=wolf_oracle_core
    wolf_ht=wolf_oracle)

add_library(wolf_hotkeys_oracle STATIC
    ${WOLF_ORACLE_DIR}/src/hotkeys.cpp
    ${WOLF_ORACLE_DIR}/core/src/logging/file_log.cpp
    tests/config_differential/oracle_hotkeys_adapter.cpp
)
target_include_directories(wolf_hotkeys_oracle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle_fake
    ${WOLF_ORACLE_DIR}/src
    ${WOLF_ORACLE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
target_compile_definitions(wolf_hotkeys_oracle PRIVATE
    cameraunlock=wolf_oracle_hk_core
    wolf_ht=wolf_oracle_hk)
target_compile_options(wolf_hotkeys_oracle PRIVATE
    /FI${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/oracle_fake/fake_keyboard.h)

add_executable(config_differential_tests
    tests/config_differential/differential_tests.cpp
    src/legacy_config/legacy_config.cpp
)
target_include_directories(config_differential_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential
)
target_compile_definitions(config_differential_tests PRIVATE
    WOLF_DIFFERENTIAL_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential/data")
target_link_libraries(config_differential_tests PRIVATE
    cameraunlock wolf_config_oracle wolf_hotkeys_oracle)
if(MSVC)
    target_compile_options(config_differential_tests PRIVATE /W4 /WX)
endif()
add_test(NAME config_differential COMMAND config_differential_tests)

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
