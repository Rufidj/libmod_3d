# Mesh and Texture API Guide

Complete API for creating and texturizing 3D objects in libmod_3d.

---

## Mesh Creation

### Primitive Meshes (Built-in)

```bennugd
IMPORT "libmod_3d"

GLOBAL
    cube_mesh;
    sphere_mesh;
    plane_mesh;
END

PROCESS main()
    // Create primitive meshes
    cube_mesh = g3d_primitive_cube();        // Unit cube (1x1x1)
    sphere_mesh = g3d_primitive_sphere(16);  // Sphere with 16 segments
    plane_mesh = g3d_primitive_plane();      // Unit plane (1x1)
    
    IF (cube_mesh < 0)
        write("ERROR: Failed to create cube");
    END
END
```

**Returns:** Mesh pointer (cast to int), or -1 on error

**Available Primitives:**
- `g3d_primitive_cube()` - Unit cube, 6 faces, proper normals and UVs
- `g3d_primitive_sphere(segments)` - UV sphere, configurable quality (3+ segments)
- `g3d_primitive_plane()` - XZ plane with normals pointing up

---

## Texture Loading

### Load Textures from Files

```bennugd
GLOBAL
    albedo_tex;
    normal_tex;
END

PROCESS main()
    // Load textures
    albedo_tex = g3d_load_texture("assets/stone_albedo.png");
    normal_tex = g3d_load_texture("assets/stone_normal.png");
    
    IF (albedo_tex < 0)
        write("ERROR: Failed to load albedo texture");
    END
    
    IF (normal_tex < 0)
        write("ERROR: Failed to load normal texture");
    END
END
```

**Supported Formats:** PNG, JPG (via SDL2_image)  
**Returns:** Texture pointer (cast to int), or -1 on error

---

## Material Texturing

### Assign Textures to Materials

```bennugd
GLOBAL
    material;
    albedo_tex;
    normal_tex;
END

PROCESS main()
    // Create material
    material = g3d_material_create();
    
    // Set base color
    g3d_material_set_color(material, 1.0, 1.0, 1.0, 1.0);
    
    // Set PBR properties
    g3d_material_set_metallic(material, 0.5);
    g3d_material_set_roughness(material, 0.7);
    
    // Load textures
    albedo_tex = g3d_load_texture("assets/rock_albedo.png");
    normal_tex = g3d_load_texture("assets/rock_normal.png");
    
    // Assign textures to material
    IF (albedo_tex >= 0)
        g3d_material_set_texture(material, 0, albedo_tex);  // Albedo
    END
    
    IF (normal_tex >= 0)
        g3d_material_set_texture(material, 1, normal_tex);  // Normal map
    END
END
```

**Texture Types (texture_type parameter):**
- `0` = Albedo (diffuse color)
- `1` = Normal map (surface detail)
- `2` = Metallic/Roughness map

---

## Complete Example: Textured Cube

```bennugd
IMPORT "libmod_3d"

GLOBAL
    scene;
    entity;
    material;
    cube_mesh;
    albedo_tex;
    light;
END

PROCESS main()
    // Initialize engine
    g3d_init(1920, 1080);
    
    // Create scene
    scene = g3d_scene_create("TexturedScene");
    g3d_scene_set_active(scene);
    
    // Create primitive cube mesh
    cube_mesh = g3d_primitive_cube();
    
    // Load texture
    albedo_tex = g3d_load_texture("assets/stone.png");
    
    // Create material
    material = g3d_material_create();
    g3d_material_set_color(material, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_metallic(material, 0.2);
    g3d_material_set_roughness(material, 0.8);
    
    // Assign texture to material
    IF (albedo_tex >= 0)
        g3d_material_set_texture(material, 0, albedo_tex);
    END
    
    // Create entity
    entity = g3d_entity_spawn(scene, 0, 0, 0, 0);
    
    // Assign mesh to entity
    g3d_entity_set_mesh(entity, cube_mesh);
    
    // Assign material to entity (internal storage)
    entity->material_id = material;  // Direct field access
    
    // Create light
    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_light_set_direction(light, -1, -1, -1);
    g3d_light_set_intensity(light, 1.5);
    
    // Set rendering
    g3d_set_clear_color(0.1, 0.1, 0.1, 1.0);
    g3d_set_ambient_light(0.3, 0.3, 0.3, 1.0);
    
    // Main loop
    LOOP
        LOCAL angle = time / 100;
        
        // Rotate cube
        g3d_entity_set_rotation(entity, angle, angle * 2, 0);
        
        // Render
        g3d_render();
        
        IF (key(_ESC))
            BREAK;
        END
        
        FRAME;
    END
    
    // Cleanup
    g3d_shutdown();
END
```

---

## Mesh Assignment to Entities

### Assign Mesh to Entity

```bennugd
GLOBAL
    entity;
    cube_mesh;
END

PROCESS main()
    // Create entity
    entity = g3d_entity_spawn(scene, 0, 0, 0, 0);
    
    // Create mesh
    cube_mesh = g3d_primitive_cube();
    
    // Assign mesh to entity
    g3d_entity_set_mesh(entity, cube_mesh);
    
    // Now entity will render with this mesh
    // Material must be assigned separately
END
```

