// Tech demo: day/night cycle. The sun arcs across the sky, the sky colour and
// ambient change, and every object's shadow sweeps with the moving light.
// Compile: bgdc TEST_DAYNIGHT.PRG     Run: bgdi TEST_DAYNIGHT.dcb

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
    int pillar_mat;
    int pe;
    int i;
    float ox;
    float oz;
    int gid = 0;
    float tod = 0.05;     // time of day 0..1
    float tspeed = 0.00104;  // ~16s full cycle
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    IF (!g3d_init(960, 540))
        say("ERROR: 3D init failed");
        RETURN;
    END

    scene = g3d_scene_create("DayNight");
    g3d_scene_set_active(scene);

    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 22.0, -32.0);
    g3d_camera_look_at(camera, 0.0, 0.0, 0.0,  0.0, 1.0, 0.0);
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

    // Pilares para que las sombras sean visibles y se muevan
    box_mesh = g3d_primitive_cube();
    pillar_mat = g3d_material_create();
    g3d_material_set_color(pillar_mat, 0.7, 0.7, 0.72, 1.0);
    FOR (i = 0; i < 6; i = i + 1)
        ox = -20.0 + i * 8.0;
        oz = 4.0;
        pe = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(pe, box_mesh);
        g3d_entity_set_material(pe, pillar_mat);
        g3d_entity_set_scale(pe, 2.5, 8.0, 2.5);
        g3d_entity_set_position(pe, ox, g3d_terrain_get_height(terrain_mesh, ox, oz) + 4.0, oz);
    END

    // Luz direccional (el sol). g3d_set_sun la controla.
    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_set_shadows(1);

    x = 480;
    y = 270;

    LOOP
        // Avanzar el tiempo y aplicar el sol
        tod = tod + tspeed;
        IF (tod >= 1.0) tod = tod - 1.0; END
        g3d_set_sun(tod);

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_shutdown();
    say("=== Done ===");
END
