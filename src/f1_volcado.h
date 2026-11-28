// Formula One 05: the diagnostic file dump (F1_VOLCAR_ARCHIVOS, off by default). Header only and without the
// runtime, so ps2xTest (ps2_f1_volcado_tests.cpp) tests the same code the game runs.
//
// It uses the hooks of the .bft capture (f1_letra.h): the open 0x23FBC8 (its path as a string view in a0, the stream
// in v0) and the read and close slots of the five stream classes. Each file whose path contains one of the patterns
// is copied as its stream reads it (the bytes the game gets: after the WAD's decompression), in order, until that
// stream closes; one index line per file. A stream opened again before its close was seen (closed some other way)
// gives the old file up, written as incomplete.
#pragma once

#include "f1_letra.h"
#include "f1_lua_volcado.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace f1::volcado
{
    // A read larger than this is cut (a whole file in one read is fine below it).
    constexpr uint32_t kLecturaMaxima = 16u << 20;
    // Larger files are not followed further (their later reads are dropped and the file is incomplete).
    constexpr size_t kArchivoMaximo = 64u << 20;
    // At most this many dumped files open at once (more are not followed).
    constexpr size_t kAbiertosMaximo = 64u;

    inline char normalizar(char c)
    {
        if (c >= 'A' && c <= 'Z')
            return static_cast<char>(c - 'A' + 'a');
        return c == '\\' ? '/' : c;
    }

    // F1_VOLCAR_ARCHIVOS: substrings separated by ';', without the spaces around them, in lower case and with '\' as
    // '/'; empty ones are dropped.
    inline std::vector<std::string> patrones(std::string_view valor)
    {
        std::vector<std::string> lista;
        size_t inicio = 0;
        while (inicio <= valor.size())
        {
            size_t fin = valor.find(';', inicio);
            if (fin == std::string_view::npos)
                fin = valor.size();
            std::string_view parte = valor.substr(inicio, fin - inicio);
            while (!parte.empty() && (parte.front() == ' ' || parte.front() == '\t'))
                parte.remove_prefix(1);
            while (!parte.empty() && (parte.back() == ' ' || parte.back() == '\t'))
                parte.remove_suffix(1);
            if (!parte.empty())
            {
                std::string p;
                for (const char c : parte)
                    p.push_back(normalizar(c));
                lista.push_back(std::move(p));
            }
            inicio = fin + 1u;
        }
        return lista;
    }

    // The path contains one of the patterns (any case, '\' as '/').
    inline bool coincide(const std::vector<std::string> &lista, std::string_view ruta)
    {
        if (lista.empty() || ruta.empty())
            return false;
        std::string r;
        for (const char c : ruta)
            r.push_back(normalizar(c));
        for (const std::string &p : lista)
            if (r.find(p) != std::string::npos)
                return true;
        return false;
    }

    // The flat file name of the Lua dump (counter, '_', the path with every other character as '_').
    inline std::string nombreArchivo(const std::string &ruta, uint32_t numero)
    {
        return f1::lua_volcado::nombreArchivo(ruta, numero);
    }

    inline std::string cabeceraIndice()
    {
        return "# archivo\truta\tbytes\tcompleto\n";
    }

    inline std::string lineaIndice(const std::string &archivo, const std::string &ruta, size_t bytes, bool completo)
    {
        return archivo + '\t' + ruta + '\t' + std::to_string(bytes) + '\t' + (completo ? "completo" : "incompleto") +
               '\n';
    }

    // The files being dumped, by stream. Only the EE thread uses it.
    struct Volcado
    {
        std::vector<std::string> patrones;
        std::vector<f1::letra::Flujo> abiertos;

        // The game opened `ruta` on `stream` (0: the open failed). The file that was still open on that stream, if
        // any, is given up and returned (incomplete, with its last read copied now); a matching path is followed from
        // now on.
        std::optional<f1::letra::Flujo> abierto(const uint8_t *ram, size_t tamano, const std::string &ruta,
                                                uint32_t stream)
        {
            std::optional<f1::letra::Flujo> abandonado;
            if (stream == 0u)
                return abandonado;
            for (auto it = abiertos.begin(); it != abiertos.end(); ++it)
                if (it->flujo == stream)
                {
                    it->copiarPendiente(ram, tamano);
                    abandonado = std::move(*it);
                    abiertos.erase(it);
                    break;
                }
            if (coincide(patrones, ruta) && abiertos.size() < kAbiertosMaximo)
            {
                f1::letra::Flujo nuevo;
                nuevo.flujo = stream;
                nuevo.nombre = ruta;
                nuevo.limite = kLecturaMaxima;
                abiertos.push_back(std::move(nuevo));
            }
            return abandonado;
        }

        void leer(const uint8_t *ram, size_t tamano, uint32_t stream, uint32_t destino, uint32_t n)
        {
            for (f1::letra::Flujo &f : abiertos)
                if (f.flujo == stream)
                {
                    if (f.bytes.size() + f.pendienteBytes < kArchivoMaximo)
                        f.anotar(ram, tamano, destino, n);
                    return;
                }
        }

        // The stream closed: its file, whole, if it was being dumped.
        std::optional<f1::letra::Flujo> cerrar(const uint8_t *ram, size_t tamano, uint32_t stream)
        {
            for (auto it = abiertos.begin(); it != abiertos.end(); ++it)
                if (it->flujo == stream)
                {
                    it->copiarPendiente(ram, tamano);
                    f1::letra::Flujo cerrado = std::move(*it);
                    abiertos.erase(it);
                    return cerrado;
                }
            return std::nullopt;
        }
    };
}
