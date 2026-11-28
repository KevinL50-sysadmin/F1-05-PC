// Formula One 05: the race cameras' field of view (settings.ini fov, the AJUSTES PC row CAMPO DE VISION; odd/tasks/
// opciones-pc.md T10). Header only and without the runtime, so ps2xTest (ps2_f1_campo_vision_tests.cpp) tests the same
// code the game runs.
//
// The camera (Ghidra, project f105; L17/T10): func_2C5080(f12 = half angle, a0 = camera, a1 = matrix, a2 = widescreen)
// copies the view matrix and stores in +0x80 atan(tan(f12) * aspect) (func_2C5028: 4/3 or 16/9) and the screen factor
// in +0x84 (func_2C4F98); func_2C5268 then builds the frustum planes from them, so the projection and the culling both
// follow +0x80. func_2C55B0(f12, f13 = near, f14 = far, a0 = camera, a1, a2) stores near and far in +0x88/+0x8C and
// calls it. The player's race cameras: the camera method 0x1EAA88 (vtable 0x42EE14; the car object's camera at +0x330)
// copies the 0x60-byte data of the current camera type and calls one of its two kinds, func_1EB768 or func_1EBDE0,
// which call func_2C55B0 with f12 = data + 0x30 * 0.75, far 2000 (returns 0x1EBA24 and 0x1EC45C) on every frame; after them
// func_2C5470 rebuilds the frustum from +0x80 times *0x48FAE0. Others, not widened (as F1 06, patch 0160, only the
// player's race cameras): the camera's creation func_111C90 (f12 = *0x479C14 = 0.3477), the fixed scene func_236008
// (*0x479C14 * 0.75), the menus' func_133F48 and the TV director's func_1EE8E8 / func_1EEC70 (from func_1EFFB0, f12 =
// its zoom), which call func_2C5080 directly. The game passes its own angle every frame, so widening it there never
// accumulates and a change shows on the next frame.
#pragma once

#include "../third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_camera_fov.h" // ps2x::widenedCameraFov (F1 06's, 1.4 rad)

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace f1::campo_vision
{
    constexpr uint32_t kCamaraEnvoltura = 0x002C55B0u;  // hooked: widens f12 for the race calls
    constexpr uint32_t kCamaraProyeccion = 0x002C5080u; // F1_FOV_DIAG only
    constexpr uint32_t kMetodoCamara = 0x001EAA88u;     // F1_FOV_DIAG only: the camera index and type
    constexpr uint32_t kFactorFrustum = 0x0048FAE0u;    // the factor func_2C5470 applies to +0x80 (F1_FOV_DIAG)

    struct Llamador
    {
        uint32_t retorno; // ra at the entry of func_2C55B0 or func_2C5080
        const char *nombre;
        bool ensancha;
    };

    inline constexpr Llamador kLlamadores[] = {
        {0x001EBA24u, "carrera 0x1EB768", true},
        {0x001EC45Cu, "carrera 0x1EBDE0", true},
        {0x00111D58u, "creacion 0x111C90", false},
        {0x00236260u, "escena 0x236008", false},
        {0x002C55C8u, "envoltura 0x2C55B0", false},
        {0x00133FF0u, "menu 0x133F48", false},
        {0x001EEC30u, "tv 0x1EE8E8", false},
        {0x001EF1B8u, "tv 0x1EEC70", false},
    };

    inline const Llamador *buscarLlamador(uint32_t retorno)
    {
        for (const Llamador &l : kLlamadores)
            if (l.retorno == retorno)
                return &l;
        return nullptr;
    }

    // Whether the call of func_2C55B0 that returns to ra is one of the player's race cameras.
    inline bool ensanchaRetorno(uint32_t retorno)
    {
        const Llamador *l = buscarLlamador(retorno);
        return l != nullptr && l->ensancha;
    }

    inline const char *nombreLlamador(uint32_t retorno)
    {
        const Llamador *l = buscarLlamador(retorno);
        return l != nullptr ? l->nombre : "otro";
    }

    // PS2X_FOV (set from settings.ini fov, or by hand): only the option's steps 0, 5 ... 30; anything else is 0.
    inline int porcentajeTexto(const char *texto)
    {
        if (texto == nullptr || *texto == '\0' || std::strlen(texto) > 2u)
            return 0;
        for (const char *c = texto; *c != '\0'; ++c)
            if (*c < '0' || *c > '9')
                return 0;
        const int valor = std::atoi(texto);
        return valor <= ps2x::kCameraFovMaxPercent && valor % 5 == 0 ? valor : 0;
    }

    // f12 at the entry of func_2C55B0 returning to ra: widened by percent for a race camera (ps2x::widenedCameraFov:
    // at most 30 %, at most 1.4 rad, never narrower), as given for anything else.
    inline float ensancharCamara(uint32_t retorno, float angulo, int porcentaje)
    {
        return ensanchaRetorno(retorno) ? ps2x::widenedCameraFov(angulo, porcentaje) : angulo;
    }
}
