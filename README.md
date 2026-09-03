# libmod_3d - Modern 3D Rendering Engine for BennuGD2

A forward-rendering 3D engine for BennuGD2 with real-time shadow mapping support.

**Status:** MVP Core (Initialization, Scene Graph, Camera, Lighting, Physics)  
**Target:** PC/Linux/Windows (OpenGL 3.3+) — Future: Android/VITA downgrade  
**Features:** Multi-scene management, dynamic lighting with shadows, entity hierarchy, glTF/MD3 support

## Features (MVP)

- ✅ Scene graph with hierarchical transforms
- ✅ Multiple cameras (perspective, orthographic)
- ✅ Dynamic lighting (directional, point, spot)
- ✅ Shadow mapping ready (implementation pending)
- ✅ Material system (Phong-based)
- ✅ Physics bodies (gravity, velocity, collision)
- ✅ Asset loading stubs (glTF, MD3, textures)
- ⏳ Forward rendering pipeline (under development)

## Sprites 2D en el mundo 3D (estilo HD-2D / Octopath)

Un sprite es un **proceso de BennuGD2 de toda la vida**, pero puesto en el espacio 3D:
mira siempre a la camara, se dibuja en el pase opaco con recorte alfa (asi tapa y es
tapado por la geometria sin ordenar nada) y proyecta su silueta en el mapa de sombras.
La textura es **el mismo grafico de BennuGD2** (`map_load` / `fpg_load`), sin cargarlo
dos veces ni inventar formatos.

```prg
PROCESS heroe()
BEGIN
    ctype = C_3D;  csubtype = C3D_SPRITE;
    entity = g3d_sprite_create(scene, 0.0, 0.0, 0.0);
    g3d_sprite_set_height(entity, 3.5);      // alto en unidades de mundo
    file = 0;  graph = hoja;                 // el grafico manda, como en 2D
    sprite.cols = 8;  sprite.rows = 4;       // hoja de sprites partida en celdas
    LOOP
        x += 0.1;  angle = rumbo;            // se mueve por el mundo 3D
        // 16 posturas: la direccion se calcula respecto a la CAMARA
        sprite.frame = g3d_sprite_dir(entity, angle, 16) * pasos + paso;
        FRAME;
    END
END
```

- **Locales:** `x/y/z` (posicion en el mundo), `angle`, `size`, `alpha`, `color_r/g/b`,
  `flags` (1 = espejo) y `file`/`graph`. Animar = cambiar `graph` (FPG) o
  `sprite.frame` (hoja de sprites).
- **Anclaje:** el punto de control 0 del grafico (`center_set`) es el que se planta en
  x,y,z — ponlo en los pies y el personaje se apoya solo en el suelo.
- **Luz:** `g3d_sprite_set_lit(entity, 1)` (por defecto) hace que el sprite reciba la
  luz ambiental y la del sol de la escena, para que se apague de noche.
- **Colision movil:** `g3d_collider_add_box` + `g3d_collider_set_box(caja, ...)` para
  un NPC que corta el paso mientras anda (`g3d_collider_remove_box` lo quita).
- **Funciones:** `g3d_sprite_create`, `g3d_sprite_set_height`,
  `g3d_sprite_set_pixels_per_unit`, `g3d_sprite_set_anchor`, `g3d_sprite_set_billboard`
  (`G3D_SPRITE_UPRIGHT` / `G3D_SPRITE_FACING`), `g3d_sprite_set_cutout`,
  `g3d_sprite_set_smooth`, `g3d_sprite_set_shadow`, `g3d_sprite_set_cell`,
  `g3d_sprite_dir`, `g3d_sprite_destroy`.

## Quick Start

