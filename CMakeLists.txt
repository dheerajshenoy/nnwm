cmake_minimum_required(VERSION 3.16)
project(nnwm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS wlroots-0.19)
if(NOT WLROOTS_FOUND)
    pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
endif()

pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(LIBINPUT REQUIRED libinput)
pkg_check_modules(LUA REQUIRED lua5.4)
pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(XDG_SHELL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.h")
set(XDG_SHELL_CODE   "${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c")

set(LAYER_SHELL_XML    "${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-layer-shell-unstable-v1.xml")
set(LAYER_SHELL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.h")
set(LAYER_SHELL_CODE   "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c")

set(OUTPUT_POWER_XML    "${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-output-power-management-unstable-v1.xml")
set(OUTPUT_POWER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.h")
set(OUTPUT_POWER_CODE   "${CMAKE_CURRENT_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.c")

set(EXT_IMAGE_COPY_CAPTURE_XML    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml")
set(EXT_IMAGE_COPY_CAPTURE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ext-image-copy-capture-v1-protocol.h")
set(EXT_IMAGE_COPY_CAPTURE_CODE   "${CMAKE_CURRENT_BINARY_DIR}/ext-image-copy-capture-v1-protocol.c")

set(EXT_IMAGE_CAPTURE_SOURCE_XML    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml")
set(EXT_IMAGE_CAPTURE_SOURCE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ext-image-capture-source-v1-protocol.h")
set(EXT_IMAGE_CAPTURE_SOURCE_CODE   "${CMAKE_CURRENT_BINARY_DIR}/ext-image-capture-source-v1-protocol.c")

add_custom_command(
    OUTPUT "${XDG_SHELL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${XDG_SHELL_XML}" "${XDG_SHELL_HEADER}"
    DEPENDS "${XDG_SHELL_XML}"
)
add_custom_command(
    OUTPUT "${XDG_SHELL_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${XDG_SHELL_XML}" "${XDG_SHELL_CODE}"
    DEPENDS "${XDG_SHELL_XML}"
)

add_custom_command(
    OUTPUT "${LAYER_SHELL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" server-header "${LAYER_SHELL_XML}" "${LAYER_SHELL_HEADER}"
    COMMAND sh -c "sed -i 's/const char [*]namespace/const char *namespace_/g' '${LAYER_SHELL_HEADER}'"
    DEPENDS "${LAYER_SHELL_XML}"
)
add_custom_command(
    OUTPUT "${LAYER_SHELL_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${LAYER_SHELL_XML}" "${LAYER_SHELL_CODE}"
    DEPENDS "${LAYER_SHELL_XML}"
)

add_custom_command(
    OUTPUT "${OUTPUT_POWER_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" server-header "${OUTPUT_POWER_XML}" "${OUTPUT_POWER_HEADER}"
    DEPENDS "${OUTPUT_POWER_XML}"
)
add_custom_command(
    OUTPUT "${OUTPUT_POWER_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${OUTPUT_POWER_XML}" "${OUTPUT_POWER_CODE}"
    DEPENDS "${OUTPUT_POWER_XML}"
)

add_custom_command(
    OUTPUT "${EXT_IMAGE_COPY_CAPTURE_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" server-header "${EXT_IMAGE_COPY_CAPTURE_XML}" "${EXT_IMAGE_COPY_CAPTURE_HEADER}"
    DEPENDS "${EXT_IMAGE_COPY_CAPTURE_XML}"
)
add_custom_command(
    OUTPUT "${EXT_IMAGE_COPY_CAPTURE_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${EXT_IMAGE_COPY_CAPTURE_XML}" "${EXT_IMAGE_COPY_CAPTURE_CODE}"
    DEPENDS "${EXT_IMAGE_COPY_CAPTURE_XML}"
)

add_custom_command(
    OUTPUT "${EXT_IMAGE_CAPTURE_SOURCE_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" server-header "${EXT_IMAGE_CAPTURE_SOURCE_XML}" "${EXT_IMAGE_CAPTURE_SOURCE_HEADER}"
    DEPENDS "${EXT_IMAGE_CAPTURE_SOURCE_XML}"
)
add_custom_command(
    OUTPUT "${EXT_IMAGE_CAPTURE_SOURCE_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${EXT_IMAGE_CAPTURE_SOURCE_XML}" "${EXT_IMAGE_CAPTURE_SOURCE_CODE}"
    DEPENDS "${EXT_IMAGE_CAPTURE_SOURCE_XML}"
)

add_executable(nnwm
    src/main.cpp
    src/nnwm.cpp
    src/lua/config.cpp
    "${XDG_SHELL_CODE}"
    "${XDG_SHELL_HEADER}"
    "${LAYER_SHELL_CODE}"
    "${LAYER_SHELL_HEADER}"
    "${OUTPUT_POWER_CODE}"
    "${OUTPUT_POWER_HEADER}"
    "${EXT_IMAGE_COPY_CAPTURE_CODE}"
    "${EXT_IMAGE_COPY_CAPTURE_HEADER}"
    "${EXT_IMAGE_CAPTURE_SOURCE_CODE}"
    "${EXT_IMAGE_CAPTURE_SOURCE_HEADER}"
)

target_include_directories(nnwm PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}"
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
)

target_compile_definitions(nnwm PRIVATE WLR_USE_UNSTABLE _DEFAULT_SOURCE)

target_link_libraries(nnwm PRIVATE
    ${WLROOTS_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${LIBINPUT_LIBRARIES}
    ${LUA_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
)

target_compile_options(nnwm PRIVATE
    ${WLROOTS_CFLAGS_OTHER}
    ${WAYLAND_SERVER_CFLAGS_OTHER}
    ${XKBCOMMON_CFLAGS_OTHER}
    ${LUA_CFLAGS_OTHER}
    $<$<CONFIG:Debug>:-ggdb3>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)
