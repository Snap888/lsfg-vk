cmake_minimum_required(VERSION 3.22)
project(lsfg_vk VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Android NDK settings ───────────────────────────────────────────────────
if(ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--exclude-libs,ALL")
    add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR=1)
    add_definitions(-DANDROID=1)
endif()

# ─── Vulkan headers ─────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)

# ─── SPIR-V shader compilation ──────────────────────────────────────────────
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS $ENV{ANDROID_NDK}/shader-tools/linux-x86_64
          /usr/bin /usr/local/bin)

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "glslangValidator not found — skipping shader compilation. "
                    "Pre-compiled .spv expected in shaders/compiled/")
endif()

set(SHADER_SRC_DIR  ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR  ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

set(SHADER_SOURCES
    ${SHADER_SRC_DIR}/optical_flow_pyramid.comp
    ${SHADER_SRC_DIR}/optical_flow_warp.comp
    ${SHADER_SRC_DIR}/optical_flow_blend.comp
    ${SHADER_SRC_DIR}/motion_vectors.comp
    ${SHADER_SRC_DIR}/ui_mask.comp
)

set(SHADER_HEADERS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SPV_FILE  ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    set(INCL_FILE ${SHADER_OUT_DIR}/${SHADER_NAME}_spv.h)

    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT ${SPV_FILE} ${INCL_FILE}
            COMMAND ${GLSLANG_VALIDATOR}
                    --target-env vulkan1.1
                    -V ${SHADER}
                    -o ${SPV_FILE}
            COMMAND ${CMAKE_COMMAND}
                    -DINPUT=${SPV_FILE}
                    -DOUTPUT=${INCL_FILE}
                    -DVAR_NAME=${SHADER_NAME}_spv
                    -P ${CMAKE_SOURCE_DIR}/cmake/SpvToHeader.cmake
            DEPENDS ${SHADER}
            COMMENT "Compiling shader ${SHADER_NAME}"
        )
    else()
        # Fall back to pre-compiled shaders
        add_custom_command(
            OUTPUT ${SPV_FILE} ${INCL_FILE}
            COMMAND ${CMAKE_COMMAND} -E copy
                    ${CMAKE_SOURCE_DIR}/shaders/compiled/${SHADER_NAME}.spv ${SPV_FILE}
            COMMAND ${CMAKE_COMMAND}
                    -DINPUT=${SPV_FILE}
                    -DOUTPUT=${INCL_FILE}
                    -DVAR_NAME=${SHADER_NAME}_spv
                    -P ${CMAKE_SOURCE_DIR}/cmake/SpvToHeader.cmake
            DEPENDS ${CMAKE_SOURCE_DIR}/shaders/compiled/${SHADER_NAME}.spv
        )
    endif()
    list(APPEND SHADER_HEADERS ${INCL_FILE})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_HEADERS})

# ─── Layer sources ───────────────────────────────────────────────────────────
set(LAYER_SOURCES
    layer/layer.cpp
    layer/swapchain_manager.cpp
    layer/frame_generator.cpp
    layer/optical_flow.cpp
    layer/present_scheduler.cpp
)

add_library(VkLayer_lsfg_frame_generation SHARED ${LAYER_SOURCES})

add_dependencies(VkLayer_lsfg_frame_generation shaders)

target_include_directories(VkLayer_lsfg_frame_generation PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/layer
    ${CMAKE_BINARY_DIR}/shaders      # generated SPIR-V headers
    ${Vulkan_INCLUDE_DIRS}
)

# ─── Compiler flags ──────────────────────────────────────────────────────────
target_compile_options(VkLayer_lsfg_frame_generation PRIVATE
    -O3
    -fvisibility=hidden
    -fno-exceptions
    -Wall -Wextra
    -Wno-unused-parameter
)

if(ANDROID)
    target_compile_options(VkLayer_lsfg_frame_generation PRIVATE
        -march=armv8-a+simd
        -mtune=cortex-a76          # good default for Adreno 7xx SoCs
    )
    target_link_libraries(VkLayer_lsfg_frame_generation
        android
        log
    )
endif()

# ─── Strip release binary ────────────────────────────────────────────────────
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET VkLayer_lsfg_frame_generation POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded
                $<TARGET_FILE:VkLayer_lsfg_frame_generation>
        COMMENT "Stripping release binary"
    )
endif()

# ─── Copy manifest ───────────────────────────────────────────────────────────
configure_file(
    ${CMAKE_SOURCE_DIR}/manifest/VkLayer_lsfg_frame_generation.json
    ${CMAKE_BINARY_DIR}/VkLayer_lsfg_frame_generation.json
    COPYONLY
)
