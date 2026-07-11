// ============================================================================
//  TEST_DESTROY.PRG - Destruccion con Jolt: un muro de cajas de madera que
//  demueles a barrilazos. Cada caja y cada barril es un cuerpo rigido Jolt.
//  WASD+raton = camara,  ESPACIO = lanzar barril,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int mbox;
    int mbarrel;
    int quit;
END

// Un cuerpo rigido generico (caja Jolt) + su modelo. Un proceso por objeto.
PROCESS prop(int model, float hx, float hy, float hz, float scale, float mass,
             float px, float py, float pz, float ivx, float ivy, float ivz)
PRIVATE
    int body; int mdl;
BEGIN
    mdl  = g3d_model_spawn(scene, model, px, py, pz, scale, 0);
    body = g3d_rigidbody_create(px, py, pz, hx, hy, hz, mass);
    IF (ivx <> 0.0 OR ivy <> 0.0 OR ivz <> 0.0)
        g3d_rigidbody_set_velocity(body, ivx, ivy, ivz);
    END
    LOOP
        g3d_entity_set_position(mdl, g3d_rigidbody_render_x(body),
                                     g3d_rigidbody_render_y(body),
                                     g3d_rigidbody_render_z(body));
        g3d_entity_set_rotation(mdl, g3d_rigidbody_angle_x(body),
                                     g3d_rigidbody_angle_y(body),
                                     g3d_rigidbody_angle_z(body));
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_rigidbody_destroy(body);
END

PROCESS main()
PRIVATE
    int light; int i; int col; int row; int ksp;
    float dt; float msens;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz; float spd;
    float sx; float sz; float gy; float ty; float tz;
    float wallx; float wallz; float wallg; float bx; float by;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.93, 0.82);
    g3d_light_set_direction(light, -0.5, -0.7, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.30, 0.34, 0.42, 1.0);

    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.75, 0.80, 0.85);
    g3d_sky_set_clouds(0.5, 1.0);
    g3d_sky_enable(1);

    g3d_set_hdr(1);
    g3d_set_tonemap(1);
    g3d_set_exposure(1.0);
    g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);

    mbox    = g3d_load_gltf("assets/wood_box.glb");
    mbarrel = g3d_load_gltf("assets/barrel.glb");

    // tierra firme
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz = 110.0 - i * 3.0;
        ty = g3d_scene_terrain_height(0.0, tz);
        IF (ty > 4.0) sz = tz; gy = ty; BREAK; END
    END

    g3d_physics_set_gravity(24.0);

    // --- MURO de cajas de madera (6 ancho x 5 alto, cubos de 2u) ---
    wallx = sx; wallz = sz - 24.0;
    wallg = g3d_scene_terrain_height(wallx, wallz);
    FOR (row = 0; row < 5; row = row + 1)
        FOR (col = 0; col < 6; col = col + 1)
            bx = wallx + (col - 2.5) * 2.02;
            by = wallg + 1.0 + row * 2.02;
            // caja: semiejes 1,1,1 (cubo de 2u), escala visual 2, masa 4
            prop(mbox, 1.0, 1.0, 1.0, 2.0, 4.0, bx, by, wallz, 0.0, 0.0, 0.0);
        END
    END

    quit = 0;
    camx = sx; camy = gy + 4.0; camz = sz + 6.0;
    yaw = 180.0; pitch = -6.0; msens = 0.18; dt = 1.0 / 60.0; spd = 1.0;

    x = 480; y = 270;
    write(0, 10, 10, 0, "WASD+raton mover - ESPACIO lanzar barril - ESC salir");
    write(0, 820, 10, 0, "FPS:"); write_var(0, 860, 10, 0, frame_info.fps);
    g3d_mouse_capture(1);

    LOOP
        g3d_rigidbody_step(dt);

        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        // lanzar barril pesado y rapido -> destroza el muro
        IF (key(_SPACE) AND ksp == 0)
            prop(mbarrel, 1.2, 1.5, 1.2, 3.0, 9.0,
                 camx + fwx * 4.5, camy + fwy * 4.5, camz + fwz * 4.5,
                 fwx * 32.0, fwy * 32.0 + 2.0, fwz * 32.0);
        END
        ksp = key(_SPACE);

        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
END
