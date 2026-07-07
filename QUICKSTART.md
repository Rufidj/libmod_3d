# libmod_3d Quick Start Guide

Get a basic 3D scene running in 5 minutes.

## Step 1: Basic Initialization

```prg
import "libmod_3d";

program hello_3d;
begin
    // Initialize engine: 1920x1080 resolution
    G3D_INIT(1920, 1080);
    
    printf("G3D: Engine ready\n");
    
    // Cleanup
    G3D_SHUTDOWN();
end
```

**Run:**
```bash
bgdc hello_3d.prg -o hello_3d.dcb
bgdplay hello_3d.dcb
```

---

## Step 2: Add Camera & Lighting

```prg
import "libmod_3d";

program scene_setup;
begin
    G3D_INIT(1920, 1080);
    
    // Create scene
    scene = G3D_SCENE_CREATE("MainLevel");
    G3D_SCENE_SET_ACTIVE(scene);
    
    // Setup camera (FPS-style: 10 units up, 20 units away)
    camera = G3D_CAMERA_CREATE();
    G3D_CAMERA_SET_ACTIVE(camera);
    G3D_CAMERA_SET_POSITION(camera, 0, 10, 20);
    G3D_CAMERA_LOOK_AT(camera, 0, 0, 0,   // Look at origin
                                  0, 1, 0);  // Up vector
    
    // Setup lighting: Sun (directional light)
    sun = G3D_LIGHT_CREATE(G3D_LIGHT_DIRECTIONAL, 1.0, 1.0, 0.95);
    G3D_LIGHT_SET_POSITION(sun, 100, 200, 100);
    G3D_LIGHT_SET_INTENSITY(sun, 1.2);
    
    // Ambient light (fill light for shadows)
    G3D_SET_AMBIENT_LIGHT(0.4, 0.4, 0.4, 1.0);
    
    printf("Scene: %d, Camera: %d, Sun: %d\n", scene, camera, sun);
    
    G3D_SHUTDOWN();
end
```

---

## Step 3: Add Entities (TODO: Models pending)

Once glTF/MD3 loading is ready:

```prg
import "libmod_3d";

program scene_with_entities;
local
    int scene, camera, cube, sphere;
end
begin
    G3D_INIT(1920, 1080);
    
    scene = G3D_SCENE_CREATE("MainLevel");
    G3D_SCENE_SET_ACTIVE(scene);
    
    // Create camera
    camera = G3D_CAMERA_CREATE();
    G3D_CAMERA_SET_ACTIVE(camera);
    G3D_CAMERA_SET_POSITION(camera, 0, 10, 20);
    G3D_CAMERA_LOOK_AT(camera, 0, 0, 0, 0, 1, 0);
    
    // Create lighting
    sun = G3D_LIGHT_CREATE(G3D_LIGHT_DIRECTIONAL, 1.0, 1.0, 0.95);
    G3D_LIGHT_SET_POSITION(sun, 100, 200, 100);
    G3D_LIGHT_ENABLE_SHADOW(sun, 1);                    // Enable shadows!
    G3D_LIGHT_SET_SHADOW_QUALITY(sun, 2048);            // 2048x2048 shadow map
    
    // TODO: Load models
    // cube_model = G3D_LOAD_GLTF("models/cube.glb");
    // sphere_model = G3D_LOAD_GLTF("models/sphere.glb");
    
    // TODO: Spawn entities
    // cube = G3D_ENTITY_SPAWN(scene, cube_model, 0, 0, 0);
    // sphere = G3D_ENTITY_SPAWN(scene, sphere_model, 5, 0, 0);
    
    // Render loop
    loop
        G3D_RENDER();
        frame;
    end
    
    G3D_SHUTDOWN();
end
```

---

## Step 4: Physics & Animation (TODO)

```prg
// TODO: Physics example
// body = G3D_PHYSICS_BODY_CREATE(entity, G3D_SHAPE_SPHERE, mass, radius);
// G3D_PHYSICS_BODY_SET_VELOCITY(body, 0, -10, 0);  // Falling
// G3D_PHYSICS_STEP(delta_time);
```

---

## Common Patterns

### Third-Person Camera (Following Entity)

