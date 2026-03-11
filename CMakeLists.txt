cmake_minimum_required(VERSION 3.13)
project(HologramDisplay LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# rpi-rgb-led-matrix library (built via its own Makefile, linked as static lib)
# ---------------------------------------------------------------------------
set(RGB_LIB_DIR ${CMAKE_SOURCE_DIR}/lib/rpi-rgb-led-matrix)
set(RGB_LIB     ${RGB_LIB_DIR}/lib/librgbmatrix.a)
set(RGB_INCLUDE ${RGB_LIB_DIR}/include)

add_custom_command(
    OUTPUT  ${RGB_LIB}
    COMMAND make -C ${RGB_LIB_DIR}/lib
    COMMENT "Building rpi-rgb-led-matrix library"
)
add_custom_target(rgbmatrix_build DEPENDS ${RGB_LIB})

add_library(rgbmatrix STATIC IMPORTED)
set_target_properties(rgbmatrix PROPERTIES
    IMPORTED_LOCATION ${RGB_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${RGB_INCLUDE}
)
add_dependencies(rgbmatrix rgbmatrix_build)

# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
add_executable(hologram_display
    src/main.cpp
    src/Config.cpp
    src/Protocol.cpp
    src/FrameBuffer.cpp
    src/UDPReceiver.cpp
    src/HallSensor.cpp
    src/LEDOutput.cpp
)

target_include_directories(hologram_display PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${RGB_INCLUDE}
)

target_link_libraries(hologram_display PRIVATE
    rgbmatrix
    pthread
    gpiod
)

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------
add_executable(test_led tests/test_led.cpp src/Config.cpp)
target_include_directories(test_led PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${RGB_INCLUDE}
)
target_link_libraries(test_led PRIVATE rgbmatrix pthread)

add_executable(test_hall tests/test_hall.cpp src/Config.cpp)
target_include_directories(test_hall PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(test_hall PRIVATE gpiod pthread)
