/*
 * libmod_3d.h - Modern 3D Rendering Engine for BennuGD2
 *
 * Forward rendering engine with shadow mapping support
 * Target platforms: PC/Linux/Windows (OpenGL 3.3+)
 * Future: Android/VITA with graceful degradation
 */

#ifndef __LIBMOD_3D_H
#define __LIBMOD_3D_H

#include <stdint.h>
#include <math.h>
#include "libmod_3d_math.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_gltf.h"
#include "libmod_3d_camera.h"
#include "libmod_3d_renderer.h"

#include "bgddl.h"
#include "libbggfx.h"
#include "dlvaracc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------- */

#define C_3D        2

/* --------------------------------------------------------------------------- */

#if !defined(__BGDC__)
enum {
    CSUBTYPE = 0,
    COORDX,
    COORDY,
    COORDZ,
    ANGLE_X,
    ANGLE_Y,
    ANGLE_Z,
    SIZE,
    SIZE_X,
    SIZE_Y,
    SIZE_Z,
    ENTITY
};
#endif

/* ============================================================================
   CONSTANTS
   ============================================================================
 */

#define G3D_MAX_ENTITIES      4096
#define G3D_MAX_SCENES        16
#define G3D_MAX_CAMERAS       16
#define G3D_MAX_LIGHTS        32
#define G3D_MAX_MATERIALS     256
#define G3D_MAX_MODELS        512
#define G3D_MAX_TEXTURES      1024
#define G3D_MAX_ANIMATIONS    256

/* Light types */
#define G3D_LIGHT_DIRECTIONAL 0
#define G3D_LIGHT_POINT       1
#define G3D_LIGHT_SPOT        2

/* Camera projection types */
#define G3D_PROJECTION_PERSPECTIVE 0
#define G3D_PROJECTION_ORTHOGRAPHIC 1

/* Entity shapes (for physics) */
#define G3D_SHAPE_SPHERE  0
#define G3D_SHAPE_BOX     1
#define G3D_SHAPE_CAPSULE 2

/* ============================================================================
   MATERIALS & TEXTURES
   ============================================================================
 */

typedef struct {
    int id;
    char *name;

    /* Phong/PBR properties */
    Vec3 albedo;           /* Base color */
    Vec3 specular;         /* Specular color */
    float shininess;       /* Specular exponent (Phong) */
    float metallic;        /* 0=dielectric, 1=metal (future PBR) */
    float roughness;       /* 0=glossy, 1=rough (future PBR) */
    float alpha;           /* Global alpha multiplier */

    /* Textures */
    int albedo_texture_id;     /* -1 if none */
    int normal_texture_id;     /* -1 if none */
    int roughness_texture_id;  /* -1 if none */
    int metallic_texture_id;   /* -1 if none */

    /* Rendering state */
    int blend_mode;        /* 0=opaque, 1=alpha, 2=additive */
    int cull_face;         /* 0=back, 1=front, 2=none */

} G3D_Material;

typedef struct {
    int id;
    char *name;
    int width, height;
    int channels;          /* 3=RGB, 4=RGBA */
    unsigned char *data;   /* Raw pixel data (owned by texture) */

    /* GPU handle (renderer-specific) */
    void *gpu_handle;      /* GL texture handle, etc */
} G3D_Texture;

/* ============================================================================
   ANIMATION
   ============================================================================
 */

typedef struct {
    char *name;
    int start_frame;
    int end_frame;
    float duration;        /* In seconds */
    int loop;              /* 1=looping, 0=one-shot */
} G3D_Animation;

/* ============================================================================
   MESH & MODEL
   ============================================================================
 */

typedef struct {
    int vertex_count;
    int index_count;

    /* Vertex attributes (interleaved, 32-byte vertex) */
    float *positions;      /* 3 floats per vertex */
    float *normals;        /* 3 floats per vertex */
    float *texcoords;      /* 2 floats per vertex */
    uint32_t *indices;     /* Index buffer */

    /* Material */
    int material_id;

    /* GPU handle */
    void *vao;             /* Vertex Array Object */
    void *vbo;             /* Vertex Buffer Object */
    void *ebo;             /* Element Buffer Object */
} G3D_Mesh;

