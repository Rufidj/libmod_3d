// Tech demo: drive a cube around the terrain with WASD, follow camera,
// scattered pillars, everything casting real shadows.
// Compile: bgdc TEST_PLAYGROUND.PRG     Run: bgdi TEST_PLAYGROUND.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int light;
    int terrain_mesh;
    int terrain_mat;
    int grass_tex;
    int box_mesh;
    int player_mat;
    int pillar_mat;
    int player;

    float px = 0.0;
    float pz = 0.0;
    float py = 0.0;
    float speed = 0.25;
    float spin = 0.0;
    int i;
    int pe;
    float ox;
    float oz;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    scene = g3d_scene_create("Playground");
    g3d_scene_set_active(scene);

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Terreno
    terrain_mesh = g3d_primitive_terrain(120, 60.0, 3.0, 14.0, 99);
    grass_tex = g3d_load_texture("assets/grass.png");
    terrain_mat = g3d_material_create();
    g3d_material_set_color(terrain_mat, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_roughness(terrain_mat, 0.9);
    IF (grass_tex >= 0)
        g3d_material_set_texture(terrain_mat, 0, grass_tex);
    END
    pe = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(pe, terrain_mesh);
    g3d_entity_set_material(pe, terrain_mat);

    // Mesh de caja compartida
    box_mesh = g3d_primitive_cube();

    // Pilares dispersos (dos filas) que proyectan sombra
    pillar_mat = g3d_material_create();
    g3d_material_set_color(pillar_mat, 0.6, 0.6, 0.65, 1.0);
    FOR (i = 0; i < 5; i = i + 1)
        ox = -18.0 + i * 9.0;
        oz = 10.0;
        pe = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(pe, box_mesh);
        g3d_entity_set_material(pe, pillar_mat);
        g3d_entity_set_scale(pe, 2.0, 7.0, 2.0);
        g3d_entity_set_position(pe, ox, g3d_terrain_get_height(terrain_mesh, ox, oz) + 3.5, oz);
    END
    FOR (i = 0; i < 5; i = i + 1)
        ox = -18.0 + i * 9.0;
        oz = -12.0;
        pe = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(pe, box_mesh);
        g3d_entity_set_material(pe, pillar_mat);
        g3d_entity_set_scale(pe, 2.0, 5.0, 2.0);
        g3d_entity_set_position(pe, ox, g3d_terrain_get_height(terrain_mesh, ox, oz) + 2.5, oz);
    END

    // Cubo jugador (rojo)
    player_mat = g3d_material_create();
    g3d_material_set_color(player_mat, 0.95, 0.25, 0.15, 1.0);
    player = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(player, box_mesh);
    g3d_entity_set_material(player, player_mat);
    g3d_entity_set_scale(player, 2.0, 2.0, 2.0);

    // Sol + sombras
    light = g3d_light_create(0, 1.0, 0.96, 0.85);
    g3d_light_set_direction(light, -0.5, -1.0, -0.4);
    g3d_light_set_intensity(light, 1.3);
    g3d_set_clear_color(0.45, 0.65, 0.95, 1.0);
    g3d_set_ambient_light(0.4, 0.42, 0.48, 1.0);
    g3d_set_shadows(1);

    say("WASD para mover el cubo. ESC para salir.");

    x = 480;
    y = 270;

    LOOP
        // Movimiento WASD en el plano XZ
        IF (key(_W)) pz = pz + speed; END
        IF (key(_S)) pz = pz - speed; END
        IF (key(_A)) px = px - speed; END
        IF (key(_D)) px = px + speed; END

        // Mantener dentro del terreno (+/-28)
        IF (px > 28.0) px = 28.0; END
        IF (px < -28.0) px = -28.0; END
        IF (pz > 28.0) pz = 28.0; END
        IF (pz < -28.0) pz = -28.0; END

        // Seguir el relieve del terreno
        py = g3d_terrain_get_height(terrain_mesh, px, pz) + 1.0;
        spin = spin + 0.04;
        g3d_entity_set_position(player, px, py, pz);
        g3d_entity_set_rotation(player, 0.0, spin * 57295.78, 0.0);

        // Camara en tercera persona siguiendo al cubo
        g3d_camera_set_position(camera, px, py + 14.0, pz - 20.0);
        g3d_camera_look_at(camera, px, py, pz,  0.0, 1.0, 0.0);




        IF (key(_ESC)) BREAK; END
        FRAME;
    END


    say("=== Done ===");
END
