// ============================================================================
//  TEST_BENCH.PRG - Medida de culling a PIE DE CALLE (no aerea).
//  Se coloca en la posicion real de juego y reporta draws/triangulos/culled.
//  Cambiar CHUNK para comparar:  -1.0 = auto,  0.0 = sin trocear.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    CHUNK = -1.0;     // -1 = chunking auto,  0 = desactivado
END

GLOBAL
    int scene; int camera; int light; int model; int ent;
    float camx; float camy; float camz;
END

PROCESS main()
PRIVATE
    int n;
    float yaw; float pitch;
    float fwx; float fwy; float fwz;
BEGIN
    set_mode(1920, 1080, MODE_WINDOW);
    set_fps(0, 0);

    scene = g3d_scene_create("bench");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.98, 0.92);
    g3d_light_set_direction(light, -0.55, -0.35, -0.5);
    g3d_light_set_intensity(light, 0.7);
    g3d_set_ambient_light(0.55, 0.58, 0.64, 1.0);
    g3d_sky_set_gradient(0.45, 0.60, 0.85,  0.85, 0.90, 0.97); g3d_sky_enable(1);
    g3d_set_shadows(0);
    g3d_set_culling(1);

    g3d_gltf_set_chunking(CHUNK);
    model = g3d_load_gltf("assets/niagara_bistro-master/bistro.gltf");
    ent   = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);

    // Posicion real de juego: a pie de calle mirando calle abajo
    // (la misma de la captura: cam X -8.0  Y 8.3  Z 10.3)
    camx = -8.0; camy = 8.3; camz = 10.3;
    yaw = 200.0; pitch = -8.0;
    fwx = sin(yaw * 1000) * cos(pitch * 1000);
    fwy = sin(pitch * 1000);
    fwz = cos(yaw * 1000) * cos(pitch * 1000);
    g3d_camera_set_position(camera, camx, camy, camz);
    g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

    // Deja correr unos frames y sal: las stats del motor van al log
    FOR (n = 0; n < 90; n++)
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);
        FRAME;
    END
    exit();
END
