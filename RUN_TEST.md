# Running libmod_3d with BennuGD

Complete instructions for testing libmod_3d in BennuGD2 runtime.

---

## Quick Start

### 1. Compile the Test Script

```bash
cd /home/ruben/BennuGD2/modules/libmod_3d
bgdc TEST_EXAMPLE.PRG
```

**Output:**
```
Compiling: TEST_EXAMPLE.PRG
Generating: TEST_EXAMPLE.DCB
✓ Compilation successful
```

### 2. Run the Test

```bash
bgdi TEST_EXAMPLE.DCB
```

**Expected Output:**
```
✓ 3D Engine initialized
✓ Scene created: 0
✓ Scene set as active
✓ Cube mesh created
⚠ Texture not loaded (file not found - OK for test)
✓ Material created: 0
✓ Material properties set (red, 30% metallic, 60% rough)
✓ Entity spawned at (0, 0, 5): 0
✓ Mesh assigned to entity
✓ Material linked to entity
✓ Light created: 0
✓ Light configured
✓ Rendering parameters set

=== Starting Render Loop ===
Press ESC to exit

G3D: Draw calls: 0, Triangles: 0, Culled: 0
... (repeats each frame)

5 second timeout - exiting
✓ Render loop completed
Shutting down...
✓ 3D Engine shut down

=== Test Complete ===
```

---

## Compilation Issues & Solutions

### Issue: "Cannot find module libmod_3d"

**Cause:** Module not in BennuGD library path

**Solution:**
```bash
# Make sure build.sh ran successfully
./build.sh

# Check if libmod_3d.so exists
ls -la build/linux-gnu/bin/libmod_3d.so

# If it doesn't exist, compile just the module:
cd modules/libmod_3d
make
```

### Issue: "IMPORT libmod_3d failed"

**Cause:** Function signatures don't match BennuGD system

**Check:** Verify libmod_3d_exports.h has:
```c
DLSYSFUNCS __bgdexport(libmod_3d, libmod_3d_functions)[] = {
    FUNC("G3D_INIT", "II", TYPE_INT, g3d_init_bgd),
    ...
    {NULL, 0, 0, 0, 0, 0, NULL}
};
```

---

## Step-by-Step Testing

### Test 1: Module Loading

**Script:**
```bennugd
PROCESS main()
    write("Importing libmod_3d...");
    IF (g3d_init(1920, 1080))
        write("✓ Module loaded and initialized");
    ELSE
        write("✗ Failed to load module");
    END
    g3d_shutdown();
END
```

**Expected:** Prints "✓ Module loaded"

---

### Test 2: Scene Management

**Script:**
```bennugd
IMPORT "libmod_3d"

GLOBAL scene;

PROCESS main()
    g3d_init(1920, 1080);
    
    scene = g3d_scene_create("TestScene");
    write("Scene created: " + STRING scene);
    
    IF (scene >= 0)
        g3d_scene_set_active(scene);
        write("✓ Scene management works");
    END
    
    g3d_shutdown();
END
```

**Expected:** Prints scene ID (should be 0 for first scene)

---

### Test 3: Entity Creation

**Script:**
```bennugd
IMPORT "libmod_3d"

GLOBAL scene, entity;

PROCESS main()
    g3d_init(1920, 1080);
    scene = g3d_scene_create("Test");
    g3d_scene_set_active(scene);
    
    entity = g3d_entity_spawn(scene, 0, 0, 0, 0);
    
    IF (entity >= 0)
        write("✓ Entity created: " + STRING entity);
        
        // Test transform
        g3d_entity_set_position(entity, 5, 10, -5);
        g3d_entity_set_rotation(entity, 45, 90, 0);
        g3d_entity_set_scale(entity, 2, 2, 2);
        write("✓ Entity transforms work");
    END
    
    g3d_shutdown();
END
```

**Expected:** Entity ID 0, transform messages

---

### Test 4: Materials & Mesh

**Script:**
```bennugd
IMPORT "libmod_3d"

GLOBAL scene, entity, material, mesh;

PROCESS main()
    g3d_init(1920, 1080);
    scene = g3d_scene_create("Test");
    g3d_scene_set_active(scene);
    
    // Create mesh
    mesh = g3d_primitive_cube();
    IF (mesh >= 0)
        write("✓ Cube mesh created");
    END
    
    // Create material
    material = g3d_material_create();
    g3d_material_set_color(material, 1.0, 0.0, 0.0, 1.0);
    write("✓ Material created with red color");
    
    // Create entity with mesh
    entity = g3d_entity_spawn(scene, 0, 0, 0, 5);
    g3d_entity_set_mesh(entity, mesh);
    write("✓ Mesh assigned to entity");
    
    g3d_shutdown();
END
```

