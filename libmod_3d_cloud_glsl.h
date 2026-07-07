/*
 * libmod_3d_cloud_glsl.h - shared GLSL for world-space low clouds.
 *
 * Used by BOTH the renderer's fullscreen volumetric cloud pass and the scene
 * shader's cloud-shadow term, so the density field they sample is identical.
 *
 * The host shader must ALSO declare: uniform float uTime; uniform vec3 uSunDir;
 */
#ifndef __LIBMOD_3D_CLOUD_GLSL_H
#define __LIBMOD_3D_CLOUD_GLSL_H

#define G3D_CLOUD_GLSL \
    "uniform float uCloudCover; uniform float uCloudSpeed;\n" \
    "uniform float uCloudBase;  uniform float uCloudThick;\n" \
    "float h13(vec3 p){ p=fract(p*0.3183099+0.1); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }\n" \
    "float vn3(vec3 p){ vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n" \
    "  return mix(mix(mix(h13(i+vec3(0,0,0)),h13(i+vec3(1,0,0)),f.x), mix(h13(i+vec3(0,1,0)),h13(i+vec3(1,1,0)),f.x),f.y),\n" \
    "             mix(mix(h13(i+vec3(0,0,1)),h13(i+vec3(1,0,1)),f.x), mix(h13(i+vec3(0,1,1)),h13(i+vec3(1,1,1)),f.x),f.y), f.z); }\n" \
    "float fbm3(vec3 p){ float s=0.0,a=0.5; for(int i=0;i<3;i++){ s+=a*vn3(p); p*=2.03; a*=0.5; } return s; }\n" \
    "float lowCloudDensity(vec3 p){\n" \
    "  float yb=uCloudBase, yt=uCloudBase+uCloudThick;\n" \
    "  vec3 wp = p*0.010 + vec3(uTime*uCloudSpeed*0.03, 0.0, uTime*uCloudSpeed*0.012);\n" \
    "  float base=fbm3(wp); float det=fbm3(wp*4.0+2.7)*0.35;\n" \
    "  float h=clamp((p.y-yb)/max(yt-yb,0.001),0.0,1.0);\n" \
    "  float grad=smoothstep(0.0,0.15,h)*smoothstep(1.0,0.5,h);\n" \
    "  float d=(base-det-(1.0-uCloudCover*0.9))*grad; return clamp(d*2.5,0.0,1.0); }\n" \
    "float cloudShadow(vec3 wp){\n" \
    "  if (uCloudCover<=0.0) return 1.0; vec3 s=normalize(uSunDir); if (s.y<0.05) return 1.0;\n" \
    "  float t0=(uCloudBase-wp.y)/s.y, t1=(uCloudBase+uCloudThick-wp.y)/s.y; if (t1<=0.0) return 1.0; t0=max(t0,0.0);\n" \
    "  float acc=0.0; float dt=(t1-t0)/3.0; for(int i=0;i<3;i++) acc+=lowCloudDensity(wp+s*(t0+(float(i)+0.5)*dt));\n" \
    "  return mix(1.0, exp(-acc*dt*0.06), 0.85); }\n"

#endif /* __LIBMOD_3D_CLOUD_GLSL_H */