```prg
process camera_follow(int cam_id, int entity_id)
local
    float x, y, z;
    float dist = 10;
    float height = 5;
end
begin
    loop
        G3D_ENTITY_GET_POSITION(entity_id, x, y, z);
        
        // Position camera 10 units behind, 5 units up
        G3D_CAMERA_SET_POSITION(cam_id, x, y+height, z+dist);
        G3D_CAMERA_LOOK_AT(cam_id, x, y+height, z, 0, 1, 0);
        
        frame;
    end
end
```

### FPS Camera (Look with mouse)

```prg
process camera_fps(int cam_id)
local
    float yaw = 0, pitch = 0;
    float speed = 50;
end
begin
    loop
        // Rotate
        if (key(_left))  yaw -= 2; end
        if (key(_right)) yaw += 2; end
        if (key(_up))    pitch -= 2; end
        if (key(_down))  pitch += 2; end
        
        // Clamp pitch [-90, 90]
        if (pitch > 90) pitch = 90; end
        if (pitch < -90) pitch = -90; end
        
        // TODO: Update camera with Euler angles
        // G3D_CAMERA_SET_EULER(cam_id, pitch, yaw, 0);
        
        frame;
    end
end
```

### Dynamic Point Light (Torch)

```prg
process torch_light(int scene, int entity_id)
local
    int light;
    float x, y, z;
    float flicker_time = 0;
end
begin
    G3D_SCENE_SET_ACTIVE(scene);
    
    light = G3D_LIGHT_CREATE(G3D_LIGHT_POINT, 1.0, 0.7, 0.3);
    G3D_LIGHT_SET_RANGE(light, 50);
    G3D_LIGHT_ENABLE_SHADOW(light, 1);
    
    loop
        // Update light position to follow entity
        G3D_ENTITY_GET_POSITION(entity_id, x, y, z);
        G3D_LIGHT_SET_POSITION(light, x, y+2, z);
        
        // Flicker effect (horror!)
        flicker_time += 0.016;  // Assuming 60 FPS
        float flicker = 0.9 + sin(flicker_time * 5) * 0.1;
        G3D_LIGHT_SET_INTENSITY(light, flicker);
        
        frame;
    end
end
```

---

## Constants Reference

```prg
/* Light types */
G3D_LIGHT_DIRECTIONAL = 0    // Sun, distant light
G3D_LIGHT_POINT = 1          // Bulb, torch
G3D_LIGHT_SPOT = 2           // Spotlight (TODO)

/* Camera projections */
G3D_PROJECTION_PERSPECTIVE = 0
G3D_PROJECTION_ORTHOGRAPHIC = 1

/* Physics shapes */
G3D_SHAPE_SPHERE = 0
G3D_SHAPE_BOX = 1
G3D_SHAPE_CAPSULE = 2
```

---

## Next Steps

1. **Model Loading** — When `G3D_LOAD_GLTF()` is ready, load your models
2. **Animation** — Play skeletal animations with `G3D_ENTITY_SET_ANIMATION()`
3. **Physics** — Add gravity and collisions
4. **Shadow Tweaking** — Adjust shadow quality/bias for your scenes
5. **Post-Processing** — Add fog, effects (future feature)

---

## Debugging

Enable wireframe mode:
```prg
G3D_SET_WIREFRAME_MODE(1);
```

Show normals (when renderer ready):
```prg
// G3D_SHOW_NORMALS(1);
```

Check entity position:
```prg
float x, y, z;
G3D_ENTITY_GET_POSITION(entity, x, y, z);
printf("Entity at: %.1f, %.1f, %.1f\n", x, y, z);
```

---

## Common Issues

**"G3D: Engine already initialized"**
- You called `G3D_INIT()` twice. Only init once per program.

**Light not casting shadows**
- Did you call `G3D_LIGHT_ENABLE_SHADOW(light, 1)`?
- Shadow resolution must be power-of-2: 1024, 2048, 4096

**Camera not moving**
- Did you call `G3D_CAMERA_SET_ACTIVE(cam)`?
- Make sure your camera is in the active scene.

---

## Performance Tips

- **Limit shadow lights** — Only 1-2 lights should cast shadows on PC
- **Use LOD** — Reduce triangle count for distant objects (future)
- **Batch similar objects** — Will be automated (future)
- **Limit entity count** — Start with <1000 entities per scene

---

Enjoy building! 🚀
