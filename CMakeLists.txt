#[===[
        WARNING

        The compiled binary HAS to be in the example directory, NOT in the build directory.

        Check IconsFontAwesome.h for fix.
]===]

cmake_minimum_required(VERSION 3.10)

project(ImGuiAuth VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES main.cpp)

set(IMGUI_SOURCES
	${IMGUI_DIR}/imgui/imgui.cpp
	${IMGUI_DIR}/imgui/imgui_demo.cpp
	${IMGUI_DIR}/imgui/imgui_draw.cpp
	${IMGUI_DIR}/imgui/imgui_tables.cpp
	${IMGUI_DIR}/imgui/imgui_widgets.cpp
	${IMGUI_DIR}/imgui/imgui_impl_win32.cpp
	${IMGUI_DIR}/imgui/imgui_impl_dx11.cpp
)

set(HTTP_SOURCES
	${IMGUI_DIR}/http/ip_helper.cpp
)

add_executable(JXUST ${SOURCES} ${IMGUI_SOURCES} ${HTTP_SOURCES} font/app.rc)

target_include_directories(JXUST PRIVATE
	${IMGUI_DIR}/imgui
	${IMGUI_DIR}/http
)
target_link_options(JXUST PRIVATE
        -static-libgcc      # 静态 libgcc
        -static-libstdc++   # 静态 libstdc++
		-static
        -Wl,/SUBSYSTEM:WINDOWS
        -Wl,/ENTRY:mainCRTStartup
    )
target_link_libraries(JXUST PRIVATE "d3d11.lib" "dxgi.lib" "d3dcompiler.lib" "user32.lib" "kernel32.lib" "gdi32.lib" "shell32.lib" "ws2_32.lib" "wininet.lib" "iphlpapi.lib" "dwmapi.lib" -pthread)