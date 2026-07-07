// ============================================================================
//  TEST_DRIVE.PRG - Conducir el buggy (fisica de vehiculo raycast arcade).
//  W/S = acelerar/marcha atras,  A/D = girar,  ESPACIO = freno,  ESC = salir.
//  Camara de persecucion. El buggy sigue el terreno con las 4 ruedas
//  (se inclina en cuestas y salta en rampas).
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
END

PROCESS main()
PRIVATE
    int light; int gid; int mbuggy; int veh; int buggy; int i;
    float dt; float thr; float steer; float brk;
    float vx; float vy; float vz; float vyaw; float vpitch; float vroll;
    float yawoff; float cfx; float cfz; float cdist; float chh;
    float sx; float sz; float gy; float ty; float tz; float spdkmh;
    float camx; float camy; float camz; float tcx; float tcy; float tcz; float ksm;
    int clon; int kn;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

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
    g3d_sky_set_low_clouds(0.5, 60.0, 26.0, 1.0);
    g3d_sky_enable(1);

    g3d_set_hdr(1);
    g3d_set_tonemap(1);
    g3d_set_exposure(1.0);
    g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6);
    g3d_water_set_ocean(0.0, 1.0, 1.2);

    // punto de tierra firme para arrancar
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz = 110.0 - i * 3.0;
        ty = g3d_scene_terrain_height(0.0, tz);
        IF (ty > 4.0) sz = tz; gy = ty; BREAK; END
    END

    mbuggy = g3d_load_fbx("assets/source/VHC_SC_Buggy_01.fbx");
    buggy = g3d_model_spawn(scene, mbuggy, sx, gy, sz, 4.0, 0.0);   // funcion del MOTOR -> id del modelo

    veh = g3d_vehicle_create(sx, gy, sz, 0.0);
    g3d_vehicle_set_geometry(veh, 5.0, 3.0, 0.0);
    g3d_vehicle_set_tuning(veh, 30.0, 60.0, 34.0, 45.0, 0.7);
    g3d_physics_set_gravity(30.0);

    // Si el buggy conduce de lado/atras, ajusta este offset (0, 90000, 180000, -90000)
    yawoff = 0.0;
    cdist = 20.0; chh = 9.0; dt = 1.0 / 60.0;

    x = 480; y = 270;      // centrar el render 960x540 en pantalla
    write(0, 10, 10, 0, "W/S acelerar - A/D girar - ESPACIO freno - N nubes - ESC salir");
    write(0, 10, 26, 0, "km/h:");     write_var(0, 60, 26, 0, spdkmh);
    write(0, 820, 10, 0, "FPS:");     write_var(0, 860, 10, 0, frame_info.fps);
    clon = 1; camx = sx; camy = gy + chh; camz = sz - cdist; ksm = 0.25;

    LOOP
        thr = 0.0; steer = 0.0; brk = 0.0;
        IF (key(_W)) thr = 1.0; END
        IF (key(_S)) thr = -1.0; END
        IF (key(_A)) steer = 1.0; END
        IF (key(_D)) steer = -1.0; END
        IF (key(_SPACE)) brk = 1.0; END

        g3d_vehicle_update(veh, dt, thr, steer, brk);

        vx = g3d_vehicle_x(veh); vy = g3d_vehicle_y(veh); vz = g3d_vehicle_z(veh);
        vyaw = g3d_vehicle_yaw(veh);
        vpitch = g3d_vehicle_pitch(veh); vroll = g3d_vehicle_roll(veh);
        spdkmh = g3d_vehicle_speed(veh) * 3.6;

        // mover TODO el buggy con su unico handle raiz
        g3d_entity_set_position(buggy, vx, vy, vz);
        g3d_entity_set_rotation(buggy, vpitch, vyaw + yawoff, vroll);

        // toggle de nubes (para medir su coste en FPS)
        IF (key(_N) AND kn == 0)
            clon = 1 - clon;
            IF (clon)
                g3d_sky_set_low_clouds(0.5, 60.0, 26.0, 1.0);
                g3d_sky_set_clouds(0.5, 1.0);
            ELSE
                g3d_sky_set_low_clouds(0.0, 60.0, 26.0, 1.0);
                g3d_sky_set_clouds(0.0, 1.0);
            END
        END
        kn = key(_N);

        // camara de persecucion detras del buggy (suavizada)
        cfx = sin(vyaw);      // yaw en radianes -> mili-grados: rad*180/pi*1000
        cfz = cos(vyaw);
        tcx = vx - cfx * cdist; tcy = vy + chh; tcz = vz - cfz * cdist;
        camx = camx + (tcx - camx) * ksm;
        camy = camy + (tcy - camy) * ksm;
        camz = camz + (tcz - camz) * ksm;
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, vx, vy + 2.0, vz, 0.0, 1.0, 0.0);

        gid = g3d_render(gid);
        graph = gid;

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_shutdown();
END
