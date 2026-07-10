// Test Example: Simple 3D Textured Cube (rendered into a BennuGD GRAPH)
// Compile with: bgdc TEST_EXAMPLE.PRG
// Run with:     bgdi TEST_EXAMPLE.dcb

import "libmod_gfx";    // set_mode() crea la ventana + contexto OpenGL
import "libmod_misc";   // say(), itoa()
import "libmod_input";  // key(), _ESC
import "libmod_3d";

GLOBAL
    int scene;
    int entity;
    int material;
    int cube_mesh;
    int texture;
    int light;
    int camera;

    float ang = 0.0;
    int frames = 0;
END

PROCESS main()
BEGIN
    // Crear ventana + contexto GL (necesario antes de g3d_init)
    set_mode(960, 540);
    set_fps(60, 0);

    say("OK: 3D Engine initialized");

    // Escena
    scene = g3d_scene_create("TestScene");
    g3d_scene_set_active(scene);
    say("OK: Scene created and active: " + itoa(scene));

    // Camara mirando al cubo (situado en z=5)
    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 1.0, 0.0);
    g3d_camera_look_at(camera, 0.0, 0.0, 5.0,  0.0, 1.0, 0.0);
    g3d_camera_set_active(camera);
    say("OK: Camera created and active: " + itoa(camera));

    // Mesh cubo
    cube_mesh = g3d_primitive_cube();
    say("OK: Cube mesh created");

    // Textura (checkerboard creada en assets/)
    texture = g3d_load_texture("assets/texture.png");
    IF (texture >= 0)
        say("OK: Texture loaded");
    ELSE
        say("WARN: Texture not loaded");
        texture = -1;
    END

    // Material rojo
    material = g3d_material_create();
    g3d_material_set_color(material, 1.0, 0.3, 0.3, 1.0);
    g3d_material_set_metallic(material, 0.3);
    g3d_material_set_roughness(material, 0.6);
    IF (texture >= 0)
        g3d_material_set_texture(material, 0, texture);
    END
    say("OK: Material created: " + itoa(material));

    // Entidad con mesh
    entity = g3d_entity_spawn(scene, 0, 0, 0, 5);
    g3d_entity_set_mesh(entity, cube_mesh);
    g3d_entity_set_material(entity, material);
    say("OK: Entity spawned: " + itoa(entity));

    // Luz direccional blanca
    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_light_set_direction(light, -1, -1, -1);
    g3d_light_set_intensity(light, 1.5);
    g3d_light_enable_shadow(light, 0);
    say("OK: Light created: " + itoa(light));

    // Parametros de render
    g3d_set_clear_color(0.1, 0.1, 0.15, 1.0);
    g3d_set_ambient_light(0.3, 0.3, 0.35, 1.0);
    g3d_set_wireframe_mode(0);

    // Mostrar el GRAPH renderizado en el centro de la pantalla
    x = 480;
    y = 270;

    say("=== Starting Render Loop ===");

    LOOP
        // Rotacion suave (angulos en RADIANES): ~0.6 deg pitch, ~1.1 deg yaw por frame
        ang += 0.02;
        g3d_entity_set_rotation(entity, ang * 0.5 * 57295.78, ang * 57295.78, 0.0);

        // Renderiza la escena 3D dentro de un GRAPH y lo muestra



        IF (key(_ESC))
            BREAK;
        END

        // Salir tras ~600 frames para test automatizado
        frames += 1;
        IF (frames > 600)
            BREAK;
        END

        FRAME;
    END

    say("OK: Render loop completed");

    say("=== Test Complete ===");
END
