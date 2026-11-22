cmake_minimum_required(VERSION 3.24)

project(tinychart VERSION 0.1.0 LANGUAGES CXX OBJCXX)

find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
find_library(CORE_GRAPHICS CoreGraphics REQUIRED)
find_library(CORE_TEXT CoreText REQUIRED)
find_library(IMAGE_IO ImageIO REQUIRED)
find_library(APP_KIT AppKit REQUIRED)

add_library(chart STATIC src/chart.cpp src/macos.cpp)
target_include_directories(chart PUBLIC include)
target_compile_features(chart PUBLIC cxx_std_20)
target_link_libraries(chart PRIVATE
    "${CORE_FOUNDATION}"
    "${CORE_GRAPHICS}"
    "${CORE_TEXT}"
    "${IMAGE_IO}"
)

add_executable(render_chart examples/render_chart.cpp)
target_link_libraries(render_chart PRIVATE chart)

add_executable(chart_demo MACOSX_BUNDLE examples/chart_demo.mm)
set_target_properties(chart_demo PROPERTIES
    OBJCXX_STANDARD 20
    OBJCXX_STANDARD_REQUIRED YES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.tinychart.demo
    MACOSX_BUNDLE_BUNDLE_NAME tinychart
)
target_compile_options(chart_demo PRIVATE -fobjc-arc)
target_link_libraries(chart_demo PRIVATE chart "${APP_KIT}")

foreach(target chart render_chart chart_demo)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
