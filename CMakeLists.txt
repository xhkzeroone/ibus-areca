cmake_minimum_required(VERSION 3.20)
project(ibus-areca VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)
find_package(PkgConfig REQUIRED)
find_program(GO_EXECUTABLE go REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

set(BAMBOO_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bamboo-bridge")
set(BAMBOO_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libareca-bamboo.a")
file(GLOB BAMBOO_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/bamboo/bamboo-core/*.go")

add_custom_command(
    OUTPUT "${BAMBOO_ARCHIVE}" "${CMAKE_CURRENT_BINARY_DIR}/libareca-bamboo.h"
    COMMAND "${CMAKE_COMMAND}" -E env
            "GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/go-build-cache"
            "${GO_EXECUTABLE}" build -buildvcs=false -buildmode=c-archive
            -o "${BAMBOO_ARCHIVE}" .
    WORKING_DIRECTORY "${BAMBOO_BRIDGE_DIR}"
    DEPENDS
        "${BAMBOO_BRIDGE_DIR}/go.mod"
        "${BAMBOO_BRIDGE_DIR}/main.go"
        ${BAMBOO_CORE_SOURCES}
    VERBATIM)
add_custom_target(areca-bamboo-bridge DEPENDS "${BAMBOO_ARCHIVE}")

add_library(areca-bamboo STATIC IMPORTED GLOBAL)
set_target_properties(areca-bamboo PROPERTIES IMPORTED_LOCATION "${BAMBOO_ARCHIVE}")
add_dependencies(areca-bamboo areca-bamboo-bridge)

# Phần logic không phụ thuộc IBus, dùng chung cho engine và test.
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(X11_CLIENT REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XTEST REQUIRED IMPORTED_TARGET xtst)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(LIBEI QUIET IMPORTED_TARGET libei-1.0>=1.0)
pkg_check_modules(LIBPORTAL QUIET IMPORTED_TARGET libportal>=0.7.1)
find_package(Threads REQUIRED)

add_executable(areca-mouse-monitor tools/mouse_monitor.cpp)
target_compile_options(areca-mouse-monitor PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(areca-mouse-monitor PRIVATE PkgConfig::LIBINPUT PkgConfig::UDEV)
install(TARGETS areca-mouse-monitor DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")

add_library(areca-core STATIC
    src/im/action.cpp
    src/im/event.cpp
    src/im/input_context.cpp
    src/im/key.cpp
    src/im/raw_config.cpp
    src/adaptive_wait_monitor.cpp
    src/atspi/atspi_accessible.cpp
    src/atspi/atspi_connection.cpp
    src/bamboo_engine_adapter.cpp
    src/browser_autocomplete.cpp
    src/forward_backspace_backend.cpp
    src/input_scheduler.cpp
    src/input_type_detector.cpp
    src/mouse_click_tracker.cpp
    src/native_device.cpp
    src/preedit_logic.cpp
    src/preedit_mode.cpp
    src/program_compatibility.cpp
    src/redirect_mode.cpp
    src/rewrite_mode.cpp
    src/sentence_capitalizer.cpp
    src/surrounding_text_backend.cpp
    src/surrounding_text_cache.cpp
    src/surrounding_text_restore.cpp
    src/surrounding_text_v2_backend.cpp
    src/uinput_backspace_backend.cpp
    src/uinput_device.cpp
    src/uinput_shift_select_backend.cpp
    src/window_focus_tracker.cpp
    src/word_segment.cpp
    src/xtest_backspace_backend.cpp
    src/xtest_device.cpp
    src/xtest_shift_select_backend.cpp
)
target_include_directories(areca-core PUBLIC src)
target_compile_options(areca-core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(areca-core PRIVATE
    ARECA_MOUSE_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/areca-mouse-monitor")
target_link_libraries(areca-core PUBLIC
    areca-bamboo Threads::Threads PkgConfig::GLIB PkgConfig::GIO
    PkgConfig::XKBCOMMON PkgConfig::DBUS PkgConfig::X11_CLIENT PkgConfig::XTEST
    dl m)
if(LIBEI_FOUND AND LIBPORTAL_FOUND)
    target_compile_definitions(areca-core PRIVATE ARECA_HAVE_LIBEI=1)
    if(LIBPORTAL_VERSION VERSION_GREATER_EQUAL 0.8)
        target_compile_definitions(areca-core PRIVATE ARECA_LIBPORTAL_PERSIST=1)
    endif()
    target_link_libraries(areca-core PUBLIC PkgConfig::LIBEI PkgConfig::LIBPORTAL)
else()
    message(WARNING "Native Wayland backend disabled; XTest fallback will be used")
endif()
add_dependencies(areca-core areca-mouse-monitor)

# Lõi engine dùng chung mọi input context, phụ thuộc host trừu tượng nên test
# được khi chưa có IBus.
add_library(areca-engine STATIC
    src/areca_engine.cpp
    src/areca_engine_actions.cpp
    src/areca_engine_backend.cpp
    src/areca_engine_backend_menu.cpp
    src/areca_engine_config.cpp
    src/notification/dynamic_island_announcement.cpp
)
target_compile_options(areca-engine PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(areca-engine PRIVATE
    ARECA_SETTINGS_PATH="${CMAKE_INSTALL_FULL_BINDIR}/ibus-areca-settings")
target_link_libraries(areca-engine PUBLIC areca-core)

if(BUILD_TESTING)
    add_executable(mouse_click_tracker_helper tests/mouse_click_tracker_helper.cpp)
    file(GLOB ARECA_TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
    foreach(file ${ARECA_TESTS})
        string(REPLACE ".cpp" "" name "${file}")
        add_executable(${name} tests/${name}.cpp)
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        target_link_libraries(${name} PRIVATE areca-core)
        add_test(NAME ${name} COMMAND ${name})
    endforeach()
    target_link_libraries(areca_engine_test PRIVATE areca-engine)
    # Biên dịch lại tracker để trỏ tới helper giả thay vì đường dẫn cài đặt.
    target_sources(mouse_click_tracker_test PRIVATE src/mouse_click_tracker.cpp)
    add_dependencies(mouse_click_tracker_test mouse_click_tracker_helper)
    target_compile_definitions(mouse_click_tracker_test PRIVATE
        ARECA_MOUSE_HELPER_PATH="$<TARGET_FILE:mouse_click_tracker_helper>")
endif()
