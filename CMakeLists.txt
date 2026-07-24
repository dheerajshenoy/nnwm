cmake_minimum_required(VERSION 3.16)
project(nnwm VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_SCENEFX "Build and use scenefx from GitHub for rounded corners and shadows" OFF)

find_package(PkgConfig REQUIRED)

# scenefx requires wlroots 0.20; without it prefer 0.19 then fall back to 0.20
if(USE_SCENEFX)
    pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
else()
    pkg_check_modules(WLROOTS wlroots-0.19)
    if(NOT WLROOTS_FOUND)
        pkg_check_modules(WLROOTS REQUIRED wlroots-0.20)
    endif()
endif()

pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(LIBINPUT REQUIRED libinput)
pkg_check_modules(LUA REQUIRED lua5.4)
pkg_check_modules(PANGOCAIRO REQUIRED pangocairo)
pkg_check_modules(PIXMAN REQUIRED pixman-1)
pkg_check_modules(DBUS REQUIRED dbus-1)

if(USE_SCENEFX)
    include(ExternalProject)
    find_program(MESON meson REQUIRED)
    find_program(NINJA ninja REQUIRED)

    set(SCENEFX_PREFIX  "${CMAKE_BINARY_DIR}/scenefx-install")
    set(SCENEFX_LIBDIR  "${SCENEFX_PREFIX}/lib")
    set(SCENEFX_LIB     "${SCENEFX_LIBDIR}/libscenefx-0.5.so")
    set(SCENEFX_INCDIR  "${SCENEFX_PREFIX}/include/scenefx-0.5")

    ExternalProject_Add(
        scenefx_ext
        GIT_REPOSITORY  https://github.com/wlrfx/scenefx.git
        GIT_TAG         main
        GIT_SHALLOW     TRUE
        PREFIX          "${CMAKE_BINARY_DIR}/scenefx-src"
        CONFIGURE_COMMAND
            ${MESON} setup
                "--prefix=${SCENEFX_PREFIX}"
                "--libdir=${SCENEFX_LIBDIR}"
                "--buildtype=release"
                "--wrap-mode=nodownload"
                <BINARY_DIR>
                <SOURCE_DIR>
        BUILD_COMMAND   ${NINJA} -C <BINARY_DIR>
        INSTALL_COMMAND ${NINJA} -C <BINARY_DIR> install
        BUILD_BYPRODUCTS "${SCENEFX_LIB}"
    )

    file(MAKE_DIRECTORY "${SCENEFX_INCDIR}")

    add_library(scenefx SHARED IMPORTED GLOBAL)
    set_target_properties(scenefx PROPERTIES
        IMPORTED_LOCATION             "${SCENEFX_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${SCENEFX_INCDIR}"
    )
    add_dependencies(scenefx scenefx_ext)
endif()

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

set(EXT_WORKSPACE_XML    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-workspace/ext-workspace-v1.xml")
set(EXT_WORKSPACE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ext-workspace-v1-protocol.h")
set(EXT_WORKSPACE_CODE   "${CMAKE_CURRENT_BINARY_DIR}/ext-workspace-v1-protocol.c")

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

add_custom_command(
    OUTPUT "${EXT_WORKSPACE_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" server-header "${EXT_WORKSPACE_XML}" "${EXT_WORKSPACE_HEADER}"
    DEPENDS "${EXT_WORKSPACE_XML}"
)
add_custom_command(
    OUTPUT "${EXT_WORKSPACE_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${EXT_WORKSPACE_XML}" "${EXT_WORKSPACE_CODE}"
    DEPENDS "${EXT_WORKSPACE_XML}"
)

add_custom_target(protocol_headers ALL
    DEPENDS
        "${XDG_SHELL_HEADER}"
        "${LAYER_SHELL_HEADER}"
        "${OUTPUT_POWER_HEADER}"
        "${EXT_IMAGE_COPY_CAPTURE_HEADER}"
        "${EXT_IMAGE_CAPTURE_SOURCE_HEADER}"
        "${EXT_WORKSPACE_HEADER}"
        "${EXT_WORKSPACE_CODE}"
)

add_executable(nnwm
    src/main.cpp
    src/nnwm.cpp
    src/actions.cpp
    src/bar.cpp
    src/cursor.cpp
    src/xdg_shell.cpp
    src/layer_shell.cpp
    src/output.cpp
    src/session_lock.cpp
    src/ext_workspace.cpp
    src/foreign_toplevel.cpp
    src/lua/config.cpp
    src/tray.cpp
    src/ipc.cpp
)
add_dependencies(nnwm protocol_headers)

target_include_directories(nnwm PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}"
    ${WLROOTS_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${LIBINPUT_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIRS}
    ${PANGOCAIRO_INCLUDE_DIRS}
    ${PIXMAN_INCLUDE_DIRS}
    ${DBUS_INCLUDE_DIRS}
)

target_compile_definitions(nnwm PRIVATE
    WLR_USE_UNSTABLE
    _DEFAULT_SOURCE
    "NNWM_VERSION=\"${PROJECT_VERSION}\""
)

if(USE_SCENEFX)
    target_compile_definitions(nnwm PRIVATE HAVE_SCENEFX)
    target_link_libraries(nnwm PRIVATE scenefx)
    add_dependencies(nnwm scenefx_ext)
    # Build RPATH: find the .so in the build tree when running without installing.
    # Install RPATH: find it in the system libdir after cmake --install.
    set_target_properties(nnwm PROPERTIES
        BUILD_RPATH   "${SCENEFX_LIBDIR}"
        INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}"
    )
endif()

target_link_libraries(nnwm PRIVATE
    ${WLROOTS_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    ${LIBINPUT_LIBRARIES}
    ${LUA_LIBRARIES}
    ${PANGOCAIRO_LIBRARIES}
    ${PIXMAN_LIBRARIES}
    ${DBUS_LIBRARIES}
)

option(HAVE_XWAYLAND "Build with XWayland support" ON)
if(HAVE_XWAYLAND)
    pkg_check_modules(XCB xcb)
    if(XCB_FOUND)
        target_compile_definitions(nnwm PRIVATE HAVE_XWAYLAND)
        target_sources(nnwm PRIVATE src/xwayland.cpp src/xwayland_compat.c)
        target_include_directories(nnwm PRIVATE ${XCB_INCLUDE_DIRS})
        target_link_libraries(nnwm PRIVATE ${XCB_LIBRARIES})
    else()
        message(STATUS "xcb not found, XWayland support disabled")
    endif()
endif()

target_compile_options(nnwm PRIVATE
    ${WLROOTS_CFLAGS_OTHER}
    ${WAYLAND_SERVER_CFLAGS_OTHER}
    ${XKBCOMMON_CFLAGS_OTHER}
    ${LUA_CFLAGS_OTHER}
    $<$<CONFIG:Debug>:-ggdb3>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)

# ---- nnwmctl ----
add_executable(nnwmctl src/nnwmctl/nnwmctl.cpp)
target_compile_options(nnwmctl PRIVATE
    $<$<CONFIG:Debug>:-ggdb3>
    $<$<CONFIG:Debug>:-O0>
)

include(GNUInstallDirs)

install(TARGETS nnwm nnwmctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES nnwm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
if(USE_SCENEFX)
    install(FILES "${SCENEFX_LIB}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