typedef struct {
    int id;
    char *name;

    int mesh_count;
    G3D_Mesh *meshes;

    int animation_count;
    G3D_Animation *animations;

    /* Bounding box */
    Vec3 aabb_min, aabb_max;

    /* GPU handles */
    void *gpu_handle;
} G3D_Model;

/* ============================================================================
   TRANSFORM & ENTITY
   ============================================================================
 */

typedef struct {
    Vec3 position;
    Quat rotation;         /* Quaternion for smooth interpolation */
    Vec3 scale;

    /* Euler angles (cached for convenience) */
    float pitch, yaw, roll;

    /* Computed matrices */
    Mat4 model_matrix;
    int dirty;             /* Needs matrix recomputation */

    /* Hierarchy */
    int parent_id;         /* -1 if root */
} G3D_Transform;

typedef struct {
    int id;
    int scene_id;

    char *name;

    /* Components */
    G3D_Transform transform;
    int model_id;          /* -1 if none */
    int material_override_id;  /* -1 if use model's materials */

    /* Animation state */
    int current_animation;
    float animation_time;
    float animation_speed;
    int animation_playing;

    /* Physics */
    int physics_body_id;   /* -1 if no physics */

    /* Visibility */
    int visible;
    int cast_shadow;

    /* User data */
    int user_data;
} G3D_Entity;

/* ============================================================================
   LIGHTING
   ============================================================================
 */

typedef struct {
    int id;
    int type;              /* DIRECTIONAL, POINT, SPOT */

    Vec3 color;
    float intensity;

    /* Position/Direction */
    Vec3 position;
    Vec3 direction;        /* For directional/spot */

    /* Range (for point/spot) */
    float range;

    /* Spot-specific */
    float cone_angle;      /* Inner cone angle */
    float outer_cone_angle;

    /* Shadow mapping */
    int cast_shadow;
    int shadow_map_id;     /* Texture ID of shadow map, -1 if none */
    int shadow_resolution; /* 1024, 2048, 4096 */
    Mat4 light_space_matrix;
    void *shadow_framebuffer;  /* GPU framebuffer handle */

    /* Visibility */
    int enabled;
} G3D_Light;

/* ============================================================================
   CAMERA
   ============================================================================
 */

typedef struct {
    int id;
    int active;

    Vec3 position;
    Vec3 target;           /* Look-at point */
    Vec3 up;               /* Up vector */

    /* Euler angles (alternative to target) */
    float pitch, yaw, roll;

    /* Projection */
    int projection_type;   /* PERSPECTIVE or ORTHOGRAPHIC */
    float fov;             /* Vertical FOV in degrees (perspective only) */
    float near_plane;
    float far_plane;
    float aspect_ratio;

    /* Orthographic specific */
    float ortho_width, ortho_height;

    /* Computed matrices */
    Mat4 view_matrix;
    Mat4 projection_matrix;
    int view_dirty, proj_dirty;
} G3D_Camera;

/* ============================================================================
   PHYSICS BODY
   ============================================================================
 */

typedef struct {
    int id;
    int entity_id;

    int shape;             /* SPHERE, BOX, CAPSULE */
    float mass;            /* 0 = static/kinematic */
    float radius;          /* For sphere, radius of capsule */
    Vec3 extents;          /* For box */

    Vec3 velocity;
    Vec3 acceleration;

    float friction;
    float restitution;
    float gravity_scale;
    float linear_damping;
    float angular_damping;

    int is_static;
    int is_kinematic;
    int is_trigger;

    int collision_layer;
    int collision_mask;
} G3D_PhysicsBody;

/* ============================================================================
   SCENE
   ============================================================================
 */

typedef struct {
    int id;
    char *name;

    int entity_count;
    G3D_Entity entities[G3D_MAX_ENTITIES];

    int light_count;
    G3D_Light lights[G3D_MAX_LIGHTS];

    int camera_count;
    G3D_Camera cameras[G3D_MAX_CAMERAS];
    int active_camera_id;

    int physics_body_count;
    G3D_PhysicsBody physics_bodies[G3D_MAX_ENTITIES];

    /* Rendering state */
    Vec3 ambient_light;
    float ambient_intensity;

    Vec3 fog_color;
    float fog_start, fog_end;
    int fog_enabled;

    Vec3 clear_color;
    float clear_alpha;
} G3D_Scene;

