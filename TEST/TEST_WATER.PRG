// Tech demo: cliffs/coast with the sea filling the low basins, animated water,
// moving sun. Compile: bgdc TEST_WATER.PRG   Run: bgdi TEST_WATER.dcb

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
    int terr_tex;
    int water_tex;
    int pe;
    
    float tod = 0.16;
    float tspeed = 0.0004;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    IF (!g3d_init(960, 540))
        say("ERROR: 3D init failed");
        RETURN;
    END

    scene = g3d_scene_create("Water");
    g3d_scene_set_active(scene);

    camera = g3d_camera_create();
    g3d_camera_set_position(camera, 0.0, 9.0, -42.0);
    g3d_camera_look_at(camera, 0.0, 2.0, 4.0,  0.0, 1.0, 0.0);
    g3d_camera_set_active(camera);

    // Acantilados: terreno alto y escarpado, cuencas hundidas para el agua
    // (grid, tam, altura, tiling, semilla, pendiente, suelo_agua)
    terrain_mesh = g3d_primitive_cliffs(150, 80.0, 16.0, 18.0, 2024, 3.5, 0.42);

    // Textura del terreno: usa assets/terrain.png si existe, si no hierba
    terr_tex = g3d_load_texture("assets/terrain.png");
    IF (terr_tex < 0)
        terr_tex = g3d_load_texture("assets/grass.png");
    END
    terrain_mat = g3d_material_create();
    g3d_material_set_color(terrain_mat, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_roughness(terrain_mat, 0.9);
    IF (terr_tex >= 0)
        g3d_material_set_texture(terrain_mat, 0, terr_tex);
    END
    pe = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(pe, terrain_mesh);
    g3d_entity_set_material(pe, terrain_mat);

    // Sol + sombras
    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_set_shadows(1);

    // Mar: nivel 0.5 (llena las cuencas hundidas), gran extension
    g3d_water_create(0.5, 220.0, 180);
    g3d_water_set_waves(0.4, 7.0, 1.6);
    g3d_water_set_color(0.02, 0.16, 0.28,  0.16, 0.42, 0.55);
    // Textura del agua (detalle/ripples)
    water_tex = g3d_load_texture("assets/water.png");
    IF (water_tex >= 0)
        g3d_water_set_texture(water_tex);
        say("OK: water texture applied");
    END
    say("OK: cliffs + water ready");

    x = 480;
    y = 270;

    LOOP
        tod = tod + tspeed;
        IF (tod >= 0.5) tod = 0.12; END
        g3d_set_sun(tod);

        
        

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    
    say("=== Done ===");
END
