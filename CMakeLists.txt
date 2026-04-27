cmake_minimum_required(VERSION 3.20)
project(agents-hub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
	add_compile_options(/Zc:__cplusplus /permissive-)
	add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

# ─── ImGui ───
include(FetchContent)
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui
	GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui_lib PUBLIC
	${imgui_SOURCE_DIR}
	${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC d3d11 d3dcompiler dxgi)

# ─── Agents-Hub ───
add_executable(agents-hub WIN32
	src/main.cpp
	src/App.cpp
	src/Logger.cpp
	src/Agent.cpp
	src/AgentDrivers.cpp
	src/AgentManager.cpp
	src/ClaudeSessionDiscovery.cpp
	src/D3D11Context.cpp
	src/FolderPicker.cpp
	src/ImGuiRuntime.cpp
	src/JsonlActivityProbe.cpp
	src/MainWindow.cpp
	src/Sidebar.cpp
	src/WaitingFlagWatcher.cpp
	src/WindowsTerminalSpawner.cpp
)
target_include_directories(agents-hub PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(agents-hub PRIVATE imgui_lib ole32 shell32 uuid)

# ─── Tests ───
option(BUILD_TESTS "Build the agents-hub unit test suite" OFF)
if(BUILD_TESTS)
	include(FetchContent)
	FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest
		GIT_TAG v1.14.0
	)
	# Force shared CRT to match our main target (MSVC /MD).
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googletest)

	enable_testing()
	add_subdirectory(tests)
endif()