/* ============================================================================
   RENDERER & ENGINE STATE
   ============================================================================
 */

typedef struct {
    int initialized;

    int display_width;
    int display_height;

    /* Active scene */
    int active_scene_id;
    G3D_Scene scenes[G3D_MAX_SCENES];

    /* Assets */
    G3D_Model models[G3D_MAX_MODELS];
    int model_count;

    G3D_Texture textures[G3D_MAX_TEXTURES];
    int texture_count;

    G3D_Material materials[G3D_MAX_MATERIALS];
    int material_count;

    /* Renderer state */
    void *gpu_context;     /* SDL_gpu target or GL context */

    /* Debug flags */
    int wireframe_mode;
    int show_normals;
    int show_bounding_boxes;

    /* Performance */
    float delta_time;
    int frame_count;

} G3D_Engine;

/* ============================================================================
   GLOBAL STATE
   ============================================================================
 */

extern G3D_Engine g_3d_engine;

/* ============================================================================
   FUNCTION DECLARATIONS (Implemented in individual .c files)
   ============================================================================
 */

/* Core */
int g3d_init(int width, int height);
int g3d_shutdown(void);
int g3d_render(void);

/* Scenes */
int g3d_scene_create(const char *name);
int g3d_scene_destroy(int scene_id);
int g3d_scene_set_active(int scene_id);
int g3d_scene_get_active(void);

/* Entities */
int g3d_entity_spawn(int scene_id, int model_id, float x, float y, float z);
int g3d_entity_destroy(int entity_id);
int g3d_entity_set_position(int entity_id, float x, float y, float z);
int g3d_entity_set_rotation(int entity_id, float pitch, float yaw, float roll);
int g3d_entity_set_scale(int entity_id, float sx, float sy, float sz);
int g3d_entity_get_position(int entity_id, float *x, float *y, float *z);
int g3d_entity_set_parent(int entity_id, int parent_id);

/* Cameras */
int g3d_camera_create(void);
int g3d_camera_set_active(int camera_id);
int g3d_camera_set_position(int camera_id, float x, float y, float z);
int g3d_camera_look_at(int camera_id, float tx, float ty, float tz,
                       float ux, float uy, float uz);
int g3d_camera_set_projection(int camera_id, int projection_type);
int g3d_camera_set_fov(int camera_id, float fov);

/* Assets */
int g3d_model_load_gltf(const char *filename);
int g3d_model_load_md3(const char *filename);
int g3d_texture_load(const char *filename);

/* Lighting */
int g3d_light_create(int type, float r, float g, float b);
int g3d_light_set_position(int light_id, float x, float y, float z);
int g3d_light_set_direction(int light_id, float dx, float dy, float dz);
int g3d_light_set_intensity(int light_id, float intensity);
int g3d_light_set_range(int light_id, float range);
int g3d_light_enable_shadow(int light_id, int enabled);
int g3d_light_set_shadow_quality(int light_id, int resolution);

/* Materials */
int g3d_material_create(void);
int g3d_material_set_color(int material_id, float r, float g, float b, float a);
int g3d_material_set_metallic(int material_id, float value);
int g3d_material_set_roughness(int material_id, float value);

/* Physics */
int g3d_physics_body_create(int entity_id, int shape, float mass, float radius);
int g3d_physics_body_set_velocity(int body_id, float vx, float vy, float vz);
int g3d_physics_step(float delta_time);

/* Rendering config */
int g3d_set_clear_color(float r, float g, float b, float a);
int g3d_set_ambient_light(float r, float g, float b, float intensity);
int g3d_set_fog(int enabled, float r, float g, float b, float start, float end);
int g3d_set_wireframe_mode(int enabled);

/* Raycasting / Picking */
int g3d_raycast(float sx, float sy, float sz, float dx, float dy, float dz,
                float max_distance, int *hit_entity, float *hit_distance);

#ifdef __cplusplus
}
#endif

/* --------------------------------------------------------------------------- */

extern DLVARFIXUP __bgdexport( libmod_3d, locals_fixup )[];

#endif /* __LIBMOD_3D_H */
