// Formula One 05: pure helpers of the Lua chunk dump (F1_LUA_VOLCAR, odd/tasks/opciones-pc.md T1). Header only and
// without the runtime, so ps2xTest (ps2_f1_lua_volcado_tests.cpp) tests the same code the game runs.
#pragma once

#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace f1::lua_volcado
{
    // Larger chunks are not dumped (F1 06's limit; the biggest F1 06 chunk was well under it).
    constexpr uint32_t kTamanoMaximo = 0x00400000u;
    // Characters of the chunk name kept in the file name (the end of the path: F1 05 names are full
    // "@C:/work/lhammert/f105/F1/FO_200X_Game/..." paths).
    constexpr size_t kLargoNombre = 96u;

    struct Trozo
    {
        uint32_t puntero = 0; // masked with PS2_RAM_MASK
        uint32_t tamano = 0;
    };

    inline uint32_t leer32(const uint8_t *rdram, uint32_t direccion)
    {
        uint32_t valor = 0;
        std::memcpy(&valor, rdram + (direccion & PS2_RAM_MASK), sizeof(valor));
        return valor;
    }

    inline bool cabeEnRam(uint32_t puntero, uint32_t tamano)
    {
        return tamano <= PS2_RAM_SIZE && (puntero & PS2_RAM_MASK) <= PS2_RAM_SIZE - tamano;
    }

    // The data of the one-shot readers: {pointer at +0, size at +4}. getS (0x2AE4F8) reads them with two lw; the front
    // end's reader (0x2891F0) with one ld at +0 (pointer = low word, size = high word, the same bytes). Both hand the
    // whole chunk out at the first call. False when there is nothing safe to dump (size 0, over kTamanoMaximo or past
    // the end of RAM).
    inline bool leerPunteroTamano(const uint8_t *rdram, uint32_t datos, Trozo &trozo)
    {
        const uint32_t puntero = leer32(rdram, datos) & PS2_RAM_MASK;
        const uint32_t tamano = leer32(rdram, datos + 4u);
        if (tamano == 0u || tamano > kTamanoMaximo || !cabeEnRam(puntero, tamano))
            return false;
        trozo.puntero = puntero;
        trozo.tamano = tamano;
        return true;
    }

    // A flat file name for the dump: the counter (at least 4 digits, so a listing keeps the load order), '_', and the
    // chunk name (lua_load's name argument) without its '@' or '=' marker, every character outside [A-Za-z0-9._-] as
    // '_' (no path separators, so no subfolders and no ".." component), only its last kLargoNombre characters, and no
    // trailing dot (Windows drops it).
    inline std::string nombreArchivo(const std::string &nombre, uint32_t numero)
    {
        size_t inicio = 0;
        while (inicio < nombre.size() && (nombre[inicio] == '@' || nombre[inicio] == '='))
            ++inicio;
        std::string parte;
        for (size_t i = inicio; i < nombre.size(); ++i)
        {
            const char c = nombre[i];
            const bool valido = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                c == '.' || c == '_' || c == '-';
            parte.push_back(valido ? c : '_');
        }
        if (parte.size() > kLargoNombre)
            parte.erase(0, parte.size() - kLargoNombre);
        while (!parte.empty() && parte.back() == '.')
            parte.back() = '_';
        if (parte.empty())
            parte = "sin_nombre";
        std::string contador = std::to_string(numero);
        if (contador.size() < 4u)
            contador.insert(0, 4u - contador.size(), '0');
        return contador + "_" + parte;
    }

    // One block of the block reader (0x29B648: v0 = its buffer, *a2 = bytes, up to 0x4000) appended to the chunk.
    // False, and nothing appended, if the block is past the end of RAM or the chunk would go over kTamanoMaximo.
    inline bool anadirBloque(std::vector<uint8_t> &bytes, const uint8_t *rdram, uint32_t puntero, uint32_t tamano)
    {
        if (!cabeEnRam(puntero, tamano) || tamano > kTamanoMaximo - std::min<size_t>(bytes.size(), kTamanoMaximo))
            return false;
        const uint8_t *inicio = rdram + (puntero & PS2_RAM_MASK);
        bytes.insert(bytes.end(), inicio, inicio + tamano);
        return true;
    }

    // FNV-1a of the chunk: the same chunk loaded again (same name and bytes) is not written twice.
    inline uint64_t huella(const uint8_t *bytes, size_t tamano)
    {
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < tamano; ++i)
            h = (h ^ bytes[i]) * 1099511628211ull;
        return h;
    }
}
