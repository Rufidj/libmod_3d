// ============================================================================
//  TEST_WALK.PRG - Character controller en 1a persona sobre la playa.
//  WASD = andar,  raton = mirar,  ESPACIO = saltar,  ESC = salir.
//  Demuestra: gravedad, colision con el terreno, subir rampas, deslizar en
//  pendientes empinadas y chocar con cajas (AABB) - el buggy es un obstaculo.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
END

// Coloca un modelo (escala a altura th, rota roty grados) - igual que en la playa.
FUNCTION place_model(int model, float px, float py, float pz, float th, float roty)
PRIVATE
    int n; int j; int sm; int st; int mat; int ent; float mh; float s;
    int snorm; int smetal; int srough;
BEGIN
    IF (model < 0) RETURN; END
    mh = g3d_model_height(model);
    IF (mh <= 0.0) mh = 1.0; END
    s = th / mh;
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        snorm = g3d_model_submesh_map(model, j, 1);
        smetal = g3d_model_submesh_map(model, j, 2);
        srough = g3d_model_submesh_map(model, j, 3);
        IF (snorm >= 0) g3d_material_set_map(mat, 1, snorm); END
        IF (smetal >= 0) g3d_material_set_map(mat, 2, smetal); END
        IF (srough >= 0) g3d_material_set_map(mat, 3, srough); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, s, s, s);
        g3d_entity_set_position(ent, px, py, pz);
        g3d_entity_set_rotation(ent, 0.0, roty * 1000, 0.0);
    END
END

PROCESS main()
PRIVATE
    int light; int gid; int ch; int mbuggy; int i;
    float yaw; float pitch; float msens; float dt;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    float wx; float wz; float wl; float spd; float eye;
    float sx; float sz; float gy; float ty; float tz;
    float px; float py; float pz;
    float bx; float by; float bz;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Amanecer suave: se ve bien para explorar, con sombras marcadas.
    light = g3d_light_create(0, 1.0, 0.93, 0.82);
    g3d_light_set_direction(light, -0.5, -0.7, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.30, 0.34, 0.42, 1.0);

    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.75, 0.80, 0.85);
    g3d_sky_set_clouds(0.5, 1.0);
    g3d_sky_set_low_clouds(0.5, 55.0, 26.0, 1.0);   // nubes bajas + sus sombras al andar
    g3d_sky_enable(1);

    g3d_set_hdr(1);
    g3d_set_tonemap(1);
    g3d_set_exposure(1.0);
    g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6);
    g3d_water_set_ocean(0.0, 1.0, 1.2);

    // --- Buscar un punto de tierra firme para aparecer ---
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz = 110.0 - i * 3.0;
        ty = g3d_scene_terrain_height(0.0, tz);
        IF (ty > 4.0) sz = tz; gy = ty; BREAK; END
    END

    // --- Buggy como obstaculo solido en el camino ---
    mbuggy = g3d_load_fbx("assets/source/VHC_SC_Buggy_01.fbx");
    bx = sx + 6.0; bz = sz - 14.0; by = g3d_scene_terrain_height(bx, bz);
    place_model(mbuggy, bx, by, bz, 4.0, 30.0);
    // AABB alrededor del buggy (semiejes ~3.5 x 4 alto x 2)
    g3d_collider_add_box(bx - 3.5, by, bz - 2.0,  bx + 3.5, by + 4.0, bz + 2.0);

    // --- Character controller (capsula) ---
    g3d_physics_set_gravity(30.0);
    ch = g3d_char_create(sx, gy + 2.0, sz, 0.6, 3.0);
    g3d_char_set_tuning(ch, 0.8, 46.0);   // escalon 0.8, pendiente max 46 grados

    yaw = 180.0; pitch = -8.0; msens = 0.18; dt = 1.0 / 60.0;
    spd = 18.0; eye = 2.7;
    g3d_mouse_capture(1);

    x = 480; y = 270;                 // centrar el render 960x540 en la pantalla
    write(0, 10, 10, 0, "WASD andar - raton mirar - ESPACIO saltar - ESC salir");
    write_var(0, 10, 26, 0, py);
    write_var(0, 10, 42, 0, pz);

    LOOP
        // ---- mirar con el raton ----
        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END

        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);
        // ejes horizontales para moverse (sin componente vertical)
        rgx = cos(yaw * 1000);
        rgz = -sin(yaw * 1000);

        // ---- intencion de movimiento (WASD) ----
        wx = 0.0; wz = 0.0;
        IF (key(_W)) wx = wx + sin(yaw * 1000); wz = wz + cos(yaw * 1000); END
        IF (key(_S)) wx = wx - sin(yaw * 1000); wz = wz - cos(yaw * 1000); END
        IF (key(_D)) wx = wx + rgx; wz = wz + rgz; END
        IF (key(_A)) wx = wx - rgx; wz = wz - rgz; END
        wl = sqrt(wx * wx + wz * wz);
        IF (wl > 0.001) wx = wx / wl * spd; wz = wz / wl * spd; END
        g3d_char_move(ch, wx, wz);

        IF (key(_SPACE)) g3d_char_jump(ch, 13.0); END

        g3d_char_update(ch, dt);

        // ---- camara en los ojos del personaje ----
        px = g3d_char_x(ch); py = g3d_char_y(ch); pz = g3d_char_z(ch);
        g3d_camera_set_position(camera, px, py + eye, pz);
        g3d_camera_look_at(camera, px + fwx, py + eye + fwy, pz + fwz, 0.0, 1.0, 0.0);

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    g3d_shutdown();
END
