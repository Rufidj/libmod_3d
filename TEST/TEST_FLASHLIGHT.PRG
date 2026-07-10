// Demo de luces: escena nocturna con antorchas de colores (point lights) y una
// LINTERNA (spotlight) que sigue a la camara estilo FPS. Camara libre.
// Compile: bgdc TEST_FLASHLIGHT.PRG   Run: bgdi TEST_FLASHLIGHT.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera;
    int grass_tex; int ground_mesh; int ground_mat;
    int torch1; int torch2; int torch3; int flash;
    int wall_mesh; int wall_mat; int wall;
    int e; int i;
    int spin[64]; int spin_n = 0; float spin_yaw = 0.0;
    int vm[32]; int vm_n = 0;          // entidades del viewmodel (linterna en mano)
    float hx; float hy; float hz;       // posicion de la linterna sostenida
    
    float cx = 0.0; float cy = 8.0; float cz = -24.0;
    float yaw = 0.0; float pitch = -14.0;
    float fwx; float fwy; float fwz;
    float ms = 0.7; float ts = 2.0;
    float t = 0.0;
    int flash_on = 1; int prev_f = 0;
    float msens = 0.12;   // sensibilidad del raton (grados por pixel)
    int prev_mx = 0; int prev_my = 0;
END

// Carga un modelo glTF, lo escala a target_h y lo planta en (px,pz) sobre el suelo.
FUNCTION place_model(string path, float px, float pz, float target_h)
PRIVATE
    int model; int n; int j; int sm; int st; int mat; int ent; float h; float sc; float gy;
BEGIN
    model = g3d_load_gltf(path);
    IF (model < 0) RETURN; END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = target_h / h;
    gy = g3d_terrain_get_height(ground_mesh, px, pz);
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, sc, sc, sc);
        g3d_entity_set_position(ent, px, gy, pz);
        spin[spin_n] = ent; spin_n = spin_n + 1;
    END
END

// Igual que place_model pero el objeto NO gira (prop estatico de escenario).
FUNCTION place_prop(string path, float px, float pz, float target_h)
PRIVATE
    int model; int n; int j; int sm; int st; int mat; int ent; float h; float sc; float gy;
BEGIN
    model = g3d_load_gltf(path);
    IF (model < 0) RETURN; END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = target_h / h;
    gy = g3d_terrain_get_height(ground_mesh, px, pz);
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, sc, sc, sc);
        g3d_entity_set_position(ent, px, gy, pz);
    END
END

// Carga un modelo como viewmodel (en la mano): se spawnea en el origen, se
// escala, y se guardan sus entidades en vm[] para colocarlo cada frame.
FUNCTION load_viewmodel(string path, float target_h)
PRIVATE
    int model; int n; int j; int sm; int st; int mat; int ent; float h; float sc;
BEGIN
    model = g3d_load_gltf(path);
    IF (model < 0) RETURN; END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = target_h / h;
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, sc, sc, sc);
        vm[vm_n] = ent; vm_n = vm_n + 1;
    END
END

