// Tech demo: a cube casting a real shadow onto the terrain
// Compile: bgdc TEST_SHADOW.PRG     Run: bgdi TEST_SHADOW.dcb

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
    int terrain_ent;
    int cube_mesh;
    int cube_mat;
    int cube_ent;
    int gid = 0;
    float ang = 0.0;
    int frames = 0;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    IF (!g3d_init(960, 540))
        say("ERROR: 3D init failed");
        RETURN;
    END

    scene = g3d_scene_create("Shadow");
    g3d_scene_set_active(scene);

    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 22.0, -30.0);
    g3d_camera_look_at(camera, 0.0, 0.0, 0.0,  0.0, 1.0, 0.0);
    g3d_camera_set_active(camera);

    // Terreno suave para que la sombra se aprecie bien
    terrain_mesh = g3d_primitive_terrain(100, 50.0, 2.0, 12.0, 7);
    grass_tex = g3d_load_texture("assets/grass.png");
    terrain_mat = g3d_material_create();
    g3d_material_set_color(terrain_mat, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_roughness(terrain_mat, 0.9);
    IF (grass_tex >= 0)
        g3d_material_set_texture(terrain_mat, 0, grass_tex);
    END
    terrain_ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(terrain_ent, terrain_mesh);
    g3d_entity_set_material(terrain_ent, terrain_mat);
    say("OK: terrain ready");

    // Cubo elevado sobre el terreno (proyectara su sombra)
    cube_mesh = g3d_primitive_cube();
    cube_mat = g3d_material_create();
    g3d_material_set_color(cube_mat, 0.9, 0.3, 0.2, 1.0);
    cube_ent = g3d_entity_spawn(scene, 0, 0, 5, 0);   // y=5
    g3d_entity_set_mesh(cube_ent, cube_mesh);
    g3d_entity_set_material(cube_ent, cube_mat);
    g3d_entity_set_scale(cube_ent, 4.0, 4.0, 4.0);
    say("OK: cube ready");

    // Sol (luz direccional) que proyecta la sombra
    light = g3d_light_create(0, 1.0, 0.96, 0.85);
    g3d_light_set_direction(light, -0.45, -1.0, -0.35);
    g3d_light_set_intensity(light, 1.3);

    g3d_set_clear_color(0.45, 0.65, 0.95, 1.0);
    g3d_set_ambient_light(0.35, 0.37, 0.42, 1.0);

    // Activar sombras
    g3d_set_shadows(1);
    say("OK: shadows enabled");

    x = 480;
    y = 270;

    LOOP
        // Girar el cubo lentamente para ver la sombra cambiar
        ang += 0.01;
        g3d_entity_set_rotation(cube_ent, 0.0, ang, 0.0);

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        frames += 1;
        IF (frames > 600) BREAK; END
        FRAME;
    END

    g3d_shutdown();
    say("=== Test Complete ===");
END
