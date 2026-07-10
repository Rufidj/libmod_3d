cmake_minimum_required(VERSION 3.5)
project(mod_3d)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "../../bin/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "../../bin/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "../../bin/")

set(CMAKE_MODULE_PATH ../../cmake/Modules)

# ============================================================================
# Platform detection
# ============================================================================

if(CMAKE_SYSTEM_NAME STREQUAL "VITA" OR VITA_BUILD)
    set(IS_VITA TRUE)
else()
    set(IS_VITA FALSE)
endif()

# ============================================================================
# Compiler flags
# ============================================================================

if(IS_VITA)
    add_definitions(-D__LIBMOD_3D -DVITA ${EXTRA_CFLAGS})
else()
    add_definitions(-D__LIBMOD_3D ${EXTRA_CFLAGS})
endif()

# ============================================================================
# SDL2 dependency
# ============================================================================

if(NOT EMSCRIPTEN AND NOT IS_VITA)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(OpenGL REQUIRED)
endif()

# ============================================================================
# Include directories
# ============================================================================

if(IS_VITA)
    include_directories(
        $ENV{VITASDK}/arm-vita-eabi/include
        ../../core/include
        ../../core/bgdrtm
        ../../modules/libbggfx
        ../../modules/libsdlhandler
        ../../vendor/sdl-gpu/include
        ${INCLUDE_DIRECTORIES}
    )
else()
    include_directories(
        ${SDL2_INCLUDE_DIR}
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIR}
        ${SDL2_image_INCLUDE_DIRS}
        ../../core/include
        ../../core/bgdrtm
        ../../modules/libbggfx
        ../../modules/libsdlhandler
        ../../vendor/sdl-gpu/include
        ${INCLUDE_DIRECTORIES}
    )
endif()

# ============================================================================
# Source files
# ============================================================================

set(SOURCES_LIBMOD_3D
    libmod_3d.c
    libmod_3d_math.c
    libmod_3d_shader.c
    libmod_3d_texture.c
    libmod_3d_mesh.c
    libmod_3d_gltf.c
    libmod_3d_obj.c
    libmod_3d_fbx.c
    ufbx.c
    libmod_3d_camera.c
    libmod_3d_renderer.c
    libmod_3d_scene.c
    libmod_3d_entity.c
    libmod_3d_material.c
    libmod_3d_light.c
    libmod_3d_primitives.c
    libmod_3d_terrain.c
    libmod_3d_paint.c
    libmod_3d_water.c
    libmod_3d_flow.c
    libmod_3d_particles.c
    libmod_3d_fire.c
    libmod_3d_sky.c
    libmod_3d_anim.c
    libmod_3d_collide.c
    libmod_3d_physics.c
    libmod_3d_mirror.c
    libmod_3d_instance.c
    libmod_3d_prefab.c
    libmod_3d_scenefile.c
    libmod_3d_pick.c
    libmod_3d_cloth.c
    libmod_3d_chunkterrain.c
    libmod_3d_fluidfill.c
    libmod_3d_watersim.c
    libmod_3d_cave.c
    libmod_3d_voxterrain.c
)

# Platform-specific sources (will add renderers later)
if(IS_VITA)
    # TODO: libmod_3d_renderer_vita.c
else()
    # TODO: libmod_3d_renderer_gpu.c
endif()

add_library(mod_3d ${LIBRARY_BUILD_TYPE} ${SOURCES_LIBMOD_3D})

# ============================================================================
# Linking
# ============================================================================

if(IS_VITA)
    target_link_libraries(mod_3d
        ${SDL_GPU_LIBRARY}
        vitaGL
        vitashark
        SceShaccCg_stub
        SDL2
        SDL2_image
        SceGxm_stub
        SceDisplay_stub
        -L../../bin
        bgdrtm
        bggfx
        sdlhandler
        ${STDLIBSFLAGS}
        -lm
    )
else()
    target_link_libraries(mod_3d
        ${SDL2_LIBRARY}
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARY}
        ${SDL2_image_LIBRARIES}
        ${SDL_GPU_LIBRARY}
        ${OPENGL_LIBRARIES}
        -L../../bin
        bgdrtm
        bggfx
        sdlhandler
        ${STDLIBSFLAGS}
        -lm
    )
endif()