PROCESS main()
PRIVATE
    int moon;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    

    scene = g3d_scene_create("Linterna");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Suelo con leve relieve
    grass_tex = g3d_load_texture("assets/grass.png");
    ground_mesh = g3d_primitive_terrain(90, 90.0, 1.6, 24.0, 7);
    ground_mat = g3d_material_create();
    g3d_material_set_color(ground_mat, 1.0, 1.0, 1.0, 1.0);
    IF (grass_tex >= 0) g3d_material_set_texture(ground_mat, 0, grass_tex); END
    e = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(e, ground_mesh);
    g3d_entity_set_material(e, ground_mat);

    // Modelos repartidos para que la luz los recorra
    place_model("assets/mario.glb",  -8.0,  4.0, 6.0);
    place_model("assets/luigi.glb",   8.0,  4.0, 6.0);
    place_model("assets/mario.glb",   0.0, 16.0, 6.0);

    // Pared de fondo (cubo aplastado) para que se proyecten sombras en ella
    wall_mesh = g3d_primitive_cube();
    wall_mat = g3d_material_create();
    g3d_material_set_color(wall_mat, 0.62, 0.62, 0.66, 1.0);
    g3d_material_set_roughness(wall_mat, 0.95);
    wall = g3d_entity_spawn(scene, 0, 0, 0, 0);
    g3d_entity_set_mesh(wall, wall_mesh);
    g3d_entity_set_material(wall, wall_mat);
    g3d_entity_set_scale(wall, 70.0, 34.0, 1.0);
    g3d_entity_set_position(wall, 0.0, 16.0, 26.0);

    // Barriles estaticos que proyectan sombra (cerca del centro, dentro del
    // area del shadow map)
    place_prop("assets/barrel.glb",  -4.0,  -6.0, 4.0);
    place_prop("assets/barrel.glb",   4.0,  -7.0, 4.0);
    place_prop("assets/barrel.glb",  -12.0,  0.0, 4.0);
    place_prop("assets/barrel.glb",  12.0,  -2.0, 4.0);
    place_prop("assets/barrel.glb",   0.0,  -2.0, 4.0);

    // Noche oscura: la linterna debe dominar para que su sombra se vea
    g3d_set_ambient_light(0.05, 0.05, 0.07, 1.0);
    g3d_sky_set_gradient(0.02, 0.03, 0.08,  0.10, 0.10, 0.16);
    g3d_set_fog(1,  0.04, 0.05, 0.10,  40.0, 100.0);

    // Luna MUY tenue, solo para que fuera del haz no sea negro absoluto (si es
    // muy brillante rellena la sombra de la linterna y no se ve)
    moon = g3d_light_create(0, 0.4, 0.45, 0.7);
    g3d_light_set_direction(moon, -0.35, -0.85, 0.3);
    g3d_light_set_intensity(moon, 0.15);
    g3d_set_shadows(1);

    // Antorchas tenues empujadas a los bordes (no rellenan el centro)
    torch1 = g3d_light_create(1, 1.0, 0.4, 0.15);   // naranja
    g3d_light_set_intensity(torch1, 1.1);
    g3d_light_set_range(torch1, 14.0);
    torch2 = g3d_light_create(1, 0.2, 0.5, 1.0);    // azul
    g3d_light_set_intensity(torch2, 1.1);
    g3d_light_set_range(torch2, 14.0);
    torch3 = g3d_light_create(1, 0.3, 1.0, 0.4);    // verde
    g3d_light_set_intensity(torch3, 1.1);
    g3d_light_set_range(torch3, 14.0);

    // Linterna: foco (tipo 2) potente para que domine la escena
    flash = g3d_light_create(2, 1.0, 0.97, 0.85);
    g3d_light_set_intensity(flash, 8.0);
    g3d_light_set_range(flash, 60.0);
    g3d_light_set_cone(flash, 32.0);

    // Modelo de la linterna en primera persona (en la mano), tamaño normal
    load_viewmodel("assets/flashlight.glb", 0.5);

    say("Raton/flechas mirar, WASD mover, Q/E subir/bajar, F linterna, ESC salir.");
    x = 480; y = 270;
    // Modo raton relativo (FPS): delta limpio, cursor bloqueado, giro infinito
    g3d_mouse_capture(1);

    LOOP
        // Mirar con el raton (delta relativo del motor)
        g3d_mouse_update();
        yaw = yaw - g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;

        // Flechas tambien sirven para mirar (mismo sentido que el raton)
        IF (key(_LEFT))  yaw = yaw + ts; END
        IF (key(_RIGHT)) yaw = yaw - ts; END
        IF (key(_UP))    pitch = pitch + ts; END
        IF (key(_DOWN))  pitch = pitch - ts; END
        IF (pitch > 85.0)  pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END

        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);

        IF (key(_W)) cx = cx + fwx*ms; cy = cy + fwy*ms; cz = cz + fwz*ms; END
        IF (key(_S)) cx = cx - fwx*ms; cy = cy - fwy*ms; cz = cz - fwz*ms; END
        IF (key(_A)) cx = cx - cos(yaw*1000)*ms; cz = cz + sin(yaw*1000)*ms; END
        IF (key(_D)) cx = cx + cos(yaw*1000)*ms; cz = cz - sin(yaw*1000)*ms; END
        IF (key(_Q)) cy = cy - ms; END
        IF (key(_E)) cy = cy + ms; END

        // Toggle linterna con F
        IF (key(_F) AND prev_f == 0)
            flash_on = 1 - flash_on;
            IF (flash_on) g3d_light_set_intensity(flash, 8.0);
            ELSE g3d_light_set_intensity(flash, 0.0); END
        END
        prev_f = key(_F);

        // Antorchas orbitando en el borde (no rellenan el centro)
        t = t + 0.012;
        g3d_light_set_position(torch1, sin(t*1000)*26.0,        5.0, cos(t*1000)*26.0);
        g3d_light_set_position(torch2, sin((t+2.1)*1000)*26.0,  5.0, cos((t+2.1)*1000)*26.0);
        g3d_light_set_position(torch3, sin((t+4.2)*1000)*26.0,  5.0, cos((t+4.2)*1000)*26.0);

        // Linterna sostenida abajo-derecha de la camara (offset del ojo -> sus
        // sombras SI se ven). Foco y modelo salen del mismo punto "en la mano".
        // forward*1.7 = distancia; right*1.1 = a la DERECHA; -0.7 = abajo.
        // (en esta proyeccion la derecha de pantalla es -cos / +sin del yaw)
        hx = cx + fwx*1.7 - cos(yaw*1000)*1.1;
        hy = cy + fwy*1.7 - 0.7;
        hz = cz + fwz*1.7 + sin(yaw*1000)*1.1;

        // El foco sale de la linterna y apunta a donde miras
        g3d_light_set_position(flash, hx, hy, hz);
        g3d_light_set_direction(flash, cx + fwx*25.0 - hx, cy + fwy*25.0 - hy, cz + fwz*25.0 - hz);

        // Colocar el modelo de la linterna en la mano, orientado con la vista
        FOR (i = 0; i < vm_n; i = i + 1)
            g3d_entity_set_position(vm[i], hx, hy, hz);
            g3d_entity_set_rotation(vm[i], pitch*1000, yaw*1000, 0.0);
        END

        // Modelos girando despacio
        spin_yaw = spin_yaw + 0.01;
        FOR (i = 0; i < spin_n; i = i + 1)
            g3d_entity_set_rotation(spin[i], 0.0, spin_yaw * 57295.78, 0.0);
        END

        g3d_camera_set_position(camera, cx, cy, cz);
        g3d_camera_look_at(camera, cx+fwx, cy+fwy, cz+fwz,  0.0, 1.0, 0.0);

        
        

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);   // soltar el cursor
    
END