```prg
import "libmod_3d";

program my_3d_game;
begin
    // Initialize
    G3D_INIT(1920, 1080);
    
    // Create scene
    scene_id = G3D_SCENE_CREATE("Level1");
    G3D_SCENE_SET_ACTIVE(scene_id);
    
    // Create camera
    cam_id = G3D_CAMERA_CREATE();
    G3D_CAMERA_SET_ACTIVE(cam_id);
    G3D_CAMERA_SET_POSITION(cam_id, 0, 5, 10);
    G3D_CAMERA_LOOK_AT(cam_id, 0, 0, 0, 0, 1, 0);
    
    // Create lighting
    sun_id = G3D_LIGHT_CREATE(G3D_LIGHT_DIRECTIONAL, 1.0, 1.0, 0.95);
    G3D_LIGHT_SET_POSITION(sun_id, 100, 200, 100);
    G3D_LIGHT_ENABLE_SHADOW(sun_id, 1);
    
    // Render loop
    loop
        G3D_RENDER();
        frame;
    end
end
```

## Architecture

```
libmod_3d/
├── libmod_3d.c              Core (init, lifecycle, main render loop)
├── libmod_3d_scene.c        Scene graph management
├── libmod_3d_camera.c       Camera system
├── libmod_3d_renderer.c     Forward rendering pipeline
├── libmod_3d_shadow.c       Shadow mapping
├── libmod_3d_asset.c        Asset loading (glTF, MD3, textures)
├── libmod_3d_material.c     Material system
├── libmod_3d_light.c        Lighting
├── libmod_3d_physics.c      Physics simulation
└── shaders/
    ├── phong.vert           Main lighting shader
    ├── phong.frag
    ├── shadow.vert          Shadow pass
    └── shadow.frag
```

## Rendering Pipeline

### Current Phase
- Scene graph initialization
- Camera management
- Entity spawning/positioning
- Light creation/configuration

### Phase 2: Forward Rendering
1. **Frustum Culling** — Cull entities outside camera view
2. **Shadow Pass** — Render depth from light perspective
3. **Forward Rendering** — Render opaque geometry with Phong lighting
4. **Transparency** — Back-to-front sorted transparent objects
5. **Post-processing** — Fog, effects (future)

### Platform Support

| Platform | Status | Features |
|----------|--------|----------|
| PC/Linux | MVP | Full OpenGL 3.3+, shadow maps 4096x4096 |
| Android | Future | GLES2, simplified lighting, LOD |
| VITA | Future | VitaGL, aggressive optimization |

## API Overview

### Core
```
G3D_INIT(width, height)                    Initialize engine
G3D_SHUTDOWN()                              Cleanup
G3D_RENDER()                                Render current frame
```

### Scenes
```
scene_id = G3D_SCENE_CREATE(name)
G3D_SCENE_SET_ACTIVE(scene_id)
G3D_SCENE_DESTROY(scene_id)
```

### Entities
```
entity_id = G3D_ENTITY_SPAWN(scene, model, x, y, z)
G3D_ENTITY_SET_POSITION(entity, x, y, z)
G3D_ENTITY_SET_ROTATION(entity, pitch, yaw, roll)
G3D_ENTITY_SET_SCALE(entity, sx, sy, sz)
G3D_ENTITY_SET_PARENT(entity, parent)  // Hierarchies
```

### Cameras
```
cam = G3D_CAMERA_CREATE()
G3D_CAMERA_SET_ACTIVE(cam)
G3D_CAMERA_SET_POSITION(cam, x, y, z)
G3D_CAMERA_LOOK_AT(cam, tx, ty, tz, ux, uy, uz)
G3D_CAMERA_SET_FOV(cam, 75)
```

### Lighting
```
light_id = G3D_LIGHT_CREATE(type, r, g, b)    // type: DIRECTIONAL, POINT, SPOT
G3D_LIGHT_SET_POSITION(light, x, y, z)
G3D_LIGHT_SET_INTENSITY(light, value)
G3D_LIGHT_ENABLE_SHADOW(light, 1)
G3D_LIGHT_SET_SHADOW_QUALITY(light, 2048)     // 1024, 2048, 4096
```

