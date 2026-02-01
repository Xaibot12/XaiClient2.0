cmake_minimum_required(VERSION 3.10)
project(XaiOverlay)

set(CMAKE_CXX_STANDARD 17)

# Find Windows Libraries
set(LIBS ws2_32 d3d11 d3dcompiler dwmapi)

# Source Files
file(GLOB SOURCES "src/*.cpp")

# Create Executable
add_executable(XaiOverlay ${SOURCES})

# Link Libraries
target_link_libraries(XaiOverlay ${LIBS})

# Note: User must provide ImGui source files in src/imgui or similar
# For this example, we assume ImGui is integrated or managed by the user
