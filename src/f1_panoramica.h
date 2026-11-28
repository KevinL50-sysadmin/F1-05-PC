// Formula One 05: the game's own widescreen option, read from the guest RAM for the presenter (odd/tasks/pantalla-ancha.md
// T2). Header only and without the runtime, so ps2xTest (ps2_f1_panoramica_tests.cpp) tests the same code the game runs.
#pragma once

#include "runtime/ps2_memory.h"

#include <cstdint>
#include <cstring>

namespace f1::panoramica
{
    // The profile object (its pointer in 0x41586C, returned by func_14F380) has the widescreen flag, a 32-bit 0/1 word,
    // in +0x28: func_133F48 passes it to the camera (func_2C5080 -> func_2C5028, 4:3 or 16:9) and the Lua setter
    // func_146248 writes it. The pointer stays 0 until func_14F000 (the Lua library start).
    constexpr uint32_t kPunteroPerfil = 0x0041586Cu;
    constexpr uint32_t kMarcaPanoramica = 0x28u;
    constexpr uint32_t kCadaLecturas = 10u;
    constexpr int kSinCambios = -1;

    struct Lector
    {
        uint32_t lecturas = 0;
        int anterior = -1; // -1: nothing reported yet, or the pointer was 0 at the last read
    };

    inline uint32_t leer32(const uint8_t *rdram, uint32_t direccion)
    {
        uint32_t valor = 0;
        std::memcpy(&valor, rdram + (direccion & PS2_RAM_MASK), sizeof(valor));
        return valor;
    }

    // Called on every frame (the game's scePadRead). Like F1 06, only one call in kCadaLecturas reads the RAM. Returns
    // the flag to report (0 = 4:3, 1 = 16:9) when it changed or the pointer became valid again, otherwise kSinCambios.
    inline int leer(Lector &lector, const uint8_t *rdram)
    {
        if ((lector.lecturas++ % kCadaLecturas) != 0u)
            return kSinCambios;
        const uint32_t perfil = leer32(rdram, kPunteroPerfil);
        if (perfil == 0u)
        {
            lector.anterior = -1;
            return kSinCambios;
        }
        const int marca = leer32(rdram, perfil + kMarcaPanoramica) != 0u ? 1 : 0;
        if (marca == lector.anterior)
            return kSinCambios;
        lector.anterior = marca;
        return marca;
    }
}
