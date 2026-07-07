// Prefabs: compone una "habitacion" (suelo + paredes con puerta) de cajas,
// la GUARDA a fichero, la CARGA de vuelta, y coloca varias en el mapa.
// Demuestra el ciclo componer -> guardar -> cargar -> instanciar.
// Camara libre: WASD/raton/QE. ESC salir.
// Compile: bgdc TEST_PREFAB.PRG   Run: bgdi TEST_PREFAB.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light;
    int grass_tex; int ground; int ground_mat;
    int room; int loaded; int n;
    int gid = 0;
    float cx = 0.0; float cy = 14.0; float cz = -40.0;
    float yaw = 0.0; float pitch = -12.0;
    float fwx; float fwy; float fwz;
    float ms = 0.8; float msens = 0.12;
END

// Construye una habitacion (origen = centro, suelo en y=0): suelo + 4 paredes
// con un hueco de puerta en la pared frontal. Paredes solidas (colisionador).
FUNCTION build_room(int pf)
BEGIN
    // suelo
    g3d_prefab_add_box(pf, 0.0, 0.2, 0.0,   20.0, 0.4, 20.0,  0.0,0.0,0.0,  0.40,0.32,0.26, 0);
    // pared trasera
    g3d_prefab_add_box(pf, 0.0, 3.2, -10.0,  20.0, 6.0, 0.5,  0.0,0.0,0.0,  0.62,0.60,0.64, 1);
    g3d_prefab_piece_texture(pf, "assets/terrain.png");
    // pared izquierda
    g3d_prefab_add_box(pf, -10.0, 3.2, 0.0,  0.5, 6.0, 20.0,  0.0,0.0,0.0,  0.62,0.60,0.64, 1);
    g3d_prefab_piece_texture(pf, "assets/terrain.png");
    // pared derecha
    g3d_prefab_add_box(pf, 10.0, 3.2, 0.0,   0.5, 6.0, 20.0,  0.0,0.0,0.0,  0.62,0.60,0.64, 1);
    g3d_prefab_piece_texture(pf, "assets/terrain.png");
    // pared frontal partida (hueco de puerta de 6u en el centro)
    g3d_prefab_add_box(pf, -6.5, 3.2, 10.0,  7.0, 6.0, 0.5,   0.0,0.0,0.0,  0.62,0.60,0.64, 1);
    g3d_prefab_piece_texture(pf, "assets/terrain.png");
    g3d_prefab_add_box(pf,  6.5, 3.2, 10.0,  7.0, 6.0, 0.5,   0.0,0.0,0.0,  0.62,0.60,0.64, 1);
    g3d_prefab_piece_texture(pf, "assets/terrain.png");
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    scene = g3d_scene_create("Prefabs");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    grass_tex = g3d_load_texture("assets/grass.png");
    ground = g3d_primitive_terrain(120, 200.0, 0.4, 40.0, 5);
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 1.0, 1.0, 1.0, 1.0);
    IF (grass_tex >= 0) g3d_material_set_texture(ground_mat, 0, grass_tex); END
    n = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(n, ground);
    g3d_entity_set_material(n, ground_mat);

    g3d_sky_set_gradient(0.30, 0.50, 0.85,  0.80, 0.88, 0.96);
    light = g3d_light_create(0, 1.0, 1.0, 0.95);
    g3d_light_set_direction(light, -0.4, -0.8, -0.35);
    g3d_light_set_intensity(light, 1.2);
    g3d_set_ambient_light(0.5, 0.52, 0.55, 1.0);
    g3d_set_shadows(1);

    // 1) COMPONER la habitacion
    room = g3d_prefab_create("habitacion");
    build_room(room);
    say("Prefab 'habitacion' con " + itoa(g3d_prefab_count(room)) + " piezas.");

    // 2) GUARDAR a fichero
    g3d_prefab_save(room, "room.prefab");

    // 3) CARGAR de vuelta (otro prefab desde disco)
    loaded = g3d_prefab_load("room.prefab");
    say("Cargada de disco con " + itoa(g3d_prefab_count(loaded)) + " piezas.");

    // 4) INSTANCIAR varias en el mapa (la original y la cargada, rotadas)
    g3d_prefab_instantiate(room,   scene,   0.0, 0.0,   0.0,  0.0);
    g3d_prefab_instantiate(loaded, scene,  45.0, 0.0,   5.0,  0.0);
    g3d_prefab_instantiate(loaded, scene, -45.0, 0.0,  10.0, 45.0);
    g3d_prefab_instantiate(loaded, scene,   0.0, 0.0,  50.0, 90.0);

    say("WASD/raton/QE moverte. ESC salir.");
    x = 480; y = 270;
    g3d_mouse_capture(1);

    LOOP
        g3d_mouse_update();
        yaw = yaw - g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0)  pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END

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

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    g3d_shutdown();
END
