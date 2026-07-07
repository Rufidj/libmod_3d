// Runtime de mapa: carga un mundo entero desde un fichero .scene (terreno, cielo,
// niebla, luz, vegetacion, modelo y prefabs) con UNA llamada. Camara libre.
// Esto es lo que hara un juego: g3d_scene_load("mapa.scene").
// Compile: bgdc TEST_MAP.PRG   Run: bgdi TEST_MAP.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera;
    int gid = 0;
    float cx = 0.0; float cy = 12.0; float cz = -45.0;
    float yaw = 0.0; float pitch = -10.0;
    float fwx; float fwy; float fwz;
    float ms = 0.9; float msens = 0.12;
END

PROCESS main()
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    IF (!g3d_init(960, 540)) RETURN; END

    // Cargar el mundo entero desde el fichero de mapa
    scene = g3d_scene_load("demo.scene");
    IF (scene < 0) say("ERROR cargando demo.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    say("Mapa cargado desde demo.scene. WASD/raton/QE moverte. ESC salir.");
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
