cmake_minimum_required(VERSION 3.24)

project(cRenderer LANGUAGES C CXX)

find_package(Vulkan REQUIRED)
find_program(SLANGC slangc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)

add_executable(cREN)

file(GLOB_RECURSE cREN_SOURCES "src/*.c" "src/*.cpp")
target_sources(cREN PRIVATE ${cREN_SOURCES})
target_include_directories(cREN PUBLIC "include")
set_target_properties(cREN PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

# SDL
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN ON CACHE BOOL "" FORCE)
set(SDL_DUMMYAUDIO OFF CACHE BOOL "" FORCE)
set(SDL_DUMMYVIDEO OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_DEPS_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/sdl3 EXCLUDE_FROM_ALL)

# VOLK
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY OFF CACHE BOOL "" FORCE)

if(WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR CACHE STRING "" FORCE)
elseif(UNIX AND NOT APPLE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_WAYLAND_KHR CACHE STRING "" FORCE)
endif()

add_subdirectory(extern/volk EXCLUDE_FROM_ALL)

# VMA
set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)

add_subdirectory(extern/vma EXCLUDE_FROM_ALL)

# Linking
target_link_libraries(cREN PRIVATE
    SDL3::SDL3-static
    volk::volk
    GPUOpen::VulkanMemoryAllocator
)

target_compile_definitions(cREN PRIVATE
    VK_NO_PROTOTYPES=1
    WIN32_LEAN_AND_MEAN=1
    NOMINMAX=1
    CREN_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
)

# Address sanitizer
option(CREN_ENABLE_ASAN "Enable Address Sanitizer" OFF)

if(CREN_ENABLE_ASAN)
    if(MSVC)
        target_compile_options(/fsanitize=address /Zi)
    endif()
endif()

# Compile shaders
set(CREN_SHADER_SRC "${CMAKE_SOURCE_DIR}/shaders/triangle.slang")
set(CREN_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders/triangle.spv")

# Shader sources
set(CREN_SHADERS triangle post)
set(CREN_SHADER_OUTPUTS "")

foreach(name IN LISTS CREN_SHADERS)
    set(src "${CMAKE_SOURCE_DIR}/shaders/${name}.slang")
    set(out "${CMAKE_BINARY_DIR}/shaders/${name}.spv")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${SLANGC} "${src}" -target spirv -profile spirv_1_5 -fvk-use-scalar-layout -o "${out}"
        DEPENDS "${src}"
        COMMENT "Compiling ${name}.slang"
        VERBATIM
    )
    list(APPEND CREN_SHADER_OUTPUTS "${out}")
endforeach()

add_custom_target(cREN_shaders DEPENDS ${CREN_SHADER_OUTPUTS})
add_dependencies(cREN cREN_shaders)

# if(MSVC)
# target_compile_options(cREN PRIVATE /W4 /we4013)
# endif()
