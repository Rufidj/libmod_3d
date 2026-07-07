# libmod_3d Usage Example

Complete example of using the 3D engine from BennuGD script.

## Basic Setup

```bennugd
IMPORT "libmod_3d"

GLOBAL
    camera;
    scene;
    entity;
    material;
    light;
END

PROCESS main()
    LOCAL display_width = 1920;
    LOCAL display_height = 1080;
    
    // Initialize 3D engine
    g3d_init(display_width, display_height);
    
    // Create a scene
    scene = g3d_scene_create("Main Scene");
    g3d_scene_set_active(scene);
    
    // Create a material
    material = g3d_material_create();
    g3d_material_set_color(material, 0.8, 0.2, 0.2, 1.0);  // Red
    g3d_material_set_metallic(material, 0.0);
    g3d_material_set_roughness(material, 0.5);
    
    // Create a light
    light = g3d_light_create(G3D_LIGHT_DIRECTIONAL, 1.0, 1.0, 1.0);
    g3d_light_set_direction(light, -1, -1, -1);
    g3d_light_set_intensity(light, 1.2);
    g3d_light_enable_shadow(light, 1);
    g3d_light_set_shadow_quality(light, 2048);
    
    // Create an entity
    entity = g3d_entity_spawn(scene, -1, 0, 0, 0);  // model_id=-1 for now
    g3d_entity_set_material(entity, material);
    
    // Set rendering parameters
    g3d_set_clear_color(0.1, 0.1, 0.1, 1.0);
    g3d_set_ambient_light(0.3, 0.3, 0.3, 1.0);
    g3d_set_wireframe_mode(0);
    
    // Main loop
    LOOP
        // Update entity transform
        LOCAL angle = time / 100;
        g3d_entity_set_position(entity, 0, 0, 5);
        g3d_entity_set_rotation(entity, 0, angle, 0);
        
        // Render frame
        g3d_render();
        
        // Input handling
        IF (key(_ESC))
            BREAK;
        END
        
        FRAME;
    END
    
    // Cleanup
    g3d_entity_destroy(entity);
    g3d_scene_destroy(scene);
    g3d_shutdown();
END
```

## Complete Example with Multiple Entities

```bennugd
IMPORT "libmod_3d"

GLOBAL
    scene;
    cube1, cube2, sphere;
    red_material, blue_material;
    directional_light, point_light;
END

PROCESS main()
    // Initialize
    g3d_init(1920, 1080);
    
    // Create scene
    scene = g3d_scene_create("Game Scene");
    g3d_scene_set_active(scene);
    
    // Create materials
    red_material = g3d_material_create();
    g3d_material_set_color(red_material, 1.0, 0.0, 0.0, 1.0);
    
    blue_material = g3d_material_create();
    g3d_material_set_color(blue_material, 0.0, 0.0, 1.0, 1.0);
    g3d_material_set_metallic(blue_material, 0.8);
    g3d_material_set_roughness(blue_material, 0.2);
    
    // Create entities
    cube1 = g3d_entity_spawn(scene, -1, -3, 0, 0);
    g3d_entity_set_material(cube1, red_material);
    g3d_entity_set_scale(cube1, 1.0, 1.0, 1.0);
    
    cube2 = g3d_entity_spawn(scene, -1, 3, 0, 0);
    g3d_entity_set_material(cube2, blue_material);
    g3d_entity_set_scale(cube2, 1.0, 1.0, 1.0);
    
    sphere = g3d_entity_spawn(scene, -1, 0, 2, 0);
    g3d_entity_set_material(sphere, red_material);
    g3d_entity_set_scale(sphere, 1.0, 1.0, 1.0);
    
    // Create lights
    directional_light = g3d_light_create(G3D_LIGHT_DIRECTIONAL, 1.0, 1.0, 1.0);
    g3d_light_set_direction(directional_light, -1, -1, -1);
    g3d_light_set_intensity(directional_light, 1.5);
    
    point_light = g3d_light_create(G3D_LIGHT_POINT, 1.0, 0.5, 0.5);
    g3d_light_set_position(point_light, 5, 5, 5);
    g3d_light_set_intensity(point_light, 0.8);
    g3d_light_set_range(point_light, 20);
    
    // Configure rendering
    g3d_set_clear_color(0.05, 0.05, 0.1, 1.0);
    g3d_set_ambient_light(0.2, 0.2, 0.3, 1.0);
    
    LOOP
        LOCAL angle = time / 100;
        LOCAL y_pos = 2 + 2 * cos(angle);
        
        // Rotate entities
        g3d_entity_set_rotation(cube1, 0, angle * 2, 0);
        g3d_entity_set_rotation(cube2, angle, angle * 2, 0);
        g3d_entity_set_position(sphere, 0, y_pos, 0);
        
        // Render
        g3d_render();
        
        // Control light
        IF (key(_1))
            g3d_light_enable_shadow(directional_light, 1);
        END
        IF (key(_2))
            g3d_light_enable_shadow(directional_light, 0);
        END
        
        IF (key(_ESC))
            BREAK;
        END
        
        FRAME;
    END
    
    // Cleanup
    g3d_light_destroy(directional_light);
    g3d_light_destroy(point_light);
    g3d_entity_destroy(cube1);
    g3d_entity_destroy(cube2);
    g3d_entity_destroy(sphere);
    g3d_material_destroy(red_material);
    g3d_material_destroy(blue_material);
    g3d_scene_destroy(scene);
    g3d_shutdown();
END
```