### Physics
```
body = G3D_PHYSICS_BODY_CREATE(entity, shape, mass, radius)
G3D_PHYSICS_BODY_SET_VELOCITY(body, vx, vy, vz)
G3D_PHYSICS_STEP(delta_time)
```

### Materials
```
mat = G3D_MATERIAL_CREATE()
G3D_MATERIAL_SET_COLOR(mat, r, g, b, a)
G3D_MATERIAL_SET_METALLIC(mat, 0.5)
G3D_MATERIAL_SET_ROUGHNESS(mat, 0.3)
```

## Development Roadmap

### Phase 1 (Current): MVP Core
- [x] Module structure & build integration
- [x] Scene graph
- [x] Entity management
- [x] Camera system
- [x] Lighting (creation, configuration)
- [x] Material system (stubs)
- [x] Physics (stubs)
- [ ] Shaders (Phong, Shadow)
- [ ] Forward rendering pipeline

### Phase 2: Rendering & Assets
- [ ] glTF loader (using cgltf.h)
- [ ] MD3 loader
- [ ] Texture loading (PNG, JPG)
- [ ] Forward rendering implementation
- [ ] Shadow mapping (PCF, soft shadows)
- [ ] Mesh rendering pipeline

### Phase 3: Polish
- [ ] Animation playback
- [ ] Fog/atmosphere
- [ ] Billboard particles
- [ ] Skybox support
- [ ] Debug visualization (normals, bboxes, wireframe)

### Phase 4: Optimization & Downgrade
- [ ] Android/GLES2 support
- [ ] VITA/VitaGL support
- [ ] Texture compression
- [ ] LOD system
- [ ] Instancing

## Dependencies

- **BennuGD2** core (bgdrtm, bggfx)
- **SDL2** (cross-platform window/input)
- **SDL_gpu** (GPU rendering abstraction)
- **cgltf.h** (glTF parsing, included from libmod_ray)
- **OpenGL 3.3+** (for PC/Linux)

## Building

```bash
cd /path/to/BennuGD2
./build.sh                  # Builds all modules including libmod_3d
```

Or specific module:
```bash
cd build/linux-gnu
cmake -DENABLE_MOD_3D=1 ..
make mod_3d
```

## Testing

Basic test:
```bash
cat > test_init.prg << 'EOF'
import "libmod_3d";
program test;
begin
    if (G3D_INIT(800, 600)) {
        printf("G3D: Engine initialized\n");
        G3D_SHUTDOWN();
    end
end
EOF

bgdc test_init.prg -o test_init.dcb
bgdplay test_init.dcb
```

## Architecture Decisions

### Why Forward Rendering (MVP)?
- Simpler to implement than deferred
- Supports transparency naturally
- Suitable for PC + mobile downgrade path
- Easier to add shadows as post-process

### Why Phong (not PBR)?
- Faster on lower-end hardware
- Easier to understand/debug
- PBR path available in Phase 4

### Why Shadow Mapping?
- Industry standard since 2000s
- Hardware-accelerated on all target platforms
- Scales from 1-light (VITA) to 4+ lights (PC)

## Known Limitations

- **No deferred rendering yet** (forward only)
- **No instancing/batching yet** (will add in optimization phase)
- **Transparency sorting is naive** (back-to-front, no deep sorting)
- **Physics is basic** (no constraints, only gravity + collision)
- **No animation blending yet** (play one animation at a time)

## Contributing

New renderer backends should follow this structure:
- `libmod_3d_renderer_<platform>.c` (GPU interface)
- `shaders/<platform>/` (platform-specific shader variants)
- Update CMakeLists.txt with platform detection

## References

- libmod_ray (existing 3D module, raycasting-based)
- libmod_gfx (2D graphics module)
- BennuGD2 module system (bgddl.h, dlvaracc.h)
- OpenGL 3.3 documentation
- Shadow Mapping techniques (GPU Gems 3)
