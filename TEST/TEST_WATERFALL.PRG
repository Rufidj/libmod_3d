// Tech demo: a mountain with a stepped gorge cascade flowing into a lake.
// Free-fly camera: WASD move, arrows look, Q/E up-down, ESC quit.
// Compile: bgdc TEST_WATERFALL.PRG   Run: bgdi TEST_WATERFALL.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int light;
    int mtn_mesh;
    int mtn_mat;
    int terr_tex;
    int water_tex;
    int pe;
    int spray;
    int splash;
    
    float htop;
    float hmid;
    // camara libre
    float cx = 0.0;
    float cy = 10.0;
    float cz = -44.0;
    float yaw = 0.0;     // grados
    float pitch = -2.0;  // grados
    float fwx; float fwy; float fwz;
    float mspeed = 0.6;
    float tspeed = 2.0;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);


    scene = g3d_scene_create("Waterfall");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Montana con garganta escalonada
    mtn_mesh = g3d_primitive_mountain(160, 90.0, 30.0, 20.0, 7, 1.0);
    terr_tex = g3d_load_texture("assets/terrain.png");
    IF (terr_tex < 0) terr_tex = g3d_load_texture("assets/grass.png"); END
    mtn_mat = g3d_material_create();
    g3d_material_set_color(mtn_mat, 1.0, 1.0, 1.0, 1.0);
    g3d_material_set_roughness(mtn_mat, 0.9);
    IF (terr_tex >= 0) g3d_material_set_texture(mtn_mat, 0, terr_tex); END
    pe = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(pe, mtn_mesh);
    g3d_entity_set_material(pe, mtn_mat);

    light = g3d_light_create(0, 1.0, 1.0, 1.0);
    g3d_set_shadows(1);
    g3d_set_sun(0.22);

    water_tex = g3d_load_texture("assets/water.png");

    // Lago
    g3d_water_create(0.0, 240.0, 160);
    g3d_water_set_waves(0.25, 6.0, 1.4);
    g3d_water_set_color(0.02, 0.15, 0.26,  0.15, 0.40, 0.52);
    IF (water_tex >= 0) g3d_water_set_texture(water_tex); END

    // Cascada = AGUA (como el mar) bajando por la garganta, con el mismo color
    // azul/fresnel/brillo. Sigue el terreno y se recorta en la superficie.
    IF (water_tex >= 0) g3d_flow_set_texture(water_tex); END
    g3d_flow_set_color(0.05, 0.22, 0.34);   // agua profunda (azul)
    g3d_flow_add_river(mtn_mesh, 0.0, -2.0,  0.0, -34.0,  6.0, 0.6, 1.6, 7.0);
    g3d_flow_set_clip(0.3);

    // + Partículas de spray emitidas desde lo alto con velocidad cuesta abajo
    // (siguen la garganta y se desvanecen en el agua; sin gotas en el cielo)
    spray = g3d_particles_create(0.0, 13.0, -5.0,  2.0, 1.0, 2.0,   0.0, -4.0, -9.0,  1.5, 9.0, 0.15, 2.6, 6000);
    g3d_particles_set_color(spray, 0.92, 0.96, 1.0);
    g3d_particles_set_floor(spray, 0.5);

    // Salpicadura en el impacto con el lago
    splash = g3d_particles_create(0.0, 0.4, -30.0,  3.0, 0.2, 3.0,   0.0, 5.0, 0.0,  3.5, 12.0, 0.15, 1.0, 2500);
    g3d_particles_set_color(splash, 1.0, 1.0, 1.0);
    g3d_particles_set_floor(splash, 0.0);

    say("WASD mover, flechas mirar, Q/E subir/bajar, ESC salir");

    x = 480;
    y = 270;

    LOOP
        // Mirar (flechas)
        IF (key(_LEFT))  yaw = yaw + tspeed; END
        IF (key(_RIGHT)) yaw = yaw - tspeed; END
        IF (key(_UP))    pitch = pitch + tspeed; END
        IF (key(_DOWN))  pitch = pitch - tspeed; END
        IF (pitch > 85.0)  pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END

        // Vector de direccion (yaw/pitch en grados -> SIN/COS usan milesimas)
        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);

        // Mover (WASD) en el plano segun el yaw
        IF (key(_W))
            cx = cx + fwx * mspeed;  cy = cy + fwy * mspeed;  cz = cz + fwz * mspeed;
        END
        IF (key(_S))
            cx = cx - fwx * mspeed;  cy = cy - fwy * mspeed;  cz = cz - fwz * mspeed;
        END
        IF (key(_A))
            cx = cx + cos(yaw * 1000) * mspeed;  cz = cz - sin(yaw * 1000) * mspeed;
        END
        IF (key(_D))
            cx = cx - cos(yaw * 1000) * mspeed;  cz = cz + sin(yaw * 1000) * mspeed;
        END
        IF (key(_Q)) cy = cy - mspeed; END
        IF (key(_E)) cy = cy + mspeed; END

        g3d_camera_set_position(camera, cx, cy, cz);
        g3d_camera_look_at(camera, cx + fwx, cy + fwy, cz + fwz,  0.0, 1.0, 0.0);

        
        

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    
    say("=== Done ===");
END
