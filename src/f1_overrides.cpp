// Game-specific setup for Formula One 05 (SCES-53033 v1.02, PAL).
// compilar-runner.cmd copies this file into ps2xRuntime/src/runner, where it is compiled with the translated code.
#include "game_overrides.h"
#include "ps2_fallo.h"
#include "ps2_heap_reserve.h"
#include "ps2_options_menu.h"
#include "ps2_runtime.h"
#include "ps2_salida.h"
#include "runtime/ps2_ab.h"
#include "Stubs/Pad.h"
#include "../lib/ps2_game_rand.h" // from ps2xRuntime/src/runner, where this file is built
#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <thread>
#include <vector>

// Crash tests (PS2X_PRUEBA_FALLO): f1FalloDePrueba at the end of this file, the other two in f1_fallos.cpp.
void f1FalloDePrueba(ps2x::fallo::Prueba tipo);
void f1FalloDeAcceso();
void f1FalloEnHiloAuxiliar();

namespace
{
    // Seconds since the program started (not since the first pad read, which comes in the middle of the loading).
    const auto g_inicioPrograma = std::chrono::steady_clock::now();

    double segundosDesdeArranque()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_inicioPrograma).count();
    }

    // ---- Scripted presses for tests without a keyboard (ported from F1 06, phase 2 T1) ----
    // F1_PULSAR="105:cruz,115:start:0.5": second since the program started, button and, optionally, how many seconds it
    // is held (0.2 by default). Button names are controles.ini's (select, l3, r3, start, arriba, derecha, abajo,
    // izquierda, l2, r2, l1, r1, triangulo, circulo, cruz, cuadrado); "cruz+abajo" presses several at once.
    // F1 06 wraps the game's scePadRead (0x2E91E0, a game address) and clears the button bits in the 32-byte buffer the
    // game reads. F1 05 does not hook any game function: the presses go in through the runtime's pad, with the test
    // hook ps2x::holdPadButtons (the one of PS2X_MENU_PETICION's mando=...). PSPadBackend::readState adds those held
    // buttons, fully pressed (digital bit and DualShock 2 pressure), to the player's pad on port 0, so the game sees
    // them through the scePadRead HLE like a real pad. A thread keeps the buttons of the presses due held.
    // As in F1 06, each press reaches at least one read even if its time has passed (the game may read the pad only a
    // few times per second while loading): it stays held until the port 0 read counter of the scePadRead HLE has
    // advanced twice since it started (one whole read after the buttons were set).
    // Two presses that share a button do not merge into one long press: the later one waits until the earlier one is
    // over and the read counter has advanced twice since those buttons were let go (one whole read with them up), and its
    // duration counts from when it really starts. An entry whose second is not a number >= 0 is ignored (atof read a typo
    // as t=0).
    // F1 06's "stick-*" entries (left stick to the end) are not supported: holdPadButtons only holds buttons.
    struct PulsacionProgramada
    {
        double segundo;
        double duracion;
        uint16_t bits;
        std::string texto;
        bool empezada = false;
        bool terminada = false;
        uint32_t lecturaInicial = 0;
        double inicioReal = 0.0;
    };

    bool leerSegundo(const std::string &texto, double &segundo)
    {
        if (texto.empty())
            return false;
        char *fin = nullptr;
        segundo = std::strtod(texto.c_str(), &fin);
        return fin == texto.c_str() + texto.size() && std::isfinite(segundo) && segundo >= 0.0;
    }

    uint32_t lecturasPuerto0()
    {
        return ps2_stubs::getPadDebugSnapshot().ports[0][0].readCount;
    }

    std::vector<PulsacionProgramada> cargarPulsaciones()
    {
        std::vector<PulsacionProgramada> pulsaciones;
        const char *valor = std::getenv("F1_PULSAR");
        if (!valor || !*valor)
            return pulsaciones;
        const std::string texto(valor);
        size_t inicio = 0;
        while (inicio < texto.size())
        {
            size_t fin = texto.find(',', inicio);
            if (fin == std::string::npos)
                fin = texto.size();
            const std::string elemento = texto.substr(inicio, fin - inicio);
            inicio = fin + 1;
            const size_t dosPuntos = elemento.find(':');
            uint16_t bits = 0u;
            double duracion = 0.2;
            double segundo = 0.0;
            if (dosPuntos == std::string::npos || !leerSegundo(elemento.substr(0, dosPuntos), segundo) ||
                !ps2x::parseHeldPadButtons(std::string_view(elemento).substr(dosPuntos + 1), bits, duracion))
            {
                std::cout << "[f1-05:pulsar] ignorada: '" << elemento << "'" << std::endl;
                continue;
            }
            pulsaciones.push_back({segundo, duracion, bits, elemento});
        }
        std::sort(pulsaciones.begin(), pulsaciones.end(),
                  [](const PulsacionProgramada &a, const PulsacionProgramada &b)
                  { return a.segundo < b.segundo; });
        std::cout << "[f1-05:pulsar] " << pulsaciones.size() << " pulsaciones programadas" << std::endl;
        return pulsaciones;
    }

    void arrancarPulsaciones()
    {
        std::vector<PulsacionProgramada> pulsaciones = cargarPulsaciones();
        if (pulsaciones.empty())
            return;
        std::thread([pulsaciones = std::move(pulsaciones)]() mutable
                    {
            bool pulsando = false;
            uint16_t bitsAnteriores = 0u;  // held in the previous pass
            uint16_t bitsSueltos = 0u;     // let go, waiting for a whole read with them up
            uint32_t lecturaSuelta = 0u;
            while (true)
            {
                const double ahora = segundosDesdeArranque();
                const uint32_t lecturas = lecturasPuerto0();
                bitsSueltos = static_cast<uint16_t>(bitsSueltos & ~bitsAnteriores);
                if (bitsSueltos != 0u && lecturas - lecturaSuelta >= 2u)
                    bitsSueltos = 0u;
                uint16_t bits = 0u;
                bool quedan = false;
                for (PulsacionProgramada &p : pulsaciones)
                {
                    if (p.terminada)
                        continue;
                    quedan = true;
                    if (ahora < p.segundo)
                        continue;
                    if (!p.empezada)
                    {
                        if ((p.bits & (bitsAnteriores | bitsSueltos | bits)) != 0u)
                            continue; // same button still down or not yet seen up: wait
                        p.empezada = true;
                        p.lecturaInicial = lecturas;
                        p.inicioReal = ahora;
                        std::cout << "[f1-05:pulsar] t=" << ahora << " " << p.texto << " (lectura " << lecturas << ")"
                                  << std::endl;
                    }
                    if (ahora >= p.inicioReal + p.duracion && lecturas - p.lecturaInicial >= 2u)
                    {
                        p.terminada = true;
                        continue;
                    }
                    bits = static_cast<uint16_t>(bits | p.bits);
                }
                if (bits != 0u)
                {
                    ps2x::holdPadButtons(bits, 0.05); // refreshed every 5 ms while due
                    pulsando = true;
                }
                else if (pulsando)
                {
                    ps2x::holdPadButtons(0u, 0.0); // let go now, not 50 ms later
                    pulsando = false;
                }
                const uint16_t soltadas = static_cast<uint16_t>(bitsAnteriores & ~bits);
                if (soltadas != 0u)
                {
                    bitsSueltos = static_cast<uint16_t>(bitsSueltos | soltadas);
                    lecturaSuelta = lecturas;
                }
                bitsAnteriores = bits;
                if (!quedan)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            std::cout << "[f1-05:pulsar] terminadas" << std::endl; })
            .detach();
    }

    // ---- A/B within one run (rendimiento T2a; F1 06's F1_AB without its game hooks) ----
    // Run-to-run variance is about +-4 %, so a change of a few points is measured by alternating it inside one run.
    // F1_AB=<seconds>: windows of that length alternate g_ps2AbMode between 0 (current code) and the modes of
    // F1_AB_MODO (a list, "5,6": the B windows rotate 0, 5, 0, 6, 0, 5...; 999 by default, a mode no code checks, so
    // the default is an A/A run). F1_AB_DESDE=<s>: mode 0 until that second since the program started. F1_AB_FIJO=<mode>:
    // that mode for the whole run, without windows. Off by default (no thread, mode 0).
    // At the end of each window, "[f1-05:ab] t=<s> modo=<mode it ran> ..." with the game's frames per second
    // (g_ps2DisplayFlips: F1 05 changes DISPFB1 and DISPFB2, two per frame) and the EE thread's cycles per frame
    // (QueryThreadCycleTime of GameThread, ps2EeThreadCycles: CPU time per frame, also when the game sits at 50 fps).
    // The switching thread is not the EE thread and hooks no game address. scripts/ab/ab_resumen.py summarises it.
    std::vector<int> modosAb()
    {
        std::vector<int> modos;
        const char *valor = std::getenv("F1_AB_MODO");
        for (const char *c = valor != nullptr ? valor : "999"; *c != '\0';)
        {
            char *fin = nullptr;
            const long modo = std::strtol(c, &fin, 10);
            if (fin == c)
                break;
            modos.push_back(static_cast<int>(modo));
            c = *fin == ',' ? fin + 1 : fin;
        }
        if (modos.empty())
            modos.push_back(999);
        return modos;
    }

    void arrancarAb()
    {
        if (const char *fijo = std::getenv("F1_AB_FIJO"))
        {
            g_ps2AbMode.store(std::atoi(fijo), std::memory_order_relaxed);
            std::cout << "[f1-05:ab] modo fijo " << std::atoi(fijo) << std::endl;
        }
        const char *valor = std::getenv("F1_AB");
        const double ventana = valor != nullptr ? std::atof(valor) : 0.0;
        if (ventana <= 0.0)
            return;
        const char *desdeTexto = std::getenv("F1_AB_DESDE");
        const double desde = desdeTexto != nullptr ? std::atof(desdeTexto) : 0.0;
        const std::vector<int> modosB = modosAb();
        std::cout << "[f1-05:ab] ventanas de " << ventana << " s desde " << desde << " s, modos B:";
        for (int modo : modosB)
            std::cout << ' ' << modo;
        std::cout << std::endl;
        std::thread([ventana, desde, modosB]()
                    {
            size_t siguienteB = 0;
            const auto inicio = std::chrono::steady_clock::now();
            double tAntes = segundosDesdeArranque();
            uint64_t cambiosAntes = g_ps2DisplayFlips.load(std::memory_order_relaxed);
            uint64_t ciclosAntes = ps2EeThreadCycles();
            for (uint64_t n = 1;; ++n)
            {
                std::this_thread::sleep_until(inicio + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                           std::chrono::duration<double>(ventana * static_cast<double>(n))));
                const double t = segundosDesdeArranque();
                const uint64_t cambios = g_ps2DisplayFlips.load(std::memory_order_relaxed);
                const uint64_t ciclos = ps2EeThreadCycles();
                const int modo = g_ps2AbMode.load(std::memory_order_relaxed);
                const int nuevo = modo == 0 && t >= desde ? modosB[siguienteB++ % modosB.size()] : 0;
                g_ps2AbMode.store(nuevo, std::memory_order_relaxed);
                const double segundos = t - tAntes;
                const double imagenes = 0.5 * static_cast<double>(cambios - cambiosAntes);
                const double mciclos = 1e-6 * static_cast<double>(ciclos - ciclosAntes);
                char texto[256];
                std::snprintf(texto, sizeof(texto),
                              "[f1-05:ab] t=%.3f modo=%d seg=%.3f imagenes/s=%.2f mciclos/s=%.1f mciclosFot=%.3f", t,
                              modo, segundos, segundos > 0.0 ? imagenes / segundos : 0.0,
                              segundos > 0.0 ? mciclos / segundos : 0.0, imagenes > 0.0 ? mciclos / imagenes : 0.0);
                std::cout << texto << std::endl;
                tAntes = t;
                cambiosAntes = cambios;
                ciclosAntes = ciclos;
            } })
            .detach();
    }

    // rand() of the game (0x2E2168, patch 0227). The recompiler binds it to the runtime's libc stub (the host's
    // std::rand() & 0x7FFF, which ignores the game's srand() at 0x2E2158); here it is the game's own code again: the C
    // library's LCG on _rand_next of the reent *0x41098C, 31 bits. With 15 bits every random failure threshold falls at
    // the bottom of its range and is the same in every race (F1 06, patch 0173): in Spa all the rivals blew their
    // engines at once (odd/tasks/motores-tras-pausa.md). F1_AZAR_ANFITRION=1 keeps the runtime's stub.
    constexpr uint32_t kRandJuego = 0x002E2168u;
    constexpr uint32_t kImpurePtr = 0x0041098Cu;
    PS2Runtime::RecompiledFunction g_randJuegoOriginal = nullptr;

    void randJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        int32_t valor = 0;
        if (!ps2x::gameRand(rdram, PS2_RAM_SIZE, kImpurePtr, valor))
        {
            g_randJuegoOriginal(rdram, ctx, runtime);
            return;
        }
        SET_GPR_S32(ctx, 2, valor);
        ctx->pc = GPR_U32(ctx, 31);
    }

    void instalarRandJuego(PS2Runtime &runtime)
    {
        if (const char *valor = std::getenv("F1_AZAR_ANFITRION"); valor == nullptr || std::atoi(valor) == 0)
        {
            g_randJuegoOriginal = runtime.lookupFunction(kRandJuego);
            if (g_randJuegoOriginal != nullptr && !runtime.replaceFunction(kRandJuego, randJuego))
                g_randJuegoOriginal = nullptr;
        }
        std::cout << "[f1-05:azar] rand() " << (g_randJuegoOriginal ? "del juego" : "de la biblioteca del runtime")
                  << std::endl;
    }

    // F1_AVERIAS=1: log the random failure thresholds and the failures of every car (odd/tasks/motores-tras-pausa.md
    // T2; addresses mapped from F1 06's beta-1.0 L8/L10 by identical instruction sequences). Per-car physics object
    // (a0): +0x458 failure mask, +0xC38 allowed failures, +0x898 wear, +0x89C scale, +0x8AC gearbox threshold (bit
    // 0x800), +0x8B0 engine threshold (bit 0x1). 0x1712B8 draws the thresholds (a1 = bits, f12 scale, f13 base);
    // 0x1719A0 breaks the engine (it overwrites +0x8B0, so it is logged on entry); 0x171B90 applies failures (a1 =
    // bits). Logging only: the game's functions run unchanged. At most 3000 lines.
    constexpr uint32_t kUmbrales = 0x001712B8u;
    constexpr uint32_t kMotorRoto = 0x001719A0u;
    constexpr uint32_t kAplicarAverias = 0x00171B90u;
    PS2Runtime::RecompiledFunction g_umbralesOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_motorRotoOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_aplicarAveriasOriginal = nullptr;
    int g_lineasAverias = 0;

    float leerFloat(const uint8_t *rdram, uint32_t direccion)
    {
        float valor = 0.0f;
        std::memcpy(&valor, rdram + (direccion & 0x01FFFFFFu), 4);
        return valor;
    }

    uint32_t leerU32(const uint8_t *rdram, uint32_t direccion)
    {
        uint32_t valor = 0u;
        std::memcpy(&valor, rdram + (direccion & 0x01FFFFFFu), 4);
        return valor;
    }

    void informeAveria(const uint8_t *rdram, const char *que, uint32_t fisica, uint32_t ra, uint32_t bits)
    {
        if (g_lineasAverias >= 3000 || fisica == 0u || (fisica & 0x01FFFFFFu) > PS2_RAM_SIZE - 0xD00u)
            return;
        ++g_lineasAverias;
        char texto[320];
        std::snprintf(texto, sizeof(texto),
                      "[f1-05:averias] t=%.3f %s fisica=0x%X ra=0x%X bits=0x%X mascara=0x%X permitidas=0x%X "
                      "desgaste=%.4f escala=%.4f umbralCaja=%.4f umbralMotor=%.4f",
                      segundosDesdeArranque(), que, fisica, ra, bits, leerU32(rdram, fisica + 0x458u),
                      leerU32(rdram, fisica + 0xC38u), leerFloat(rdram, fisica + 0x898u),
                      leerFloat(rdram, fisica + 0x89Cu), leerFloat(rdram, fisica + 0x8ACu),
                      leerFloat(rdram, fisica + 0x8B0u));
        std::cout << texto << std::endl;
    }

    void umbralesChivato(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kUmbrales;
        const uint32_t fisica = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31), bits = GPR_U32(ctx, 5);
        g_umbralesOriginal(rdram, ctx, runtime);
        if (entrada)
            informeAveria(rdram, "umbrales", fisica, ra, bits);
    }

    void motorRotoChivato(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kMotorRoto)
            informeAveria(rdram, "motor roto", GPR_U32(ctx, 4), GPR_U32(ctx, 31), 0u);
        g_motorRotoOriginal(rdram, ctx, runtime);
    }

    void aplicarAveriasChivato(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kAplicarAverias;
        const uint32_t fisica = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31), bits = GPR_U32(ctx, 5);
        g_aplicarAveriasOriginal(rdram, ctx, runtime);
        if (entrada)
            informeAveria(rdram, "averias aplicadas", fisica, ra, bits);
    }

    void instalarChivatosAverias(PS2Runtime &runtime)
    {
        const char *valor = std::getenv("F1_AVERIAS");
        if (valor == nullptr || std::atoi(valor) == 0)
            return;
        auto instalar = [&runtime](uint32_t direccion, PS2Runtime::RecompiledFunction chivato,
                                   PS2Runtime::RecompiledFunction &original)
        {
            original = runtime.lookupFunction(direccion);
            if (original != nullptr && !runtime.replaceFunction(direccion, chivato))
                original = nullptr;
            return original != nullptr;
        };
        const bool u = instalar(kUmbrales, umbralesChivato, g_umbralesOriginal);
        const bool m = instalar(kMotorRoto, motorRotoChivato, g_motorRotoOriginal);
        const bool a = instalar(kAplicarAverias, aplicarAveriasChivato, g_aplicarAveriasOriginal);
        std::cout << "[f1-05:averias] chivatos umbrales=" << u << " motor=" << m << " aplicar=" << a << std::endl;
    }

    // ---- GS dump (F1_VOLCAR_GS=<seconds>[,<seconds>...]; ported from F1 06 for scripts/hacer-icono.ps1, phase 3 T4) ----
    // At each second since the program started: logs/gs_historial-<s>.txt (the GS's last 512 events: draws with their
    // registers and screen rectangle, transfers, registers) and the textures of F1_VOLCAR_TEX. Diagnostics only: it reads
    // the VRAM from another thread without stopping the GS and hooks no game function. Off by default (no thread).
    // F1_VOLCAR_TEX="<spec>[;<spec>...]":
    //   "tbp:tbw:psm:width:height:cbp": that texture, in logs/gs_tex<tbp>-<s>.png.
    //   "dibujo:tbw:psm:width:height": tbp and cbp from the last textured draw of the history with that shape (the
    //     addresses change from one run to another), in logs/gs_tex<tbp>-<s>.png.
    //   "todas": every distinct texture drawn in the history (tbp, tbw, psm, size, cbp), in
    //     logs/gs_tex<tbp>-<cbp>-<psm>-<width>x<height>-<s>.png; the list goes to stderr.
    // psm 0 (CT32) is read as is; psm 19 (T8) and 20 (T4) through their CT32 CLUT in CSM1. Each texture gives <base>.png
    // (colour, alpha 255), <base>-alfa.png (the game's 0-128 alpha doubled, in grey) and, if indexed, <base>-indices.png.
    // F1 05 reuses the same VRAM for several textures within one frame (the menu background tiles are uploaded in two
    // batches over the logo's area), so a read can catch another texture or half of one: each texture (with its CLUT)
    // is read until two reads in a row match (at most 20), and "lecturas=<n>" in the log says how many it took.
    void volcarTexturaGs(PS2Runtime &runtime, unsigned tbp, unsigned tbw, unsigned psm, unsigned ancho, unsigned alto,
                         unsigned cbp, const std::string &base)
    {
        if ((psm != 0u && psm != 19u && psm != 20u) || ancho == 0u || alto == 0u || ancho > 1024u || alto > 1024u)
        {
            std::cerr << "[f1-05:gs] textura no soportada: tbp=" << tbp << " psm=" << psm << " " << ancho << "x" << alto
                      << std::endl;
            return;
        }
        const bool indexada = psm != 0u;
        const unsigned entradas = psm == 19u ? 256u : 16u;
        // Raw words: the CLUT (256 entries, unused ones 0) and then the texels, row by row.
        auto leer = [&]()
        {
            std::vector<uint32_t> crudo(256u + static_cast<size_t>(ancho) * alto, 0u);
            for (unsigned i = 0; indexada && i < entradas; ++i)
            {
                // CSM1, CT32: T8 in 16x16 with bits 3 and 4 of the index swapped; T4 in 8x2.
                unsigned x, y;
                if (psm == 19u)
                {
                    const unsigned j = (i & 0xE7u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1);
                    x = j & 15u;
                    y = j >> 4;
                }
                else
                {
                    x = i & 7u;
                    y = i >> 3;
                }
                crudo[i] = runtime.gs().ReadVram(0u, cbp, 1u, x, y);
            }
            for (unsigned y = 0; y < alto; ++y)
                for (unsigned x = 0; x < ancho; ++x)
                    crudo[256u + static_cast<size_t>(y) * ancho + x] =
                        runtime.gs().ReadVram(psm, tbp, std::max(1u, tbw), x, y);
            return crudo;
        };
        std::vector<uint32_t> crudo = leer();
        int lecturas = 1;
        for (; lecturas < 20; ++lecturas)
        {
            std::vector<uint32_t> otra = leer();
            const bool igual = otra == crudo;
            crudo = std::move(otra);
            if (igual)
                break;
        }
        const uint32_t *clut = crudo.data();
        std::vector<uint8_t> color(static_cast<size_t>(ancho) * alto * 4u), alfa(color.size()), indices(color.size());
        for (unsigned y = 0; y < alto; ++y)
            for (unsigned x = 0; x < ancho; ++x)
            {
                uint32_t c = crudo[256u + static_cast<size_t>(y) * ancho + x];
                uint32_t indice = 0u;
                if (indexada)
                {
                    indice = c & (entradas - 1u);
                    c = clut[indice];
                }
                const size_t o = (static_cast<size_t>(y) * ancho + x) * 4u;
                color[o] = static_cast<uint8_t>(c);
                color[o + 1] = static_cast<uint8_t>(c >> 8);
                color[o + 2] = static_cast<uint8_t>(c >> 16);
                color[o + 3] = 0xFF;
                const uint8_t a = static_cast<uint8_t>(std::min<uint32_t>(255u, (c >> 24) * 2u));
                const uint8_t g = static_cast<uint8_t>(psm == 19u ? indice : indice * 17u);
                alfa[o] = alfa[o + 1] = alfa[o + 2] = a;
                indices[o] = indices[o + 1] = indices[o + 2] = g;
                alfa[o + 3] = indices[o + 3] = 0xFF;
            }
        Image imagen{color.data(), static_cast<int>(ancho), static_cast<int>(alto), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(imagen, (base + ".png").c_str());
        imagen.data = alfa.data();
        ExportImage(imagen, (base + "-alfa.png").c_str());
        if (indexada)
        {
            imagen.data = indices.data();
            ExportImage(imagen, (base + "-indices.png").c_str());
        }
        std::cerr << "[f1-05:gs] textura tbp=" << tbp << " tbw=" << tbw << " psm=" << psm << " " << ancho << "x" << alto
                  << " cbp=" << cbp << " lecturas=" << (lecturas < 20 ? lecturas + 1 : 20) << " en " << base << ".png"
                  << std::endl;
    }

    void volcarTexturasGs(PS2Runtime &runtime, const std::vector<GSDebugHistoryEntry> &historial, const std::string &spec,
                          const std::string &sufijo)
    {
        unsigned tbp = 0, tbw = 0, psm = 0, ancho = 0, alto = 0, cbp = 0;
        if (spec == "todas")
        {
            std::set<std::tuple<unsigned, unsigned, unsigned, unsigned, unsigned, unsigned>> vistas;
            for (const GSDebugHistoryEntry &e : historial)
            {
                if (e.kind != GSDebugEventKind::Draw || !e.prim.tme)
                    continue;
                const unsigned p = e.tex0.psm;
                const auto clave = std::make_tuple(unsigned(e.tex0.tbp0), unsigned(e.tex0.tbw), p, 1u << e.tex0.tw,
                                                   1u << e.tex0.th, p == 0u ? 0u : unsigned(e.tex0.cbp));
                if (!vistas.insert(clave).second)
                    continue;
                std::cerr << "[f1-05:gs] dibujo con textura tbp=" << std::get<0>(clave) << " tbw=" << std::get<1>(clave)
                          << " psm=" << p << " " << std::get<3>(clave) << "x" << std::get<4>(clave)
                          << " cbp=" << std::get<5>(clave) << " pantalla x=[" << e.xMin << "," << e.xMax << "] y=["
                          << e.yMin << "," << e.yMax << "]" << std::endl;
                volcarTexturaGs(runtime, std::get<0>(clave), std::get<1>(clave), p, std::get<3>(clave),
                                std::get<4>(clave), std::get<5>(clave),
                                "logs/gs_tex" + std::to_string(std::get<0>(clave)) + "-" +
                                    std::to_string(std::get<5>(clave)) + "-" + std::to_string(p) + "-" +
                                    std::to_string(std::get<3>(clave)) + "x" + std::to_string(std::get<4>(clave)) +
                                    sufijo);
            }
            return;
        }
        bool leida = false;
        if (spec.rfind("dibujo:", 0) == 0)
        {
            if (std::sscanf(spec.c_str() + 7, "%u:%u:%u:%u", &tbw, &psm, &ancho, &alto) == 4)
                for (const GSDebugHistoryEntry &e : historial)
                    if (e.kind == GSDebugEventKind::Draw && e.prim.tme && e.tex0.tbw == tbw && e.tex0.psm == psm &&
                        (1u << e.tex0.tw) == ancho && (1u << e.tex0.th) == alto)
                    {
                        tbp = e.tex0.tbp0;
                        cbp = e.tex0.cbp;
                        leida = true;
                    }
            if (!leida)
            {
                std::cerr << "[f1-05:gs] F1_VOLCAR_TEX: ningun dibujo con \"" << spec << "\" en el historial" << std::endl;
                return;
            }
        }
        else
        {
            leida = std::sscanf(spec.c_str(), "%u:%u:%u:%u:%u:%u", &tbp, &tbw, &psm, &ancho, &alto, &cbp) == 6;
        }
        if (!leida)
        {
            std::cerr << "[f1-05:gs] F1_VOLCAR_TEX: no entiendo \"" << spec << "\"" << std::endl;
            return;
        }
        volcarTexturaGs(runtime, tbp, tbw, psm, ancho, alto, cbp, "logs/gs_tex" + std::to_string(tbp) + sufijo);
    }

    void volcarHistorialGs(const std::vector<GSDebugHistoryEntry> &historial, const std::string &ruta)
    {
        static const char *kTipos[] = {"giftag", "reg", "DIBUJO", "transfer", "present"};
        static const char *kPrims[] = {"punto", "linea", "tiralineas", "tri", "tiratri", "abanico", "sprite", "?"};
        std::ofstream out(ruta);
        for (const GSDebugHistoryEntry &e : historial)
        {
            const int tipo = std::min<int>(static_cast<int>(e.kind), 4);
            out << e.seq << " f=" << e.frameIndex << " vsync=" << e.vsyncTick << " " << kTipos[tipo];
            switch (e.kind)
            {
            case GSDebugEventKind::Draw:
                out << " " << kPrims[e.prim.type & 7] << " tme=" << e.prim.tme << " abe=" << e.prim.abe
                    << " fst=" << e.prim.fst << " ctxt=" << e.prim.ctxt << " fbp=" << e.frame.fbp << " fbw=" << e.frame.fbw
                    << " psm=" << int(e.frame.psm) << " tbp=" << e.tex0.tbp0 << " tbw=" << int(e.tex0.tbw)
                    << " tpsm=" << int(e.tex0.psm) << " tw=" << int(e.tex0.tw) << " th=" << int(e.tex0.th)
                    << " cbp=" << e.tex0.cbp << " tcc=" << int(e.tex0.tcc) << " alpha=0x" << std::hex << e.alpha
                    << std::dec << " v=" << e.vertexCount << " x=[" << e.xMin << "," << e.xMax << "] y=[" << e.yMin
                    << "," << e.yMax << "]";
                break;
            case GSDebugEventKind::Transfer:
                out << " dbp=" << e.bitbltbuf.dbp << " dbw=" << int(e.bitbltbuf.dbw) << " dpsm=" << int(e.bitbltbuf.dpsm)
                    << " pos=(" << e.trxpos.dsax << "," << e.trxpos.dsay << ") tam=" << e.trxreg.rrw << "x"
                    << e.trxreg.rrh << " dir=" << e.trxdir << " pix=" << e.transferPixels;
                break;
            case GSDebugEventKind::Register:
                out << " reg=0x" << std::hex << int(e.reg) << " val=0x" << e.regValue << std::dec;
                break;
            case GSDebugEventKind::GifTag:
                out << " nloop=" << e.gifNloop << " flg=" << int(e.gifFlg) << " nreg=" << int(e.gifNreg)
                    << " bytes=" << e.gifSizeBytes;
                break;
            default:
                out << " displayFbp=" << e.displayFbp << " sourceFbp=" << e.sourceFbp;
                break;
            }
            out << "\n";
        }
    }

    void arrancarVolcadoGs(PS2Runtime &runtime)
    {
        const char *valor = std::getenv("F1_VOLCAR_GS");
        if (valor == nullptr || *valor == '\0')
            return;
        std::vector<int> segundos;
        for (const char *p = valor; *p;)
        {
            segundos.push_back(std::max(1, std::atoi(p)));
            while (*p && *p != ',')
                ++p;
            if (*p == ',')
                ++p;
        }
        std::sort(segundos.begin(), segundos.end());
        const char *texturas = std::getenv("F1_VOLCAR_TEX");
        const std::string specs = texturas != nullptr ? texturas : "";
        std::thread([&runtime, segundos, specs]()
                    {
            for (const int s : segundos)
            {
                while (segundosDesdeArranque() < s)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const std::string sufijo = "-" + std::to_string(s);
                runtime.gs().setDebugHistoryPaused(true);
                const std::vector<GSDebugHistoryEntry> historial = runtime.gs().getDebugHistory();
                runtime.gs().setDebugHistoryPaused(false);
                volcarHistorialGs(historial, "logs/gs_historial" + sufijo + ".txt");
                std::stringstream flujo(specs);
                for (std::string spec; std::getline(flujo, spec, ';');)
                    if (!spec.empty())
                        volcarTexturasGs(runtime, historial, spec, sufijo);
                std::cerr << "[f1-05:gs] GS volcado (" << s << " s) en logs/gs_historial" << sufijo << ".txt" << std::endl;
            } })
            .detach();
    }

    // ---- Crash reports with a non-zero exit code (fase 5 T1, F1 06's fallos-con-log T2/T3b, odd/tasks/paquete.md S3) ----
    // exit() of the C library (newlib): runs the atexit handlers and calls _exit (0x101570: Exit syscall 0x23), which
    // ends the main thread; the other threads keep waiting and the screen freezes. Found by matching F1 06's exit
    // (0x102038): its 38 instructions, with jump targets and immediates masked, match only at 0x101680 in F1 05's ELF,
    // and the unmasked words differ only in _impure_ptr (lui 0x41 / lw 0x98C = 0x41098C, the reent of kImpurePtr) and
    // the jal to _exit. The game's own calls (jal at 0x29F77C, 0x2AEF74 and 0x382C38) reach it through the function
    // table; the tail jump of the entry point (0x101568, after main returns) calls it directly and is not
    // seen here. The process then exits with 11 (ps2_salida.h) when the window is closed, so RecompLauncher attaches
    // the log.
    constexpr uint32_t kExit = 0x00101680u;
    PS2Runtime::RecompiledFunction g_exitOriginal = nullptr;

    void trazarExit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc == kExit)
        {
            std::cout << "[f1-05:exit] t=" << segundosDesdeArranque() << " codigo="
                      << static_cast<int32_t>(GPR_U32(ctx, 4)) << " ra=0x" << std::hex << GPR_U32(ctx, 31)
                      << " sp=0x" << GPR_U32(ctx, 29) << std::dec << std::endl;
            ps2x::salida::anotarFallo(ps2x::salida::kExitDelJuego, "exit-del-juego");
        }
        g_exitOriginal(rdram, ctx, runtime);
    }

    // PS2X_PRUEBA_FALLO=<acceso|excepcion|terminate|exit>:<seconds> (ps2_fallo.h, only to test the reports): that crash
    // once, at <seconds> since the program started, on the game thread. F1 06 triggers it in its frame render function;
    // F1 05 hooks no per-frame game function, so it goes in the game's scePadRead (0x2EB168, config/recomp.toml; the
    // game reads the pad on every frame, also in the menus), and only with the variable set: without it nothing is
    // hooked. exit: the game calls its exit(1) instead of that pad read. lua is not supported: F1 06's trigger raises a
    // Lua error from a native of its Lua front end (end_frame, luaB_error), and F1 05's equivalents are not mapped.
    constexpr uint32_t kScePadRead = 0x002EB168u;
    PS2Runtime::RecompiledFunction g_scePadReadOriginal = nullptr;
    ps2x::fallo::PruebaFallo g_pruebaFallo; // read and cleared only on the game thread

    void exitDePrueba(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cout << "[f1-05:exit] prueba: exit(1) del juego a proposito (PS2X_PRUEBA_FALLO=exit) t="
                  << segundosDesdeArranque() << std::endl;
        SET_GPR_U32(ctx, 4, 1u);
        ctx->pc = kExit;
        runtime->lookupFunction(kExit)(rdram, ctx, runtime);
    }

    void scePadReadConPrueba(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (g_pruebaFallo.tipo != ps2x::fallo::Prueba::Ninguna && segundosDesdeArranque() >= g_pruebaFallo.segundos)
        {
            const ps2x::fallo::Prueba tipo = g_pruebaFallo.tipo;
            g_pruebaFallo.tipo = ps2x::fallo::Prueba::Ninguna;
            if (tipo == ps2x::fallo::Prueba::Exit)
            {
                exitDePrueba(rdram, ctx, runtime);
                return;
            }
            f1FalloDePrueba(tipo);
        }
        g_scePadReadOriginal(rdram, ctx, runtime);
    }

    void instalarFallos(PS2Runtime &runtime)
    {
        g_exitOriginal = runtime.lookupFunction(kExit);
        if (g_exitOriginal != nullptr && !runtime.replaceFunction(kExit, trazarExit))
            g_exitOriginal = nullptr;
        if (g_exitOriginal == nullptr)
            std::cout << "[f1-05:exit] no se puede enganchar exit() (0x101680)" << std::endl;

        const ps2x::fallo::PruebaFallo prueba = ps2x::fallo::leerPrueba(std::getenv("PS2X_PRUEBA_FALLO"));
        if (prueba.tipo == ps2x::fallo::Prueba::Ninguna)
            return;
        if (prueba.tipo == ps2x::fallo::Prueba::Lua)
        {
            std::cout << "[fallo] prueba: PS2X_PRUEBA_FALLO=lua no existe en F1 05 (sin hacer nada)" << std::endl;
            return;
        }
        g_scePadReadOriginal = runtime.lookupFunction(kScePadRead);
        if (g_scePadReadOriginal != nullptr && runtime.replaceFunction(kScePadRead, scePadReadConPrueba))
            g_pruebaFallo = prueba;
        else
            g_scePadReadOriginal = nullptr;
        std::cout << "[fallo] prueba: PS2X_PRUEBA_FALLO a los " << prueba.segundos << " s "
                  << (g_scePadReadOriginal != nullptr ? "preparada (en scePadRead)" : "sin enganche") << std::endl;
    }

    // "[salida] partida:" (ps2x::salida::registrarPartida) is not registered: F1 05 has no evidence-backed source for
    // menu/race (ps2x::raceOnScreen needs noteRaceCameraFrame, which only F1 06's hooks call) nor for the circuit, so
    // the runtime default (no partida line) is kept rather than a wrong "estado=menu" during a race.

    void applyFormulaOne05(PS2Runtime &runtime)
    {
        // Runtime memory (same layout as F1 06, patches 0002 and 0116): the game manages all RAM from _end with its own
        // newlib sbrk, so the runtime's internal heap and invocation stacks must not live there. Before this, the
        // runtime's guestMalloc handed out blocks inside the game's heap manager (header at 0x4D12C0): word +4, which
        // the game expects to be 0, got 0xBD800 and the first malloc jumped to address 0 (JALR at 0x24403C).
        // They go to the kernel RAM, which the game does not use: 0x80000-0x100000 and 0x20000-0x70000.
        ps2_syscalls::setRuntimeLowHeap(0x00080000u, 0x00088000u);
        ps2_syscalls::addRuntimeLowStackArea(0x00088000u, 0x00100000u);
        ps2_syscalls::addRuntimeLowStackArea(0x00020000u, 0x00070000u);
        instalarRandJuego(runtime);
        instalarFallos(runtime);
        instalarChivatosAverias(runtime);
        arrancarPulsaciones();
        arrancarAb();
        arrancarVolcadoGs(runtime);
    }
}

