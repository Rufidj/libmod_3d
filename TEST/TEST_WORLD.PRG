// Mundo abierto: terreno gigante + bosque de arboles y rocas INSTANCIADOS (un
// draw call por submalla) con viento + culling distancia/frustum, y montañas en
// los bordes. FPS sin limite. Camara libre: WASD/raton/QE, +/- velocidad, ESC.
// Compile: bgdc TEST_WORLD.PRG   Run: bgdi TEST_WORLD.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    TERRAIN = 500.0;   // tamaño del mundo
    // CONFIGURABLE (esto es lo que el editor ajustara):
    NUM_TREES = 1000;
    NUM_ROCKS = 200;
    NUM_MOUNTAINS = 40;
END

GLOBAL
    int scene; int camera; int light;
    int grass_tex; int terr_tex; int ground; int ground_mat; int mtn_mat;
    int i;
    
    float cx = 0.0; float cy = 35.0; float cz = -120.0;
    float yaw = 0.0; float pitch = -18.0;
    float fwx; float fwy; float fwz;
    float ms = 1.5; float msens = 0.12;
END

PROCESS main()
PRIVATE
    int tree; int rock; int mtn1; int mtn2; int mtn3; int mcount;
BEGIN
    set_mode(960, 540);
    set_fps(0, 0);                 // sin limite, para ver FPS reales
    

    scene = g3d_scene_create("World");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Terreno gigante (grid 250 = maximo por malla; mundo de 500u)
    grass_tex = g3d_load_texture("assets/grass.png");
    ground = g3d_primitive_terrain(250, TERRAIN, 28.0, 120.0, 7);
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 1.0, 1.0, 1.0, 1.0);
    IF (grass_tex >= 0) g3d_material_set_texture(ground_mat, 0, grass_tex); END
    i = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(i, ground);
    g3d_entity_set_material(i, ground_mat);

    terr_tex = g3d_load_texture("assets/terrain.png");
    mtn_mat = g3d_material_create();
    g3d_material_set_color(mtn_mat, 1.0, 1.0, 1.0, 1.0);
    IF (terr_tex >= 0) g3d_material_set_texture(mtn_mat, 0, terr_tex); END

    // Cielo + niebla + sol
    g3d_sky_set_gradient(0.30, 0.50, 0.85,  0.80, 0.88, 0.96);
    g3d_set_fog(1,  0.80, 0.88, 0.96,  140.0, 340.0);
    light = g3d_light_create(0, 1.0, 1.0, 0.95);
    g3d_light_set_direction(light, -0.4, -0.85, -0.35);
    g3d_light_set_intensity(light, 1.2);
    g3d_set_ambient_light(0.5, 0.52, 0.55, 1.0);

    // Poblado dirigido por datos (lo que el editor configurara): cantidad,
    // area, altura objetivo, variacion de escala, viento, semilla.
    tree = g3d_load_gltf("assets/tree.glb");
    rock = g3d_load_gltf("assets/rock.glb");
    g3d_scatter_model(tree, ground, NUM_TREES, 240.0, 12.0, 0.3, 1.0, 1234);
    g3d_scatter_model(rock, ground, NUM_ROCKS,  240.0,  3.5, 0.4, 0.0, 777);

    // Montañas INSTANCIADAS: 3 mallas base por variedad, repartidas (baratas)
    mcount = NUM_MOUNTAINS / 3;
    mtn1 = g3d_primitive_mountain(48, 80.0, 60.0, 14.0, 11, 0.0);
    mtn2 = g3d_primitive_mountain(48, 90.0, 70.0, 14.0, 47, 0.0);
    mtn3 = g3d_primitive_mountain(48, 75.0, 55.0, 14.0, 93, 0.0);
    g3d_scatter_mesh(mtn1, terr_tex, ground, mcount, 240.0, 1.4, 2.4, 0.0, 5);
    g3d_scatter_mesh(mtn2, terr_tex, ground, mcount, 240.0, 1.4, 2.4, 0.0, 6);
    g3d_scatter_mesh(mtn3, terr_tex, ground, NUM_MOUNTAINS - mcount*2, 240.0, 1.4, 2.4, 0.0, 7);

    say("Mundo abierto. WASD/raton/QE mover, +/- velocidad, ESC salir.");
    x = 480; y = 270;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "FPS:");
    write_var(0, 70, 10, 0, frame_info.fps);

    LOOP
        g3d_mouse_update();
        yaw = yaw - g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0)  pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END

        IF (key(_EQUALS)) ms = ms + 0.1; END
        IF (key(_MINUS)) ms = ms - 0.1; IF (ms < 0.2) ms = 0.2; END END

        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);

        IF (key(_W)) cx = cx + fwx*ms; cy = cy + fwy*ms; cz = cz + fwz*ms; END
        IF (key(_S)) cx = cx - fwx*ms; cy = cy - fwy*ms; cz = cz - fwz*ms; END
        IF (key(_A)) cx = cx + cos(yaw*1000)*ms; cz = cz - sin(yaw*1000)*ms; END
        IF (key(_D)) cx = cx - cos(yaw*1000)*ms; cz = cz + sin(yaw*1000)*ms; END
        IF (key(_Q)) cy = cy - ms; END
        IF (key(_E)) cy = cy + ms; END

        g3d_camera_set_position(camera, cx, cy, cz);
        g3d_camera_look_at(camera, cx+fwx, cy+fwy, cz+fwz,  0.0, 1.0, 0.0);

        
        

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    
END
