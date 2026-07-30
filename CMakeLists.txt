cmake_minimum_required(VERSION 3.10)
project(ZabbixDesktopInfo C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# Windows version: Vista+ for UpdateLayeredWindow, modern GDI+
add_definitions(-D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -D_WIN32_IE=0x0600)

# Source files
set(SOURCES
    src/json.c
    src/zabbix_api.c
    src/config.c
    src/render.cpp
    src/widget.c
    src/ui_login.c
    src/ui_select.c
    src/main.c
    src/resource.rc
)

# WIN32 = subsystem:windows (no console)
add_executable(zabbix-desktop WIN32 ${SOURCES})

target_include_directories(zabbix-desktop PRIVATE src)

# Windows libraries
target_link_libraries(zabbix-desktop PRIVATE
    gdiplus
    wininet
    comctl32
    comdlg32
    shell32
    user32
    gdi32
    ole32
    uuid
    crypt32
)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(zabbix-desktop PRIVATE -O2 -Wall)
endif()
