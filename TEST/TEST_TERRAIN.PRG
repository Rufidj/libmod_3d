// Test: Procedural noise terrain with a tiled texture
// Compile: bgdc TEST_TERRAIN.PRG     Run: bgdi TEST_TERRAIN.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int ent;
    int material;
    int terrain_mesh;
    int texture;
    int light;
    int camera;

    float ang = 0.0;
    int frames = 0;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    say("OK: 3D Engine initialized");

    scene = g3d_scene_create("Terrain");
    g3d_scene_set_active(scene);

    // Camara elevada mirando al centro del terreno
    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 18.0, -28.0);
    g3d_camera_look_at(camera, 0.0, 0.0, 0.0,  0.0, 1.0, 0.0);
    g3d_camera_set_active(camera);

    // Terreno procedural: grid=120, tamano=50, altura=7, tiling=12, semilla=1234
    terrain_mesh = g3d_primitive_terrain(120, 50.0, 7.0, 12.0, 1234);
    IF (terrain_mesh < 0)
        say("ERROR: terrain creation failed");
        RETURN;
    END
    say("OK: Terrain mesh created");

    // Textura de hierba (se repite por el tiling de los texcoords)
    texture = g3d_load_texture("assets/grass.png");
    IF (texture < 0)
        say("WARN: grass texture not loaded");
        texture = -1;
    END

    material = g3d_material_create();
    g3d_material_set_color(material, 1.0, 1.0, 1.0, 1.0);  // tinte neutro
    g3d_material_set_metallic(material, 0.0);
    g3d_material_set_roughness(material, 0.9);
    IF (texture >= 0)
        g3d_material_set_texture(material, 0, texture);
    END

    ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(ent, terrain_mesh);
    g3d_entity_set_material(ent, material);
    say("OK: Terrain ent ready");

    // Luz direccional (sol)
    light = g3d_light_create(0, 1.0, 0.95, 0.8);
    g3d_light_set_direction(light, -0.5, -1.0, -0.3);
    g3d_light_set_intensity(light, 1.4);
    g3d_light_enable_shadow(light, 0);

    g3d_set_clear_color(0.4, 0.6, 0.9, 1.0);   // cielo azul
    g3d_set_ambient_light(0.45, 0.45, 0.5, 1.0);
    g3d_set_wireframe_mode(0);

    x = 480;
    y = 270;

    say("=== Render loop (terrain rotating) ===");

    LOOP
        // Rotar el terreno lentamente para apreciarlo (radianes)
        ang += 0.008;
        g3d_entity_set_rotation(ent, 0.0, ang * 57295.78, 0.0);




        IF (key(_ESC)) BREAK; END

        frames += 1;
        IF (frames > 600) BREAK; END

        FRAME;
    END

    say("OK: done");

    say("=== Test Complete ===");
END
