# Plan: libmod_3d Scene Editor

**For:** Creating a comprehensive 3D scene editor for libmod_3d  
**Status:** Design phase (ready for implementation)  
**Priority:** Phase 2 (after core rendering)

---

## Overview

A Qt-based scene editor allowing designers to:
- **Create/edit scenes** without coding
- **Place entities** (models) in 3D space
- **Configure lighting** (directional, point, spot)
- **Set up cameras** (viewpoints)
- **Manage materials** (colors, textures, properties)
- **Edit physics** (gravity, collisions, mass)
- **Export** to native format or BennuGD scripts

Similar to: Godot Editor, Unity Editor, Unreal Engine Editor

---

## Architecture

```
tools/g3d_editor/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── mainwindow.h/cpp
│   ├── viewport.h/cpp              # 3D viewport rendering
│   ├── scenetree.h/cpp             # Scene hierarchy widget
│   ├── properties.h/cpp            # Property inspector
│   ├── lighteditor.h/cpp           # Light configuration
│   ├── materialseditor.h/cpp       # Material properties
│   ├── physics.h/cpp               # Physics setup
│   ├── assetbrowser.h/cpp          # Model/texture browser
│   └── serializer.h/cpp            # Scene save/load
├── resources/
│   └── icons/
└── scenes/
    └── example.g3d                 # Example scene file
```

---

## Core Components

### 1. Main Window (Qt)

**Layout:**
```
┌─────────────────────────────────────────────┐
│ Menu | File Edit View Tools Window Help     │
├───────────────────────┬─────────────────────┤
│                       │                     │
│  3D Viewport          │  Properties Panel   │
│  (OpenGL Context)     │  ┌─────────────────┤
│                       │  │ Transform       │
│                       │  │ Position: ...   │
│                       │  │ Rotation: ...   │
│                       │  │ Scale: ...      │
│                       │  ├─────────────────┤
│                       │  │ Lighting        │
│                       │  │ Color: ...      │
│                       │  │ Intensity: ...  │
│                       │  │ Shadow Map: ... │
├───────────┬───────────┼─────────────────────┤
│ Scene     │ Assets    │                     │
│ Hierarchy │ Browser   │ Inspector           │
│           │           │ (Details)           │
└───────────┴───────────┴─────────────────────┘
```

**Features:**
- Drag-and-drop scene hierarchy
- Multi-select entities
- Undo/redo system
- Keyboard shortcuts (gizmos for transform)
- Real-time preview

### 2. 3D Viewport (OpenGL)

**Rendering:**
- Draw scene using libmod_3d API (same as game)
- Grid floor (debug visualization)
- Entity wireframes/outlines when selected
- Light visualizations (cone for spot, sphere for point)
- Camera frustum visualization
- Gizmos (move, rotate, scale)

**Interaction:**
- **Left-drag:** Rotate camera around pivot
- **Right-drag:** Pan camera
- **Scroll:** Zoom
- **Click:** Select entity
- **Move gizmo:** Translate/rotate/scale entity
- **Delete key:** Remove selected entity
- **Ctrl+D:** Duplicate entity

### 3. Scene Hierarchy (QTreeWidget)

**Display:**
```
📁 Scene "Level1"
  ├ 🎥 Camera_Main
  ├ ☀️ Light_Sun
  ├ 💡 Light_Torch (dynamic)
  ├ 🪶 Cube_01
  ├ 🪶 Sphere_02
  └ 📁 Group_Building
    ├ 🪶 Wall_Front
    ├ 🪶 Wall_Back
    └ 🚪 Door_01
```

**Features:**
- Reparent by drag-and-drop
- Toggle visibility (eye icon)
- Lock/unlock for editing
- Rename in-place
- Color-coded icons (mesh, camera, light, etc)
- Search/filter

### 4. Properties Inspector (QTableWidget)

**Tabs:**

**Transform Tab:**
```
Position X: [0.00] m
Position Y: [0.00] m
Position Z: [0.00] m
Rotation X: [0.00]° (Pitch)
Rotation Y: [0.00]° (Yaw)
Rotation Z: [0.00]° (Roll)
Scale X: [1.00]
Scale Y: [1.00]
Scale Z: [1.00]
[Lock Aspect] [Reset]
```

**Entity Tab:**
```
Name: [Entity_001]
Model: [sphere.glb] [Browse...]
Material: [Default] [Edit...]
Visible: [✓]
Cast Shadow: [✓]
```