## API Reference

### Initialization
- `g3d_init(width, height)` - Initialize 3D engine
- `g3d_shutdown()` - Shutdown 3D engine
- `g3d_render()` - Render current frame

### Scenes
- `g3d_scene_create(name)` - Create scene
- `g3d_scene_destroy(scene_id)` - Destroy scene
- `g3d_scene_set_active(scene_id)` - Activate scene

### Entities
- `g3d_entity_spawn(scene_id, model_id, x, y, z)` - Create entity
- `g3d_entity_destroy(entity_id)` - Destroy entity
- `g3d_entity_set_position(entity_id, x, y, z)` - Set position
- `g3d_entity_set_rotation(entity_id, pitch, yaw, roll)` - Set rotation (degrees)
- `g3d_entity_set_scale(entity_id, sx, sy, sz)` - Set scale
- `g3d_entity_get_position(entity_id, x, y, z)` - Get position
- `g3d_entity_set_parent(entity_id, parent_id)` - Set parent (hierarchical transform)

### Materials
- `g3d_material_create()` - Create material
- `g3d_material_destroy(material_id)` - Destroy material
- `g3d_material_set_color(material_id, r, g, b, a)` - Set albedo color
- `g3d_material_set_metallic(material_id, value)` - Set metallic (0-1)
- `g3d_material_set_roughness(material_id, value)` - Set roughness (0-1)

### Lights
- `g3d_light_create(type, r, g, b)` - Create light (G3D_LIGHT_DIRECTIONAL, G3D_LIGHT_POINT, G3D_LIGHT_SPOT)
- `g3d_light_destroy(light_id)` - Destroy light
- `g3d_light_set_position(light_id, x, y, z)` - Set position (point/spot)
- `g3d_light_set_direction(light_id, dx, dy, dz)` - Set direction (directional)
- `g3d_light_set_intensity(light_id, intensity)` - Set brightness
- `g3d_light_set_range(light_id, range)` - Set range (point/spot)
- `g3d_light_enable_shadow(light_id, enabled)` - Enable shadow mapping
- `g3d_light_set_shadow_quality(light_id, resolution)` - Shadow map resolution (512-4096)

### Rendering
- `g3d_set_clear_color(r, g, b, a)` - Background color
- `g3d_set_ambient_light(r, g, b, intensity)` - Global ambient
- `g3d_set_wireframe_mode(enabled)` - Wireframe rendering
- `g3d_set_fog(enabled, r, g, b, start, end)` - Fog effect

## Known Limitations

- Model IDs not yet supported (use -1 for now)
- Texture loading returns placeholder IDs
- Camera system uses BGD wrapper stubs
- Physics bodies not implemented
- No raycast/picking yet
- No batch rendering optimization
- VITA/Android support requires GL abstraction
