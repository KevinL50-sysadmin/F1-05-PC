// Formula One 05: the game's letters for the runtime's FPS counter (odd/tasks/opciones-pc.md T9). Header only and
// without the runtime, so ps2xTest (ps2_f1_letra_tests.cpp) tests the same code the game runs.
//
// F1 06 copies each .bft as its reader func_264888 reads it (ps2x::storeGameFontFile, ps2_game_font.h); F1 05 has the
// same reader at 0x2D2DE8 (font, name): it opens "<name>.bft" with 0x23FBC8 (the name as a string view, one 64-bit
// register: low word the address, high word the length; the stream object in v0) and reads it through the stream's
// virtual read, slot +0xC of the table at stream+0x10 (this, destination, bytes); slot +0x34 closes it. The WAD
// stream's table is at 0x433168: read 0x26ACD0 (F1 06's 0x27A0B8), close 0x26AF48 (F1 06's 0x27A340). The other
// stream classes with the same table shape are hooked as well (kLecturas, kCierres): only the reads and the close of
// the font's own stream are copied.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace f1::letra
{
    constexpr uint32_t kLeerLetra = 0x002D2DE8u;
    constexpr uint32_t kAbrirArchivo = 0x0023FBC8u;
    // The stream classes' virtual reads and closes (slots +0xC and +0x34 of the tables at 0x432710, 0x432750,
    // 0x433168, 0x433ED8 and 0x433F70; 0x432880's are pure virtual).
    inline constexpr uint32_t kLecturas[] = {0x0023E2C8u, 0x0023E858u, 0x0026ACD0u, 0x0026F200u, 0x00271CB0u};
    inline constexpr uint32_t kCierres[] = {0x0023DFF0u, 0x0023E648u, 0x0026AF48u, 0x0026F178u, 0x00271BE8u};
    // No .bft read is larger (F1 06's limit).
    constexpr uint32_t kLecturaMaxima = 1u << 20;
    constexpr size_t kLargoMaximo = 255u;

    // A .bft by its name (any case), the only files the capture follows.
    inline bool esLetra(std::string_view nombre)
    {
        if (nombre.size() <= 4u)
            return false;
        const std::string_view fin = nombre.substr(nombre.size() - 4u);
        auto igual = [](char a, char b) { return (a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) == b; };
        return igual(fin[0], '.') && igual(fin[1], 'b') && igual(fin[2], 'f') && igual(fin[3], 't');
    }

    // The text of a string view register (low word the address, high word the length) in a RAM of `tamano` bytes (a
    // power of two; the address is masked with tamano - 1), up to a NUL; empty if it is too long or past the end.
    inline std::string nombreDeVista(const uint8_t *ram, size_t tamano, uint64_t vista)
    {
        const size_t direccion = static_cast<uint32_t>(vista) & (tamano - 1u);
        const size_t largo = static_cast<size_t>(vista >> 32);
        if (ram == nullptr || largo == 0u || largo > kLargoMaximo || direccion > tamano - largo)
            return {};
        std::string nombre;
        for (size_t i = 0; i < largo && ram[direccion + i] != 0u; ++i)
            nombre.push_back(static_cast<char>(ram[direccion + i]));
        return nombre;
    }

    // The stream an open returned (v0), or 0 when the open came back half way (at a check point: pc is not the return
    // address and v0 is not its result yet; odd/tasks/opciones-pc.md L15, A1).
    inline uint32_t flujoAbierto(uint32_t pc, uint32_t ra, uint32_t v0)
    {
        return pc == ra ? v0 : 0u;
    }

    // One stream's reads, in order (the .bft capture below and the file dump, f1_volcado.h). The translated code may
    // come back from a call half way (at a check point) and go on later without passing through the hook, so each
    // read is copied when the next one starts (it cannot start before the last one ends) and the last one at the
    // close, as F1 06 does. Each read is capped at `limite` bytes. Only the EE thread uses it.
    struct Flujo
    {
        uint32_t flujo = 0u;
        std::string nombre;
        std::vector<uint8_t> bytes;
        uint32_t pendienteDestino = 0u, pendienteBytes = 0u;
        uint32_t limite = kLecturaMaxima;

        void copiarPendiente(const uint8_t *ram, size_t tamano)
        {
            for (uint32_t i = 0; i < pendienteBytes; ++i)
                bytes.push_back(ram[(pendienteDestino + i) & (tamano - 1u)]);
            pendienteBytes = 0u;
        }

        // A read of this stream: the previous one is copied now, this one at the next read or the close.
        void anotar(const uint8_t *ram, size_t tamano, uint32_t destino, uint32_t n)
        {
            copiarPendiente(ram, tamano);
            pendienteDestino = destino;
            pendienteBytes = n < limite ? n : limite;
        }
    };

    // One .bft being read: from the reader's call (empezar) to its stream's close (cerrar). A capture whose reader
    // came back (acabar: its normal return) without that close was abandoned (the open failed, or the file was closed
    // some other way): it ends at the next open (odd/tasks/opciones-pc.md L15, A2), so no later .bft opened outside
    // the reader is followed.
    struct Captura : Flujo
    {
        bool activa = false;
        bool lectorAcabado = false;

        void empezar()
        {
            *this = Captura{};
            activa = true;
        }

        // The reader came back to its caller.
        void acabar()
        {
            if (activa)
                lectorAcabado = true;
        }

        // The file the game opened (and its stream, 0 if it failed): the first .bft after empezar.
        void abierto(const std::string &archivo, uint32_t stream)
        {
            if (activa && lectorAcabado)
            {
                *this = Captura{};
                return;
            }
            if (activa && flujo == 0u && stream != 0u && esLetra(archivo))
            {
                flujo = stream;
                nombre = archivo;
            }
        }

        void leer(const uint8_t *ram, size_t tamano, uint32_t stream, uint32_t destino, uint32_t n)
        {
            if (!activa || flujo == 0u || stream != flujo)
                return;
            anotar(ram, tamano, destino, n);
        }

        // True when the font's stream closes: bytes and nombre hold the whole file.
        bool cerrar(const uint8_t *ram, size_t tamano, uint32_t stream)
        {
            if (!activa || flujo == 0u || stream != flujo)
                return false;
            copiarPendiente(ram, tamano);
            activa = false;
            return true;
        }
    };
}