**Light Tab** (if selected is light):
```
Light Type: [Directional ▼]
Color R: [1.00]
Color G: [1.00]
Color B: [0.95]
Intensity: [1.20]
Range: [100.0] m (for point/spot)
Cast Shadow: [✓]
Shadow Quality: [2048 ▼]  (1024, 2048, 4096)
```

**Physics Tab** (if physics body exists):
```
Enabled: [✓]
Shape: [Sphere ▼]
Mass: [1.0] kg
Friction: [0.5]
Restitution: [0.3]
Gravity Scale: [1.0]
Is Static: [ ]
Is Kinematic: [ ]
Is Trigger: [ ]
```

**Material Tab:**
```
Material Name: [Default]
Albedo Color: [████] (color picker)
Metallic: [0.0] ——————|——— [1.0]
Roughness: [0.5] ——|———————— [1.0]
Albedo Texture: [none] [Browse...]
Normal Map: [none] [Browse...]
Blend Mode: [Opaque ▼]
[Apply] [Reset]
```

### 5. Asset Browser (QListWidget / QIconView)

**Display:**
```
📁 assets/
  📁 models/
    • cube.glb          [thumbnail]
    • sphere.glb        [thumbnail]
    • character.glb     [thumbnail]
  📁 textures/
    • wood.png          [thumbnail]
    • metal.png         [thumbnail]
    • stone.png         [thumbnail]
```