**Note:** Entity must have both mesh AND material for rendering

---

## Advanced: Multiple Meshes

```bennugd
GLOBAL
    scene;
    entities[10];
    meshes[3];
    materials[3];
    albedo_textures[3];
END

PROCESS main()
    g3d_init(1920, 1080);
    
    scene = g3d_scene_create("MultiMesh");
    g3d_scene_set_active(scene);
    
    // Create variety of meshes
    meshes[0] = g3d_primitive_cube();
    meshes[1] = g3d_primitive_sphere(24);
    meshes[2] = g3d_primitive_plane();
    
    // Load different textures
    albedo_textures[0] = g3d_load_texture("assets/stone.png");
    albedo_textures[1] = g3d_load_texture("assets/metal.png");
    albedo_textures[2] = g3d_load_texture("assets/wood.png");
    
    // Create materials with textures
    LOCAL i;
    FOR (i = 0; i < 3; i++)
        materials[i] = g3d_material_create();
        g3d_material_set_color(materials[i], 1.0, 1.0, 1.0, 1.0);
        
        IF (albedo_textures[i] >= 0)
            g3d_material_set_texture(materials[i], 0, albedo_textures[i]);
        END
    END
    
    // Create entities with different meshes and materials
    FOR (i = 0; i < 10; i++)
        entities[i] = g3d_entity_spawn(scene, 0, 0, 0, 0);
        
        LOCAL mesh_idx = i % 3;
        LOCAL mat_idx = i % 3;
        
        g3d_entity_set_mesh(entities[i], meshes[mesh_idx]);
        entities[i].material_id = materials[mat_idx];
        
        LOCAL x = (i % 4) * 3 - 4.5;
        LOCAL z = (i / 4) * 3;
        g3d_entity_set_position(entities[i], x, 0, z);
    END
    
    LOOP
        g3d_render();
        
        IF (key(_ESC))
            BREAK;
        END
        
        FRAME;
    END
    
    g3d_shutdown();
END
```

---

## Error Handling

### Check for Load Failures

```bennugd
FUNCTION INT load_material_safely(STRING texture_path)
    LOCAL tex = g3d_load_texture(texture_path);
    
    IF (tex < 0)
        write("ERROR: Failed to load texture: " + texture_path);
        return -1;
    END
    
    LOCAL mat = g3d_material_create();
    g3d_material_set_texture(mat, 0, tex);
    
    write("Material created with texture: " + texture_path);
    return mat;
END

PROCESS main()
    LOCAL material = load_material_safely("assets/rock.png");
    
    IF (material >= 0)
        write("Material ready: " + STRING material);
    ELSE
        write("Failed to load material");
    END
END
```

---

## Performance Tips

### 1. Load Textures Once
```bennugd
// ✓ DO: Load once at startup
GLOBAL textures[10];

PROCESS startup()
    LOCAL i;
    FOR (i = 0; i < 10; i++)
        textures[i] = g3d_load_texture("assets/tex_" + STRING i + ".png");
    END
END

PROCESS render_loop()
    // Use pre-loaded textures
END
```

### 2. Reuse Meshes
```bennugd
// ✓ DO: Create one mesh, use for multiple entities
LOCAL cube = g3d_primitive_cube();

FOR (i = 0; i < 1000; i++)
    entity = g3d_entity_spawn(scene, 0, x, y, z);
    g3d_entity_set_mesh(entity, cube);  // Reuse same mesh
END
```

### 3. Batch Material Creation
```bennugd
// ✓ DO: Create materials with common properties once
LOCAL base_material = g3d_material_create();
g3d_material_set_metallic(base_material, 0.5);

// Then customize per instance
FOR (i = 0; i < 100; i++)
    LOCAL mat = g3d_material_create();
    g3d_material_set_roughness(mat, 0.5 + RAND(0, 0.5));
END
```

---

## Limitations & Future

### Current Limitations
- ❌ No glTF loading (returns NULL)
- ❌ No MD3 support
- ❌ No animated meshes
- ❌ No mesh deformation
- ⚠️ Texture coordinates fixed to UV mapping

### Coming Soon
- ✅ Full glTF 2.0 loader
- ✅ Model instancing
- ✅ LOD system
- ✅ Skeletal animation
- ✅ Custom vertex attributes

---

## API Reference

### Mesh Functions
```c
G3DMesh *g3d_primitive_cube(void);
G3DMesh *g3d_primitive_sphere(int segments);
G3DMesh *g3d_primitive_plane(void);

int g3d_entity_set_mesh(int entity_id, G3DMesh *mesh);
int g3d_mesh_upload_gpu(G3DMesh *mesh);
```

### Texture Functions
```c
G3DTexture *g3d_load_texture(const char *filepath);
int g3d_texture_upload_gpu(G3DTexture *texture);
void g3d_texture_bind(G3DTexture *texture, int texture_unit);
```

### Material Functions
```c
int g3d_material_create(void);
int g3d_material_set_color(int material_id, float r, float g, float b, float a);
int g3d_material_set_metallic(int material_id, float value);
int g3d_material_set_roughness(int material_id, float value);
int g3d_material_set_texture(int material_id, int texture_type, G3DTexture *texture);
```

---

**Ready to render textured 3D objects!** 🎨