// The crash of PS2X_PRUEBA_FALLO (ps2_fallo.h). excepcion: std::runtime_error on the game thread (it reaches
// GameThread's catch: "[excepcion]" report, exits with 10); terminate: std::runtime_error on a helper thread
// "PruebaFallo" (nobody catches it: std::terminate, "[terminate]" report and abort); acceso and the thread:
// f1_fallos.cpp. exit goes in scePadReadConPrueba, with the EE context.
__declspec(noinline) void f1FalloDePrueba(ps2x::fallo::Prueba tipo)
{
    std::cout << "[fallo] prueba: PS2X_PRUEBA_FALLO t=" << segundosDesdeArranque() << std::endl;
    switch (tipo)
    {
    case ps2x::fallo::Prueba::Acceso:
        f1FalloDeAcceso();
        break;
    case ps2x::fallo::Prueba::Excepcion:
        throw std::runtime_error("prueba: excepcion en el hilo del juego (PS2X_PRUEBA_FALLO=excepcion)");
    case ps2x::fallo::Prueba::Terminate:
        f1FalloEnHiloAuxiliar();
        break;
    case ps2x::fallo::Prueba::Lua:
    case ps2x::fallo::Prueba::Exit:
    case ps2x::fallo::Prueba::Ninguna:
        break;
    }
}

PS2_REGISTER_GAME_OVERRIDE("Formula One 05 (SCES-53033)", "SCES_530.33", 0x00101330u, 0u, applyFormulaOne05)
