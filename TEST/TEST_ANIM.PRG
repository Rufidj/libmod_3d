// Demo de animacion esqueletal: carga un modelo con esqueleto y reproduce sus
// animaciones glTF. LEFT/RIGHT cambian de animacion, la camara orbita.
// Cambia MODEL_PATH por el modelo que quieras probar.
// Compile: bgdc TEST_ANIM.PRG   Run: bgdi TEST_ANIM.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    MODEL_PATH = "assets/crash.glb";
END

GLOBAL
    int scene; int camera; int light;
    int model; int nsub; int i;
    int parts[64];
    
    float t = 0.0;
    int anim = 0; int nanim = 0;
    float dur = 0.0;
    int prevl = 0; int prevr = 0;
    float camang = 0.0;
END

PROCESS main()
PRIVATE
    int j; int sm; int st; int mat; int e; float h; float sc;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);
    

    scene = g3d_scene_create("Anim");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    model = g3d_load_gltf(MODEL_PATH);
    IF (model < 0)
        say("ERROR cargando " + MODEL_PATH);
        RETURN;
    END
    h = g3d_model_height(model);
    IF (h <= 0.0) h = 1.0; END
    sc = 5.0 / h;
    nsub = g3d_model_submesh_count(model);
    nanim = g3d_model_anim_count(model);
    dur = g3d_model_anim_duration(model, anim);
    say("Modelo: " + MODEL_PATH + "  submallas=" + itoa(nsub) + "  animaciones=" + itoa(nanim));

    FOR (i = 0; i < nsub; i = i + 1)
        sm = g3d_model_submesh(model, i);
        st = g3d_model_submesh_texture(model, i);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        e = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(e, sm);
        g3d_entity_set_material(e, mat);
        g3d_entity_set_scale(e, sc, sc, sc);
        parts[i] = e;
    END

    light = g3d_light_create(0, 1.0, 1.0, 0.95);
    g3d_light_set_direction(light, -0.4, -0.7, -0.4);
    g3d_light_set_intensity(light, 1.3);
    g3d_set_ambient_light(0.45, 0.47, 0.5, 1.0);
    g3d_sky_set_gradient(0.30, 0.50, 0.85,  0.80, 0.88, 0.96);
    g3d_set_shadows(1);

    say("LEFT/RIGHT cambian de animacion. ESC para salir.");
    x = 480; y = 270;

    LOOP
        // Avanzar el tiempo y animar (en bucle)
        t = t + 0.0166;
        IF (nanim > 0) g3d_model_animate(model, anim, t, 1); END

        // Cambiar de animacion
        IF (key(_RIGHT) AND prevr == 0 AND nanim > 0)
            anim = anim + 1; IF (anim >= nanim) anim = 0; END
            t = 0.0; dur = g3d_model_anim_duration(model, anim);
        END
        prevr = key(_RIGHT);
        IF (key(_LEFT) AND prevl == 0 AND nanim > 0)
            anim = anim - 1; IF (anim < 0) anim = nanim - 1; END
            t = 0.0; dur = g3d_model_anim_duration(model, anim);
        END
        prevl = key(_LEFT);

        // Camara orbitando despacio
        camang = camang + 0.3;
        g3d_camera_set_position(camera, sin(camang*1000)*9.0, 3.5, cos(camang*1000)*9.0);
        g3d_camera_look_at(camera, 0.0, 2.6, 0.0,  0.0, 1.0, 0.0);

        
        

        write(0, 10, 10, 0, "Animacion " + itoa(anim) + "/" + itoa(nanim) + "  dur=" + ftoa(dur) + "s");
        write(0, 10, 28, 0, "LEFT/RIGHT cambian, ESC sale");

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    
END