**Features:**
- Tree view or icon view
- Double-click to add to scene
- Drag-and-drop to scene
- Rename/delete assets
- Tag system (e.g., #interactive, #static)
- Search bar

### 6. Serialization Format (.g3d)

**Text-based YAML for human readability:**

```yaml
# libmod_3d Scene File v1
version: "1.0"
scene_name: "Level1"

settings:
  ambient_light: [0.4, 0.4, 0.4]
  ambient_intensity: 1.0
  fog_enabled: true
  fog_color: [0.7, 0.7, 0.7]
  fog_start: 100.0
  fog_end: 1000.0
  clear_color: [0.2, 0.2, 0.2]

cameras:
  - id: 0
    name: "Main"
    position: [0, 10, 20]
    rotation: [0, 0, 0]  # Pitch, Yaw, Roll
    fov: 75
    active: true

lights:
  - id: 0
    name: "Sun"
    type: 0  # Directional
    color: [1.0, 1.0, 0.95]
    intensity: 1.2
    position: [100, 200, 100]
    direction: [-0.5, -1, -0.5]
    cast_shadow: true
    shadow_quality: 2048
  - id: 1
    name: "Torch"
    type: 1  # Point
    color: [1.0, 0.7, 0.3]
    intensity: 0.8
    position: [5, 2, 0]
    range: 50
    cast_shadow: false

entities:
  - id: 0
    name: "Cube_01"
    model: "models/cube.glb"
    position: [0, 0, 0]
    rotation: [0, 0, 0]
    scale: [1, 1, 1]
    material: "default"
    visible: true
    cast_shadow: true
    physics:
      enabled: true
      shape: 1  # BOX
      mass: 1.0
      friction: 0.5
      restitution: 0.3
  - id: 1
    name: "Sphere_02"
    model: "models/sphere.glb"
    position: [5, 0, 0]
    rotation: [0, 0, 0]
    scale: [1, 1, 1]
    material: "metal"
    visible: true
    cast_shadow: true

materials:
  - id: 0
    name: "default"
    albedo: [1, 1, 1]
    metallic: 0.0
    roughness: 0.5
    albedo_texture: null
    normal_map: null
  - id: 1
    name: "metal"
    albedo: [0.8, 0.8, 0.8]
    metallic: 1.0
    roughness: 0.2
    albedo_texture: "textures/metal.png"
    normal_map: "textures/metal_normal.png"
```

### 7. BennuGD Script Export

Editor can export scene to `.prg`:

```prg
import "libmod_3d";

program Level1;
local
    int scene, camera, sun, entity_cube, entity_sphere;
end
begin
    G3D_INIT(1920, 1080);
    
    scene = G3D_SCENE_CREATE("Level1");
    G3D_SCENE_SET_ACTIVE(scene);
    
    G3D_SET_AMBIENT_LIGHT(0.4, 0.4, 0.4, 1.0);
    G3D_SET_FOG(1, 0.7, 0.7, 0.7, 100, 1000);
    
    // Camera
    camera = G3D_CAMERA_CREATE();
    G3D_CAMERA_SET_ACTIVE(camera);
    G3D_CAMERA_SET_POSITION(camera, 0, 10, 20);
    G3D_CAMERA_LOOK_AT(camera, 0, 0, 0, 0, 1, 0);
    G3D_CAMERA_SET_FOV(camera, 75);
    
    // Lighting
    sun = G3D_LIGHT_CREATE(G3D_LIGHT_DIRECTIONAL, 1.0, 1.0, 0.95);
    G3D_LIGHT_SET_POSITION(sun, 100, 200, 100);
    G3D_LIGHT_SET_INTENSITY(sun, 1.2);
    G3D_LIGHT_ENABLE_SHADOW(sun, 1);
    G3D_LIGHT_SET_SHADOW_QUALITY(sun, 2048);
    
    // Entities
    entity_cube = G3D_ENTITY_SPAWN(scene, 
                                   G3D_LOAD_GLTF("models/cube.glb"),
                                   0, 0, 0);
    entity_sphere = G3D_ENTITY_SPAWN(scene,
                                     G3D_LOAD_GLTF("models/sphere.glb"),
                                     5, 0, 0);
    
    // Render loop
    loop
        G3D_RENDER();
        frame;
    end
    
    G3D_SHUTDOWN();
end
```

---

## Implementation Phases

### Phase 1: Viewport + Hierarchy (Week 1)
- [x] Qt main window with OpenGL context
- [x] Scene tree widget
- [x] Load/save basic scene (YAML)
- [x] Entity creation/deletion
- [x] Scene import from .g3d

**Deliverable:** Basic editor with entities visible, no interaction yet

### Phase 2: Interaction + Properties (Week 2)
- [ ] 3D gizmos (move, rotate, scale)
- [ ] Properties inspector
- [ ] Light editor
- [ ] Material editor
- [ ] Real-time preview with libmod_3d

**Deliverable:** Full scene editing, camera/light adjustment

### Phase 3: Asset Management (Week 3)
- [ ] Model browser (glTF, MD3)
- [ ] Texture browser (PNG, JPG)
- [ ] Material library
- [ ] Drag-and-drop to scene
- [ ] Asset tagging/categorization

**Deliverable:** Professional asset workflow

### Phase 4: Polish + Export (Week 4)
- [ ] Undo/redo system
- [ ] Export to BennuGD script
- [ ] Export to binary .g3d (compressed)
- [ ] Editor settings/preferences
- [ ] Keyboard shortcuts
- [ ] Help/documentation

**Deliverable:** Production-ready editor

---

## Technology Stack

- **Qt 5.15+** — UI framework (cross-platform)
- **OpenGL 3.3** — Viewport rendering
- **YAML-CPP** — Scene serialization
- **libmod_3d C API** — Scene management
- **CMake** — Build system

---

## Dependencies for Editor

```
tools/g3d_editor/
  └─ CMakeLists.txt
      ├─ Qt5 (Core, Gui, Widgets, OpenGL)
      ├─ OpenGL
      ├─ yaml-cpp
      └─ Link against libmod_3d (from modules/)
```

---

## File Format Comparison

| Format | Pros | Cons |
|--------|------|------|
| **.g3d (YAML)** | Human-readable, versionable, merge-friendly | Larger file size |
| **.g3db (Binary)** | Compact, fast load | Not human-readable |
| **.prg (BennuGD)** | Native, scriptable | Hard to edit visually, verbose |

**Recommendation:** Use YAML during development, binary for release.

---

## Integration with libmod_3d

Editor uses same C API as scripts:

```c
// Same functions used by both editor and game
G3D_INIT(width, height);
G3D_SCENE_CREATE("Level1");
G3D_ENTITY_SPAWN(scene, model, x, y, z);
G3D_LIGHT_CREATE(type, r, g, b);
G3D_RENDER();  // Real-time preview
```

This ensures **100% fidelity** between editor preview and game execution.

---

## Next Steps

1. **Await libmod_3d Rendering** — Editor needs working render pipeline
2. **Create Qt Skeleton** — Main window + basic UI
3. **Implement Viewport** — OpenGL rendering of scenes
4. **Add Serialization** — Save/load .g3d files
5. **Polish & Iterate** — User feedback, refinement

---

## Deliverables Checklist

- [ ] Executable `g3d_editor` (Windows/Linux/macOS)
- [ ] Example scene `scenes/example.g3d`
- [ ] Editor documentation (manual)
- [ ] Test suite (save/load fidelity)
- [ ] BennuGD integration guide (export script generation)

---

## Success Criteria

✅ Designer can create scene without writing code  
✅ Real-time 3D preview  
✅ Save scene, reload perfectly  
✅ Export to playable BennuGD script  
✅ Works with all libmod_3d features (lights, physics, materials)

---

**Ready for implementation by specialized agent!**