**Expected:** All creation messages successful

---

### Test 5: Full Render Loop

Use `TEST_EXAMPLE.PRG` (provided in module directory)

**Run:**
```bash
bgdc TEST_EXAMPLE.PRG
bgdi TEST_EXAMPLE.DCB
```

**Check:**
- ✓ Scene created
- ✓ Entity spawned
- ✓ Material created
- ✓ Mesh created
- ✓ Light created
- ✓ Render loop executes

---

## Integration with Larger Project

### Add to Your Game Project

```bennugd
IMPORT "libmod_3d"

GLOBAL g3d_scene, g3d_entities[100], g3d_entity_count = 0;

FUNCTION init_3d()
    g3d_init(320, 240);  // Adjust to your resolution
    g3d_scene = g3d_scene_create("GameWorld");
    g3d_scene_set_active(g3d_scene);
    g3d_set_clear_color(0.1, 0.1, 0.2, 1.0);
    g3d_set_ambient_light(0.4, 0.4, 0.5, 1.0);
END

FUNCTION spawn_3d_object(x, y, z)
    g3d_entity_count++;
    g3d_entities[g3d_entity_count] = 
        g3d_entity_spawn(g3d_scene, 0, x, y, z);
    RETURN g3d_entities[g3d_entity_count];
END

FUNCTION render_3d()
    g3d_render();
END

PROCESS main()
    init_3d();
    
    // Spawn objects
    LOCAL e1 = spawn_3d_object(0, 0, 5);
    LOCAL e2 = spawn_3d_object(3, 0, 8);
    
    LOOP
        render_3d();
        
        IF (key(_ESC))
            BREAK;
        END
        
        FRAME;
    END
    
    g3d_shutdown();
END
```

---

## Debugging

### Enable Debug Output

```bennugd
PROCESS main()
    // All g3d_* functions print debug info to console
    g3d_init(1920, 1080);
    
    // Watch console for:
    // - "G3D: Initializing 3D engine"
    // - "G3D: Scene created"
    // - "G3D: Entity spawned"
    // - "G3D: Draw calls: X, Triangles: Y"
END
```

### Check Return Values

All functions return:
- **Positive number** = Success (entity/scene/material ID)
- **0 or 1** = Success (boolean operation)
- **-1** = Failure (check console for error)

```bennugd
IF (result < 0)
    write("ERROR: Operation failed");
END
```

---

## Performance Notes

### Expected Frame Rate
- **Cube mesh:** 60 FPS
- **10 entities:** 60 FPS
- **100 entities:** 30-60 FPS (depends on culling)

### Debug Output Impact
Debug messages slow down rendering. If you need max FPS:
1. Redirect debug output
2. Disable console output
3. Run in release mode

---

## Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Module not found | Library not compiled | Run `./build.sh` from project root |
| Function not found | BGD function signature mismatch | Check FUNC macro in libmod_3d_exports.h |
| Seg fault on g3d_render() | No active scene/entity | Call g3d_scene_set_active() first |
| Return value is -1 | Operation failed | Check console for error message |
| No visual output | Mesh data missing | Create primitive with g3d_primitive_* |

---

## What to Expect

### If Everything Works ✅
```
Console Output:
✓ 3D Engine initialized
✓ Scene created: 0
✓ Entity spawned at (0, 0, 5): 0
✓ Cube mesh created
✓ Material created: 0
G3D: Draw calls: 1, Triangles: 12, Culled: 0
(repeats each frame)
```

### Visual Rendering
Once fully integrated:
- Red cube rotating in center of screen
- Lighting from directional light
- Clear dark blue background

---

## Next Steps

1. **✓ Test module loads** → TEST_EXAMPLE.PRG
2. **Run in bgdi** → bgdi TEST_EXAMPLE.DCB
3. **Check console output** → Verify "Draw calls" increasing
4. **Integrate into your game** → Use helper functions above
5. **Add more entities/meshes** → See frame rate
6. **Load textures** → Use g3d_load_texture()

---

**Ready to test!** 🎮

Run `bgdc TEST_EXAMPLE.PRG && bgdi TEST_EXAMPLE.DCB` and watch the console.
