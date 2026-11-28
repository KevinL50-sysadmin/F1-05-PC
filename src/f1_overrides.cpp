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
#include "../../../../../src/f1_panoramica.h" // the project's src/, seen from ps2xRuntime/src/runner (ps2xTest tests it)
#include "../../../../../src/f1_lua_volcado.h" // the same, for the Lua chunk dump
#include "../../../../../src/f1_opciones_juego.h" // the same, for the PC options in the game's menus
#include "../../../../../src/f1_letra.h" // the same, for the game's letters (the FPS counter)
#include "../../../../../src/f1_volcado.h" // the same, for the file dump (F1_VOLCAR_ARCHIVOS)
#include "../../../../../src/f1_campo_vision.h" // the same, for the race cameras' field of view (fov)
#include "../lib/ps2_game_font.h" // from ps2xRuntime/src/runner, where this file is built
#include "ps2_display.h"
#include "ps2_options.h"
#include "raylib.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <thread>
#include <utility>
#include <vector>

// Crash tests (PS2X_PRUEBA_FALLO): f1FalloDePrueba at the end of this file, the other two in f1_fallos.cpp.
void f1FalloDePrueba(ps2x::fallo::Prueba tipo);
void f1FalloDeAcceso();
void f1FalloEnHiloAuxiliar();
// ps2_runtime.cpp (patch 0103): the game's widescreen option (1 = 16:9 anamorphic, 0 = 4:3) for the presenter.
void ps2SetGameWidescreen(int on);

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

    // ---- Lua chunk dump (F1_LUA_VOLCAR=<dir>, odd/tasks/opciones-pc.md T1; ported from F1 06's) ----
    // Diagnostics only, to read the menus' Lua: without the variable nothing is dumped (the PC options below share the
    // lua_load and heap hooks).
    // lua_load (0x29DEF8(L, reader, data, name); a NULL name is "?") is wrapped, and each chunk it loads is written to
    // <dir>/<counter>_<name> (f1_lua_volcado.h: nombreArchivo) with one line "[f1-05:lua] volcado <name> <size> bytes
    // -> <file>" and one row in <dir>/indice.txt. The same name with the same bytes again is not written twice
    // ("[f1-05:lua] repetido ..."). The bytes are read from the guest RAM; no game code is called but the originals.
    // - 0x2891F0 (front-end scripts, its loader 0x288C20) and getS 0x2AE4F8 (luaL_loadbuffer): data {pointer, size},
    //   the whole chunk at the first call, so it is read before lua_load runs.
    // - 0x29B648 (block stream, its loader 0x29BAE0): data is a C++ stream object; through its vtable (+0x10; entries
    //   {this offset, function} at +0x20 and +0x8) it asks for the bytes left and reads up to 0x4000 of them into the
    //   buffer *0x41BC48, which it returns (v0) with the size in *a2. The reader itself is wrapped too: after each of its calls that returns normally for the chunk being loaded,
    //   the block is appended; the chunk is written when lua_load returns. A call resumed in the middle (a block the
    //   wrapper did not see whole) marks the chunk "incompleto" in the log and the index.
    // - Any other reader: "[f1-05:lua] <name>: lector 0x... sin volcar" (at most 50 lines).
    // Lua heap: 0x29B798(size, 4) destroys and recreates the active heap (*0x41BC54); realloc_lua_heap (0x131A98) calls
    // it. Each call is logged, "[f1-05:lua-monton] t=<s> tamano=0x<a0> (<a0> bytes) a1=<a1> ra=0x<ra>", and then runs
    // unchanged, unless the PC options are on (luaMontonJuego: the menus' heap is made bigger and the size granted is
    // logged too). The lua_load and heap hooks are shared with the PC options (instalarLua).
    // scripts/lua_volcados.py reads the dumped chunks (functions, constants, source names and lines).
    constexpr uint32_t kLuaLoad = 0x0029DEF8u;
    constexpr uint32_t kLuaLoadReanudar[] = {0x0029DF34u, 0x0029DF44u}; // lua_load's resume points (register_functions)
    constexpr uint32_t kLuaLectorFrontEnd = 0x002891F0u;
    constexpr uint32_t kLuaLectorGetS = 0x002AE4F8u;
    constexpr uint32_t kLuaLectorBloques = 0x0029B648u;
    constexpr uint32_t kLuaMonton = 0x0029B798u;
    PS2Runtime::RecompiledFunction g_luaLoadOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_luaLectorBloquesOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_luaMontonOriginal = nullptr;
    std::mutex g_luaVolcadoCerrojo; // guards everything below (only held while no game code runs)
    std::filesystem::path g_luaVolcado;
    uint32_t g_luaArchivos = 0;                                         // files written
    std::map<std::pair<std::string, uint64_t>, std::string> g_luaVistos; // (name, huella) -> file
    uint32_t g_luaSinVolcar = 0;

    // The block stream chunk being loaded (reader 0x29B648).
    struct TrozoPorBloques
    {
        bool activo = false;
        uint32_t datos = 0;
        std::string nombre;
        std::vector<uint8_t> bytes;
        bool incompleto = false;
    };
    TrozoPorBloques g_luaBloques;

    // The PC options (below, servirOpcionesJuego): on unless F1_OPCIONES_JUEGO=0. The chunk getS is serving in place of
    // the game's: the data of its lua_load (the key) and what is left.
    bool g_opcionesJuego = false;
    int g_opcionesRotas = 0; // F1_OPCIONES_ROTAS (f1_opciones_juego.h: pruebaRota), only to test the fallbacks
    struct TrozoServido
    {
        uint32_t datos = 0u;
        std::vector<uint8_t> bytes;
        size_t servidos = 0u;
    };
    TrozoServido g_trozoServido; // only on the game thread

    std::string leerCadenaLua(const uint8_t *rdram, uint32_t direccion, size_t maximo)
    {
        std::string texto;
        for (size_t i = 0; i < maximo; ++i)
        {
            const char c = static_cast<char>(rdram[(direccion + i) & PS2_RAM_MASK]);
            if (c == '\0')
                break;
            texto.push_back(c >= 0x20 && c < 0x7F ? c : '.');
        }
        return texto;
    }

    // With g_luaVolcadoCerrojo held.
    void guardarTrozoLua(const std::string &nombre, const uint8_t *bytes, size_t tamano, uint32_t lector, bool completo)
    {
        const auto clave = std::make_pair(nombre, f1::lua_volcado::huella(bytes, tamano));
        if (const auto visto = g_luaVistos.find(clave); visto != g_luaVistos.end())
        {
            std::cout << "[f1-05:lua] repetido " << nombre << " " << tamano << " bytes (= " << visto->second << ")"
                      << std::endl;
            return;
        }
        const std::string archivo = f1::lua_volcado::nombreArchivo(nombre, ++g_luaArchivos);
        g_luaVistos.emplace(clave, archivo);
        std::ofstream(g_luaVolcado / archivo, std::ios::binary)
            .write(reinterpret_cast<const char *>(bytes), static_cast<std::streamsize>(tamano));
        const char *formato = tamano != 0u && bytes[0] == 0x1Bu ? "bytecode" : "texto";
        std::ofstream(g_luaVolcado / "indice.txt", std::ios::app)
            << archivo << '\t' << nombre << '\t' << tamano << '\t' << formato << "\t0x" << std::hex << lector << std::dec
            << '\t' << (completo ? "completo" : "incompleto") << '\n';
        std::cout << "[f1-05:lua] volcado " << nombre << " " << tamano << " bytes -> " << archivo
                  << (completo ? "" : " (incompleto)") << std::endl;
    }

    // With g_luaVolcadoCerrojo held: writes the block stream chunk, if any.
    void cerrarTrozoPorBloques()
    {
        if (!g_luaBloques.activo)
            return;
        if (g_luaBloques.bytes.empty())
            std::cout << "[f1-05:lua] " << g_luaBloques.nombre << ": lector 0x29b648 sin bytes vistos" << std::endl;
        else
            guardarTrozoLua(g_luaBloques.nombre, g_luaBloques.bytes.data(), g_luaBloques.bytes.size(), kLuaLectorBloques,
                            !g_luaBloques.incompleto);
        g_luaBloques = {};
    }

    // What servirOpcionesJuego served in place of the game's chunk.
    enum class Servido
    {
        Nada,        // the game's own reader and data
        Extension,   // the text with the original and the extension
        Original,    // a =ps2x: load: the kept bytecode
        SinOriginal, // a =ps2x: load with no kept bytecode: f1_opciones_juego.h's textoSinOriginal
        Pagina,      // a =ps2x: load of a PC page's body (f1_opciones_juego.h's kPaginas)
    };
    Servido servirOpcionesJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const std::string &nombre);

    // A load of servirOpcionesJuego's until lua_load returns to ra (odd/tasks/opciones-pc.md T5, R4-001/R4-002 of L8):
    // its status (v0) is checked there, also when it returns through a resumed call. A served text that does not
    // compile is loaded again with the game's own reader and data (the original bytecode, so the stock menu still
    // works) after popping its error object; a =ps2x: load that fails only logs it (loadstring returns nil and the
    // error, and the served text skips the original). The Lua error message (a string at L->top - 8) is logged.
    struct CargaPendiente
    {
        bool activo = false;
        Servido servido = Servido::Nada;
        bool repetida = false;
        uint32_t L = 0u, lector = 0u, datos = 0u, nombre = 0u, ra = 0u;
        std::string texto; // the chunk's name
    };
    CargaPendiente g_cargaPendiente; // only on the game thread

    void terminarCarga(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        while (g_cargaPendiente.activo && ctx->pc == g_cargaPendiente.ra)
        {
            CargaPendiente &c = g_cargaPendiente;
            const uint32_t estado = GPR_U32(ctx, 2);
            if (estado == 0u)
            {
                if (c.repetida)
                    std::cout << "[f1-05:opciones] " << c.texto << ": cargado el original, sin las opciones del PC"
                              << std::endl;
                c.activo = false;
                break;
            }
            std::string mensaje;
            if (!f1::opciones_juego::mensajeDeError(rdram, c.L, mensaje))
                mensaje = "(sin mensaje de Lua)";
            mensaje += " (estado " + std::to_string(estado) + ")";
            if (c.servido == Servido::Extension && !c.repetida)
            {
                std::cout << f1::opciones_juego::lineaExtensionNoCarga(c.texto, mensaje) << std::endl;
                f1::opciones_juego::quitarCima(rdram, c.L);
                g_trozoServido = {};
                c.repetida = true;
                SET_GPR_U32(ctx, 4, c.L);
                SET_GPR_U32(ctx, 5, c.lector);
                SET_GPR_U32(ctx, 6, c.datos);
                SET_GPR_U32(ctx, 7, c.nombre);
                SET_GPR_U32(ctx, 31, c.ra);
                ctx->pc = kLuaLoad;
                g_luaLoadOriginal(rdram, ctx, runtime);
                continue; // checked again if it returned now; otherwise when its resumed call does
            }
            std::cout << f1::opciones_juego::lineaNoCarga(c.repetida ? c.texto + " (el original)" : c.texto, mensaje)
                      << std::endl;
            c.activo = false;
        }
    }

    // lua_load: the dump (only with F1_LUA_VOLCAR) sees the game's own reader and data, so it always writes the
    // original bytecode (the =ps2x: loads of the served texts are not dumped); then the PC options
    // (servirOpcionesJuego) may swap the reader for getS.
    void luaLoadJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kLuaLoad) // resumed in the middle: its chunk was handled at the entry
        {
            g_luaLoadOriginal(rdram, ctx, runtime);
            terminarCarga(rdram, ctx, runtime);
            return;
        }
        const uint32_t lector = GPR_U32(ctx, 5), datos = GPR_U32(ctx, 6), ra = GPR_U32(ctx, 31);
        const std::string nombre = GPR_U32(ctx, 7) != 0u ? leerCadenaLua(rdram, GPR_U32(ctx, 7), 256) : "?";
        std::string claveOriginal;
        const bool volcando = !g_luaVolcado.empty() && !f1::opciones_juego::nombreOriginal(nombre, claveOriginal);
        if (volcando)
        {
            std::lock_guard<std::mutex> cerrojo(g_luaVolcadoCerrojo);
            cerrarTrozoPorBloques(); // one left open by a lua_load that did not return normally
            if (lector == kLuaLectorFrontEnd || lector == kLuaLectorGetS)
            {
                f1::lua_volcado::Trozo trozo;
                if (f1::lua_volcado::leerPunteroTamano(rdram, datos, trozo))
                    guardarTrozoLua(nombre, rdram + trozo.puntero, trozo.tamano, lector, true);
                else
                    std::cout << "[f1-05:lua] " << nombre << ": sin datos que volcar (lector 0x" << std::hex << lector
                              << std::dec << ")" << std::endl;
            }
            else if (lector == kLuaLectorBloques && g_luaLectorBloquesOriginal != nullptr)
            {
                g_luaBloques.activo = true;
                g_luaBloques.datos = datos;
                g_luaBloques.nombre = nombre;
            }
            else if (g_luaSinVolcar++ < 50u)
            {
                std::cout << "[f1-05:lua] " << nombre << ": lector 0x" << std::hex << lector << std::dec << " sin volcar"
                          << std::endl;
            }
        }
        g_trozoServido = {};
        g_cargaPendiente = {};
        const Servido servido = g_opcionesJuego ? servirOpcionesJuego(rdram, ctx, runtime, nombre) : Servido::Nada;
        if (servido != Servido::Nada)
            g_cargaPendiente = {true, servido, false, GPR_U32(ctx, 4), lector, datos, GPR_U32(ctx, 7), ra, nombre};
        g_luaLoadOriginal(rdram, ctx, runtime);
        terminarCarga(rdram, ctx, runtime);
        if (volcando && lector == kLuaLectorBloques && ctx->pc == ra)
        {
            std::lock_guard<std::mutex> cerrojo(g_luaVolcadoCerrojo);
            cerrarTrozoPorBloques();
        }
    }

    void luaLectorBloquesVolcado(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == kLuaLectorBloques;
        const uint32_t datos = GPR_U32(ctx, 5), tamanoEn = GPR_U32(ctx, 6), ra = GPR_U32(ctx, 31);
        g_luaLectorBloquesOriginal(rdram, ctx, runtime);
        std::lock_guard<std::mutex> cerrojo(g_luaVolcadoCerrojo);
        if (!g_luaBloques.activo)
            return;
        if (!entrada || ctx->pc != ra) // resumed, or not finished yet: this block is not seen whole
        {
            g_luaBloques.incompleto = true;
            return;
        }
        if (datos != g_luaBloques.datos)
            return;
        const uint32_t tamano = f1::lua_volcado::leer32(rdram, tamanoEn);
        if (tamano != 0u && !f1::lua_volcado::anadirBloque(g_luaBloques.bytes, rdram, GPR_U32(ctx, 2), tamano))
            g_luaBloques.incompleto = true;
    }

    // Lua heap (0x29B798(size, 4)): with the PC options the menus' 2 MB becomes 4 MB (f1_opciones_juego.h:
    // tamanoMonton), as in F1 06. The buffer comes from the game's malloc (0x29C618 -> 0x23BFF8), which returns 0 when
    // it has no room: then the heap is created again with the size the game asked for (F1_OPCIONES_ROTAS=3 asks for
    // kMontonImposible to test it). The race heap (1 MB) is not touched: the menus' heap is destroyed before it is
    // created, so the 4 MB are not in use during a race.
    // Each call: "[f1-05:lua-monton] t=<s> tamano=0x<asked> (<asked> bytes) a1=<a1> ra=0x<ra>" (" usado=0x<size>" when
    // it is changed) and, when it returns, "[f1-05:lua-monton] concedido=<bytes> (0x<size>) bufer=0x<buffer>". The
    // creation may yield inside (the game's malloc) and return through a resumed call: the check is then made when that
    // call reaches the return address (g_montonPendiente). Such a call is dispatched by its resume address
    // (f1_opciones_juego.h: kMontonReanudar), so those addresses are hooked too (instalarLua).
    struct MontonPendiente
    {
        bool activo = false;
        uint32_t pedido = 0u, usado = 0u, a1 = 0u, ra = 0u;
    };
    MontonPendiente g_montonPendiente; // only on the game thread

    // After the original returned to g_montonPendiente.ra: the size granted and, if the bigger heap got no buffer, the
    // heap again with the size asked for.
    void terminarMonton(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        while (g_montonPendiente.activo && ctx->pc == g_montonPendiente.ra)
        {
            const f1::opciones_juego::Monton monton = f1::opciones_juego::leerMonton(rdram);
            if (monton.bufer == 0u && g_montonPendiente.usado != g_montonPendiente.pedido)
            {
                std::cout << "[f1-05:lua-monton] sin sitio para 0x" << std::hex << g_montonPendiente.usado << std::dec
                          << " en el malloc del juego: se crea con lo pedido" << std::endl;
                g_montonPendiente.usado = g_montonPendiente.pedido;
                SET_GPR_U32(ctx, 4, g_montonPendiente.pedido);
                SET_GPR_U32(ctx, 5, g_montonPendiente.a1);
                SET_GPR_U32(ctx, 31, g_montonPendiente.ra);
                ctx->pc = kLuaMonton;
                g_luaMontonOriginal(rdram, ctx, runtime);
                continue; // checked again if it returned now; otherwise when its resumed call does
            }
            g_montonPendiente.activo = false;
            std::cout << "[f1-05:lua-monton] concedido=" << (monton.bufer != 0u ? monton.tamano : 0u) << " (0x"
                      << std::hex << monton.tamano << ") bufer=0x" << monton.bufer << std::dec
                      << (monton.bufer == 0u ? " SIN BUFER (el malloc del juego no tiene sitio)" : "") << std::endl;
        }
    }

    void luaMontonJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != kLuaMonton) // resumed in the middle
        {
            g_luaMontonOriginal(rdram, ctx, runtime);
            terminarMonton(rdram, ctx, runtime);
            return;
        }
        const uint32_t pedido = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), ra = GPR_U32(ctx, 31);
        const uint32_t usado = f1::opciones_juego::tamanoMonton(pedido, g_opcionesJuego, g_opcionesRotas);
        std::cout << "[f1-05:lua-monton] t=" << segundosDesdeArranque() << " tamano=0x" << std::hex << pedido << std::dec
                  << " (" << pedido << " bytes) a1=" << a1 << " ra=0x" << std::hex << ra << std::dec;
        if (usado != pedido)
            std::cout << " usado=0x" << std::hex << usado << std::dec;
        std::cout << std::endl;
        g_montonPendiente = {pedido != 0u, pedido, usado, a1, ra};
        SET_GPR_U32(ctx, 4, usado);
        g_luaMontonOriginal(rdram, ctx, runtime);
        terminarMonton(rdram, ctx, runtime);
    }

    // ---- PC options in the game's own menus (odd/tasks/opciones-pc.md T4; F1 06's src/f1_overrides.cpp:6685-6704,
    // :8338-8620) ----
    // The game's bytecode is not changed: when lua_load loads one of the chunks of f1_opciones_juego.h's kExtensiones
    // (by claveTrozo of its name; only with the readers {pointer, size}, 0x2891F0 and getS), its bytes are kept and a
    // text chunk is served in their place (reader swapped for getS 0x2AE4F8, wrapped here: it hands out our bytes in
    // blocks of up to kBuferLua from a buffer of the runtime's heap). That text first runs the original, asked for with
    // loadstring("", "=ps2x:<chunk>") in the chunk's environment (the same hook knows that name and serves the kept
    // bytecode; the parser sees the 0x1B), and then the extension, which wraps the original's functions. No Lua
    // strings are made from C++ and no game code is called from the hooks (it may yield at a checkpoint).
    // Bridge: os.getenv (0x2AE880; the game's scripts never call it) with a key "ps2x:<...>" returns a number or nil
    // (f1_opciones_juego.h: leerArgumentos / escribirResultado, the TObject of lua_pushnumber 0x29D4A0); any other key
    // goes to the original. The keys (puenteOpciones): "ps2x:<option>"[, v] reads (or sets live, applied by the runtime
    // in the next frame) an option of the runtime's model (patch 0143, PS2OptionsMenu::gameOptionNumber /
    // setGameOptionNumber); "pagina"[, v] the PC page DisplaySettings shows (0 = the game's own rows; T6); "abrir"
    // takes the values F10/F11 changed (syncGameOptions); "recordar", "descartar" and "por_defecto" keep, restore or
    // reset the page's options (f1_opciones_juego.h: clavesPagina); "guardar" writes settings.ini; "ventanas",
    // "ventana_ancho"/"ventana_alto" (of list entry v), "tamano_ventana"[, v] and "gs_en_uso" are PC DISPLAY's;
    // "prueba", v logs "[f1-05:opciones] puente ok vsync=<v>" (the bridge check of kExtensionOpciones); "entrada" /
    // "entrada_pausa", v log where the PC OPTIONS entry was put (T5); "error_letra", c and "error", page log a Lua error
    // of a PC page (T6).
    // A served chunk that does not load falls back to the original (terminarCarga); F1_OPCIONES_ROTAS=1|2 breaks the
    // extensions or drops the originals, only to test that (3: the heap's fallback, luaMontonJuego).
    // T6-T7: a =ps2x: load of a PC page's name ("=ps2x:graficos-pc"...; f1_opciones_juego.h: cuerpoPagina) gets that
    // page's body, asked for by kExtensionPantalla when it opens the page.
    // F1_OPCIONES_JUEGO=0 turns all of it off (and the 4 MB heap): nothing is hooked unless F1_LUA_VOLCAR is set.
    constexpr uint32_t kOsGetenv = 0x002AE880u;
    constexpr uint32_t kBuferLua = 0x800u;
    PS2Runtime::RecompiledFunction g_luaGetSOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_osGetenvOriginal = nullptr;
    uint32_t g_buferLua = 0u;
    int g_paginaOpciones = 0;
    std::map<std::string, std::vector<uint8_t>> g_trozosOriginales; // chunk key -> its bytecode

    // lua_load hook: what is served in place of the chunk (getS serves it from g_trozoServido), or Servido::Nada.
    Servido servirOpcionesJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, const std::string &nombre)
    {
        if (g_luaGetSOriginal == nullptr)
            return Servido::Nada;
        const uint32_t lector = GPR_U32(ctx, 5);
        const uint32_t datos = GPR_U32(ctx, 6);
        std::vector<uint8_t> bytes;
        std::string detalle;
        std::string clave;
        Servido servido = Servido::Extension;
        if (f1::opciones_juego::nombreOriginal(nombre, clave))
        {
            const auto original = g_trozosOriginales.find(clave);
            if (const char *pagina = f1::opciones_juego::cuerpoPagina(clave); pagina != nullptr)
            {
                const std::string texto = pagina;
                bytes.assign(texto.begin(), texto.end());
                detalle = "pagina " + clave;
                servido = Servido::Pagina;
            }
            else if (original == g_trozosOriginales.end())
            {
                const std::string texto = f1::opciones_juego::textoSinOriginal(nombre);
                bytes.assign(texto.begin(), texto.end());
                detalle = "no se guardo el original; la carga falla a proposito";
                servido = Servido::SinOriginal;
            }
            else
            {
                bytes = original->second;
                detalle = "bytecode original";
                servido = Servido::Original;
            }
        }
        else
        {
            clave = f1::opciones_juego::claveTrozo(nombre);
            const char *extension = f1::opciones_juego::extension(clave);
            if (extension == nullptr)
                return Servido::Nada;
            f1::lua_volcado::Trozo trozo;
            if (lector != kLuaLectorGetS && lector != kLuaLectorFrontEnd)
            {
                std::cout << "[f1-05:opciones] " << nombre << ": lector 0x" << std::hex << lector << std::dec
                          << " desconocido; se carga sin las opciones del PC" << std::endl;
                return Servido::Nada;
            }
            if (!f1::lua_volcado::leerPunteroTamano(rdram, datos, trozo))
            {
                std::cout << "[f1-05:opciones] " << nombre << ": sin datos; se carga sin las opciones del PC" << std::endl;
                return Servido::Nada;
            }
            if (g_opcionesRotas == 2)
                g_trozosOriginales.erase(clave);
            else
                g_trozosOriginales[clave].assign(rdram + trozo.puntero, rdram + trozo.puntero + trozo.tamano);
            std::string texto = f1::opciones_juego::textoConExtension(nombre, extension);
            if (g_opcionesRotas == 1)
                texto += f1::opciones_juego::kTextoRoto;
            bytes.assign(texto.begin(), texto.end());
            detalle = "original de " + std::to_string(trozo.tamano) + " bytes y extension" +
                      (g_opcionesRotas != 0 ? " (F1_OPCIONES_ROTAS=" + std::to_string(g_opcionesRotas) + ")" : "");
        }
        if (g_buferLua == 0u)
            g_buferLua = runtime->guestMalloc(kBuferLua, 64u);
        if (g_buferLua == 0u)
        {
            std::cout << "[f1-05:opciones] " << nombre << ": sin bufer en el monton del runtime" << std::endl;
            return Servido::Nada;
        }
        std::cout << "[f1-05:opciones] " << nombre << ": " << detalle << " (" << bytes.size() << " bytes)" << std::endl;
        g_trozoServido.datos = datos;
        g_trozoServido.bytes = std::move(bytes);
        g_trozoServido.servidos = 0u;
        SET_GPR_U32(ctx, 5, kLuaLectorGetS);
        return servido;
    }

    // getS (0x2AE4F8(L, data, size*)): the chunk being served goes out in blocks of up to kBuferLua; any other data to
    // the original.
    void luaGetSJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t datos = GPR_U32(ctx, 5);
        if (ctx->pc != kLuaLectorGetS || g_trozoServido.datos == 0u || datos != g_trozoServido.datos)
        {
            g_luaGetSOriginal(rdram, ctx, runtime);
            return;
        }
        const size_t quedan = g_trozoServido.bytes.size() - g_trozoServido.servidos;
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(quedan, kBuferLua));
        std::memcpy(rdram + (g_buferLua & PS2_RAM_MASK), g_trozoServido.bytes.data() + g_trozoServido.servidos, n);
        g_trozoServido.servidos += n;
        std::memcpy(rdram + (GPR_U32(ctx, 6) & PS2_RAM_MASK), &n, sizeof(n));
        SET_GPR_U32(ctx, 2, n != 0u ? g_buferLua : 0u);
        if (n == 0u)
            g_trozoServido = {};
        ctx->pc = GPR_U32(ctx, 31);
    }

#if defined(PS2X_ENABLE_DEBUG_UI)
    std::vector<std::string> g_tamanosVentana;
    std::map<std::string, std::string> g_fotoPagina;
    std::string g_errorLua; // a PC page's Lua error, as "error_letra" sends it

    // The bridge's keys (without "ps2x:"). `hay` false returns nil.
    void puenteOpciones(PS2Runtime &runtime, const f1::opciones_juego::Argumentos &a, bool &hay, float &resultado)
    {
        const std::string &clave = a.clave;
        const long numero = a.conValor ? std::lround(a.valor) : -1;
        const bool enLista = numero >= 0 && static_cast<size_t>(numero) < g_tamanosVentana.size();
        const std::vector<std::string> &opcionesPagina = f1::opciones_juego::clavesPagina(g_paginaOpciones);
        hay = true;
        if (clave == "prueba")
        {
            if (a.conValor)
                std::cout << "[f1-05:opciones] puente ok vsync=" << a.valor << std::endl;
            else
                std::cout << "[f1-05:opciones] puente sin valor (ps2x:vsync no esta en el modelo de opciones)"
                          << std::endl;
            hay = a.conValor;
            resultado = a.valor;
        }
        else if (clave == "pagina")
        {
            if (a.conValor)
            {
                g_paginaOpciones = static_cast<int>(numero);
                std::cout << "[f1-05:opciones] pagina = " << g_paginaOpciones << std::endl;
            }
            resultado = static_cast<float>(g_paginaOpciones);
        }
        else if (clave == "entrada" || clave == "entrada_pausa")
        {
            std::cout << "[f1-05:opciones] entrada OPCIONES PC en "
                      << (clave == "entrada" ? "OPCIONES" : "las OPCIONES de la pausa") << " (posicion " << numero
                      << ", detras de DISPLAY SETTINGS)" << std::endl;
            resultado = 1.0f;
        }
        else if (clave == "abrir")
        {
            PS2OptionsMenu::syncGameOptions(runtime);
            std::cout << "[f1-05:opciones] pagina " << g_paginaOpciones << std::endl;
            resultado = 1.0f;
        }
        else if (clave == "guardar")
        {
            resultado = PS2OptionsMenu::saveGameOptions() ? 1.0f : 0.0f;
            std::cout << "[f1-05:opciones] guardar settings.ini: " << (resultado != 0.0f ? "bien" : "error") << std::endl;
        }
        else if (clave == "recordar")
        {
            g_fotoPagina.clear();
            for (const std::string &opcion : opcionesPagina)
                if (std::string texto; PS2OptionsMenu::gameOptionText(opcion, texto))
                    g_fotoPagina[opcion] = texto;
            resultado = 1.0f;
        }
        else if (clave == "descartar")
        {
            std::string fijas;
            for (const auto &[opcion, texto] : g_fotoPagina)
                if (std::string ahora; PS2OptionsMenu::gameOptionText(opcion, ahora) && ahora != texto &&
                                       !PS2OptionsMenu::setGameOptionText(opcion, texto))
                    fijas += (fijas.empty() ? "" : ", ") + opcion;
            std::cout << "[f1-05:opciones] pagina " << g_paginaOpciones << ": vuelve a lo de antes de abrirla"
                      << (fijas.empty() ? "" : " (no se puede: " + fijas + ")") << std::endl;
            resultado = 1.0f;
        }
        else if (clave == "por_defecto")
        {
            PS2OptionsMenu::resetGameOptions(opcionesPagina);
            std::string valores;
            for (const std::string &opcion : opcionesPagina)
            {
                std::string texto;
                PS2OptionsMenu::gameOptionText(opcion, texto);
                valores += (valores.empty() ? "" : ", ") + opcion + " = " + texto;
            }
            std::cout << "[f1-05:opciones] pagina " << g_paginaOpciones << ": valores por defecto (" << valores << ")"
                      << std::endl;
            resultado = 1.0f;
        }
        else if (clave == "ventanas")
        {
            g_tamanosVentana = PS2OptionsMenu::gameWindowSizes();
            resultado = static_cast<float>(g_tamanosVentana.size());
        }
        else if (clave == "ventana_ancho" || clave == "ventana_alto")
        {
            int ancho = 0, alto = 0;
            hay = enLista && ps2x::parseWindowSize(g_tamanosVentana[static_cast<size_t>(numero)], ancho, alto);
            resultado = static_cast<float>(clave == "ventana_ancho" ? ancho : alto);
        }
        else if (clave == "tamano_ventana")
        {
            if (a.conValor)
            {
                const bool cambiada =
                    enLista && PS2OptionsMenu::setGameOptionText(clave, g_tamanosVentana[static_cast<size_t>(numero)]);
                std::cout << "[f1-05:opciones] tamano_ventana = "
                          << (enLista ? g_tamanosVentana[static_cast<size_t>(numero)] : std::to_string(numero))
                          << (cambiada ? "" : " (no se puede)") << std::endl;
            }
            std::string actual;
            PS2OptionsMenu::gameOptionText(clave, actual);
            const auto encontrado = std::find(g_tamanosVentana.begin(), g_tamanosVentana.end(), actual);
            hay = encontrado != g_tamanosVentana.end();
            resultado = static_cast<float>(encontrado - g_tamanosVentana.begin());
        }
        else if (clave == "gs_en_uso")
        {
            resultado = PS2OptionsMenu::gameGsInUse() == "gpu" ? 1.0f : 0.0f;
        }
        else if (clave == "error_letra")
        {
            if (g_errorLua.size() < f1::opciones_juego::kLargoMensaje)
                g_errorLua.push_back(numero >= 0x20 && numero < 0x7F ? static_cast<char>(numero) : '?');
            resultado = 1.0f;
        }
        else if (clave == "error")
        {
            std::cout << f1::opciones_juego::lineaErrorPagina(static_cast<int>(numero), g_errorLua) << std::endl;
            g_errorLua.clear();
            resultado = 1.0f;
        }
        else
        {
            if (a.conValor)
            {
                const bool cambiada = PS2OptionsMenu::setGameOptionNumber(clave, a.valor);
                std::cout << "[f1-05:opciones] " << clave << " = " << a.valor << (cambiada ? "" : " (no se puede)")
                          << std::endl;
            }
            hay = PS2OptionsMenu::gameOptionNumber(clave, resultado);
        }
    }
#endif

    // os.getenv(key[, value]) (0x2AE880, a Lua C function): the "ps2x:" keys return 1 result written here; the rest
    // goes to the original.
    void osGetenvJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t L = GPR_U32(ctx, 4);
        f1::opciones_juego::Argumentos argumentos;
        if (ctx->pc != kOsGetenv || !f1::opciones_juego::leerArgumentos(rdram, L, argumentos))
        {
            g_osGetenvOriginal(rdram, ctx, runtime);
            return;
        }
        bool hay = false;
        float resultado = 0.0f;
#if defined(PS2X_ENABLE_DEBUG_UI)
        puenteOpciones(*runtime, argumentos, hay, resultado);
#endif
        f1::opciones_juego::escribirResultado(rdram, L, hay, resultado);
        SET_GPR_U32(ctx, 2, 1u);
        ctx->pc = GPR_U32(ctx, 31);
    }

    // F1_LUA_VOLCAR (the dump) and the PC options share the lua_load and heap hooks.
    void instalarLua(PS2Runtime &runtime)
    {
        auto instalar = [&runtime](uint32_t direccion, PS2Runtime::RecompiledFunction chivato,
                                   PS2Runtime::RecompiledFunction &original)
        {
            original = runtime.lookupFunction(direccion);
            if (original != nullptr && !runtime.replaceFunction(direccion, chivato))
                original = nullptr;
            return original != nullptr;
        };
        g_opcionesJuego = f1::opciones_juego::activas(std::getenv("F1_OPCIONES_JUEGO"));
        g_opcionesRotas = f1::opciones_juego::pruebaRota(std::getenv("F1_OPCIONES_ROTAS"));
        if (g_opcionesJuego && g_opcionesRotas != 0)
            std::cout << "[f1-05:opciones] F1_OPCIONES_ROTAS=" << g_opcionesRotas
                      << (g_opcionesRotas == 1   ? ": extensiones con un error de sintaxis (prueba)"
                          : g_opcionesRotas == 2 ? ": sin guardar los originales (prueba)"
                                                 : ": monton Lua de los menus imposible, para la vuelta al pedido (prueba)")
                      << std::endl;
#if !defined(PS2X_ENABLE_DEBUG_UI)
        if (g_opcionesJuego)
            std::cout << "[f1-05:opciones] sin el menu de opciones del runtime en esta compilacion: desactivadas"
                      << std::endl;
        g_opcionesJuego = false;
#endif
        const char *carpeta = std::getenv("F1_LUA_VOLCAR");
        const bool volcar = carpeta != nullptr && *carpeta != '\0';
        bool bloques = false;
        if (volcar)
        {
            g_luaVolcado = std::filesystem::path(carpeta);
            std::error_code error;
            std::filesystem::create_directories(g_luaVolcado, error);
            std::ofstream(g_luaVolcado / "indice.txt", std::ios::trunc)
                << "# archivo\tnombre\ttamano\tformato\tlector\tcompleto\n";
            // The block reader before lua_load: lua_load only follows its chunks if the reader is wrapped.
            bloques = instalar(kLuaLectorBloques, luaLectorBloquesVolcado, g_luaLectorBloquesOriginal);
        }
        if (!volcar && !g_opcionesJuego)
        {
            std::cout << "[f1-05:opciones] desactivadas (F1_OPCIONES_JUEGO=0)" << std::endl;
            return;
        }
        bool getS = false, entorno = false;
        if (g_opcionesJuego)
        {
            getS = instalar(kLuaLectorGetS, luaGetSJuego, g_luaGetSOriginal);
            entorno = instalar(kOsGetenv, osGetenvJuego, g_osGetenvOriginal);
        }
        const bool carga = instalar(kLuaLoad, luaLoadJuego, g_luaLoadOriginal);
        // A call resumed in the middle is dispatched by its resume address, not by 0x29DEF8: lua_load's resume points
        // (the returns of its two calls; a parse error longjmps out of luaD_protectedparser 0x2A0790 and its status
        // comes back through 0x29DF44) go through the hook too, so terminarCarga sees that status.
        if (carga && g_opcionesJuego)
            for (const uint32_t reanudar : kLuaLoadReanudar)
                if (runtime.lookupFunction(reanudar) != g_luaLoadOriginal || !runtime.replaceFunction(reanudar, luaLoadJuego))
                    std::cout << "[f1-05:opciones] lua_load: punto de reanudacion 0x" << std::hex << reanudar << std::dec
                              << " sin enganchar; un trozo servido que no compila no vuelve al original" << std::endl;
        const bool monton = instalar(kLuaMonton, luaMontonJuego, g_luaMontonOriginal);
        // The same for the heap's resume points (R4-003 of odd/tasks/opciones-pc.md L8): a creation that yields in the
        // game's malloc comes back through them, and terminarMonton (the fallback to the size asked for) runs there.
        if (monton)
            for (const uint32_t reanudar : f1::opciones_juego::kMontonReanudar)
                if (runtime.lookupFunction(reanudar) != g_luaMontonOriginal ||
                    !runtime.replaceFunction(reanudar, luaMontonJuego))
                    std::cout << "[f1-05:lua-monton] punto de reanudacion 0x" << std::hex << reanudar << std::dec
                              << " sin enganchar; una creacion reanudada no vuelve al tamano pedido" << std::endl;
        if (volcar)
            std::cout << "[f1-05:lua] volcado de trozos en " << g_luaVolcado.string() << " (lua_load=" << carga
                      << " lector 0x29b648=" << bloques << " monton=" << monton << ")" << std::endl;
        if (g_opcionesJuego)
            std::cout << "[f1-05:opciones] activadas (lua_load=" << carga << " getS=" << getS << " os.getenv=" << entorno
                      << " monton=" << monton << "; monton Lua de los menus: "
                      << (f1::opciones_juego::kMontonMenusPc >> 20) << " MB)" << std::endl;
        else
            std::cout << "[f1-05:opciones] desactivadas (F1_OPCIONES_JUEGO=0)" << std::endl;
    }

    // ---- The game's letters for the runtime's FPS counter (odd/tasks/opciones-pc.md T9; F1 06's Aspecto T2) ----
    // The .bft reader 0x2D2DE8 opens its file with 0x23FBC8 and reads it through the stream's virtual read; every read
    // of that stream is copied, in order, until it closes, and the file goes to ps2x::storeGameFontFile (the counter
    // draws only with a letter it knows, ps2_game_font.h: F1 05's Frontend/Fonts/Futurah*Px.bft). The bytes come from
    // the user's disc as the game reads them (nothing of the game goes in the package). The game reads its letters
    // again on many screens: [f1-05:letra] comes out only when they change. F1_BFT_VOLCAR=<folder>: a copy of each
    // .bft there (game data: outside the project). F1_LETRAS=0: no font capture.
    // The same open and stream hooks feed the diagnostic file dump (f1_volcado.h; off by default):
    // F1_VOLCAR_ARCHIVOS=<substring>[;<substring>...] copies each file whose path contains one (any case) to
    // F1_VOLCAR_DIR (default logs\archivos), as its stream reads it, with a line in <dir>/indice.txt per file and a
    // [f1-05:volcado] log line per open and per file written. Game data: the folder is not part of the package.
    PS2Runtime::RecompiledFunction g_leerLetraOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_abrirArchivoOriginal = nullptr;
    std::map<uint32_t, PS2Runtime::RecompiledFunction> g_lecturasFlujoOriginal, g_cierresFlujoOriginal;
    f1::letra::Captura g_letra; // only the EE thread touches it
    const char *g_volcarBft = nullptr;
    bool g_letras = false;
    f1::volcado::Volcado g_volcado; // only the EE thread touches it
    std::filesystem::path g_volcadoDir;
    uint32_t g_volcadoArchivos = 0u;

    void guardarVolcado(const f1::letra::Flujo &archivo, bool completo)
    {
        const std::string nombre = f1::volcado::nombreArchivo(archivo.nombre, ++g_volcadoArchivos);
        std::ofstream(g_volcadoDir / nombre, std::ios::binary)
            .write(reinterpret_cast<const char *>(archivo.bytes.data()),
                   static_cast<std::streamsize>(archivo.bytes.size()));
        std::ofstream(g_volcadoDir / "indice.txt", std::ios::app)
            << f1::volcado::lineaIndice(nombre, archivo.nombre, archivo.bytes.size(), completo);
        std::cout << "[f1-05:volcado] \"" << archivo.nombre << "\": " << archivo.bytes.size() << " bytes -> " << nombre
                  << (completo ? "" : " (incompleto)") << std::endl;
    }

    void leerLetraJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == f1::letra::kLeerLetra;
        const uint32_t vuelta = GPR_U32(ctx, 31);
        if (entrada)
            g_letra.empezar();
        g_leerLetraOriginal(rdram, ctx, runtime);
        if (entrada && ctx->pc == vuelta)
            g_letra.acabar(); // back to its caller: a capture still open was abandoned (L15 A2)
    }

    void abrirArchivoJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const bool entrada = ctx->pc == f1::letra::kAbrirArchivo;
        const uint32_t vuelta = GPR_U32(ctx, 31);
        const bool letra = entrada && g_letra.activa;
        const bool volcar = entrada && !g_volcado.patrones.empty();
        const std::string nombre = letra || volcar ? f1::letra::nombreDeVista(rdram, PS2_RAM_SIZE, GPR_U64(ctx, 4))
                                                   : std::string();
        g_abrirArchivoOriginal(rdram, ctx, runtime);
        if (!entrada)
            return;
        // v0 is the stream only after the normal return (L15 A1): back half way, the open goes on later unseen.
        const uint32_t flujo = f1::letra::flujoAbierto(ctx->pc, vuelta, GPR_U32(ctx, 2));
        if (letra)
            g_letra.abierto(nombre, flujo);
        if (volcar)
        {
            if (f1::volcado::coincide(g_volcado.patrones, nombre))
                std::cout << "[f1-05:volcado] abierto \"" << nombre << "\" flujo=0x" << std::hex << flujo << std::dec
                          << (ctx->pc != vuelta ? " (vuelta a medias: sin seguir)" : "") << std::endl;
            if (const auto abandonado = g_volcado.abierto(rdram, PS2_RAM_SIZE, nombre, flujo))
                guardarVolcado(*abandonado, false);
        }
    }

    template <size_t I>
    void leerFlujoJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr uint32_t direccion = f1::letra::kLecturas[I];
        if (ctx->pc == direccion)
        {
            g_letra.leer(rdram, PS2_RAM_SIZE, GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6));
            if (!g_volcado.abiertos.empty())
                g_volcado.leer(rdram, PS2_RAM_SIZE, GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6));
        }
        g_lecturasFlujoOriginal[direccion](rdram, ctx, runtime);
    }

    template <size_t I>
    void cerrarFlujoJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        constexpr uint32_t direccion = f1::letra::kCierres[I];
        if (ctx->pc == direccion && !g_volcado.abiertos.empty())
            if (const auto cerrado = g_volcado.cerrar(rdram, PS2_RAM_SIZE, GPR_U32(ctx, 4)))
                guardarVolcado(*cerrado, true);
        if (ctx->pc == direccion && g_letra.cerrar(rdram, PS2_RAM_SIZE, GPR_U32(ctx, 4)))
        {
            if (g_volcarBft != nullptr)
            {
                const std::string base = g_letra.nombre.substr(g_letra.nombre.find_last_of('/') + 1);
                std::ofstream(std::string(g_volcarBft) + "/" + base, std::ios::binary)
                    .write(reinterpret_cast<const char *>(g_letra.bytes.data()),
                           static_cast<std::streamsize>(g_letra.bytes.size()));
            }
            const size_t bytes = g_letra.bytes.size();
            if (ps2x::storeGameFontFile(g_letra.nombre, std::move(g_letra.bytes)))
                std::cout << "[f1-05:letra] \"" << g_letra.nombre << "\": " << bytes << " bytes leidos" << std::endl;
            g_letra = f1::letra::Captura{};
        }
        g_cierresFlujoOriginal[direccion](rdram, ctx, runtime);
    }

    template <size_t... I>
    int instalarFlujos(PS2Runtime &runtime, std::index_sequence<I...>)
    {
        int enganchados = 0;
        auto instalar = [&runtime, &enganchados](uint32_t direccion, PS2Runtime::RecompiledFunction chivato,
                                                 std::map<uint32_t, PS2Runtime::RecompiledFunction> &originales)
        {
            const PS2Runtime::RecompiledFunction original = runtime.lookupFunction(direccion);
            if (original != nullptr && runtime.replaceFunction(direccion, chivato))
            {
                originales[direccion] = original;
                ++enganchados;
            }
        };
        (instalar(f1::letra::kLecturas[I], leerFlujoJuego<I>, g_lecturasFlujoOriginal), ...);
        (instalar(f1::letra::kCierres[I], cerrarFlujoJuego<I>, g_cierresFlujoOriginal), ...);
        return enganchados;
    }

    void instalarLetras(PS2Runtime &runtime)
    {
        g_letras = true;
        if (const char *valor = std::getenv("F1_LETRAS"); valor != nullptr && std::strcmp(valor, "0") == 0)
        {
            std::cout << "[f1-05:letra] sin capturar las letras del juego (F1_LETRAS=0)" << std::endl;
            g_letras = false;
        }
        if (const char *valor = std::getenv("F1_VOLCAR_ARCHIVOS"); valor != nullptr)
            g_volcado.patrones = f1::volcado::patrones(valor);
        if (!g_letras && g_volcado.patrones.empty())
            return;
        if (!g_volcado.patrones.empty())
        {
            const char *carpeta = std::getenv("F1_VOLCAR_DIR");
            g_volcadoDir = std::filesystem::path(carpeta != nullptr && *carpeta != '\0' ? carpeta : "logs\\archivos");
            std::error_code error;
            std::filesystem::create_directories(g_volcadoDir, error);
            std::ofstream(g_volcadoDir / "indice.txt", std::ios::trunc) << f1::volcado::cabeceraIndice();
        }
        if (const char *carpeta = std::getenv("F1_BFT_VOLCAR"); g_letras && carpeta != nullptr && *carpeta != '\0')
            g_volcarBft = carpeta;
        static_assert(std::size(f1::letra::kLecturas) == std::size(f1::letra::kCierres));
        // The stream hooks first: they must be in place before the reader can run.
        const int flujos =
            instalarFlujos(runtime, std::make_index_sequence<std::size(f1::letra::kLecturas)>{});
        g_abrirArchivoOriginal = runtime.lookupFunction(f1::letra::kAbrirArchivo);
        const bool abrir = g_abrirArchivoOriginal != nullptr &&
                           runtime.replaceFunction(f1::letra::kAbrirArchivo, abrirArchivoJuego);
        if (!g_volcado.patrones.empty())
        {
            std::cout << "[f1-05:volcado] F1_VOLCAR_ARCHIVOS:";
            for (const std::string &p : g_volcado.patrones)
                std::cout << " \"" << p << "\"";
            std::cout << " -> " << g_volcadoDir.string() << " (abrir=" << abrir << " flujos=" << flujos << "/"
                      << 2 * std::size(f1::letra::kLecturas) << ")" << std::endl;
        }
        if (!g_letras)
            return;
        g_leerLetraOriginal = runtime.lookupFunction(f1::letra::kLeerLetra);
        const bool lector = abrir && g_leerLetraOriginal != nullptr &&
                            runtime.replaceFunction(f1::letra::kLeerLetra, leerLetraJuego);
        std::cout << "[f1-05:letra] ganchos lector=" << lector << " abrir=" << abrir << " flujos=" << flujos << "/"
                  << 2 * std::size(f1::letra::kLecturas)
                  << (g_volcarBft != nullptr ? "; copia de cada .bft en F1_BFT_VOLCAR" : "") << std::endl;
    }

    // The game's scePadRead (0x2EB168, config/recomp.toml) is F1 05's per-frame hook: the game reads the pad on every
    // frame, also in the menus, on its own thread. F1 06 uses its frame render function for the same two jobs.
    // - Widescreen option (odd/tasks/pantalla-ancha.md T2): every 10 reads it looks at the game's flag
    //   (f1_panoramica.h, *0x41586C + 0x28) and passes it to the presenter (ps2SetGameWidescreen, patch 0103) only when
    //   it changes or the profile pointer becomes valid. Always on: with aspecto = auto the window follows it.
    // - PS2X_PRUEBA_FALLO=<acceso|excepcion|terminate|exit>:<seconds> (ps2_fallo.h, only to test the reports): that
    //   crash once, at <seconds> since the program started; without the variable it does nothing. exit: the game calls
    //   its exit(1) instead of that pad read. lua is not supported: F1 06's trigger raises a Lua error from a native of
    //   its Lua front end (end_frame, luaB_error), and F1 05's equivalents are not mapped.
    constexpr uint32_t kScePadRead = 0x002EB168u;
    PS2Runtime::RecompiledFunction g_scePadReadOriginal = nullptr;
    ps2x::fallo::PruebaFallo g_pruebaFallo; // read and cleared only on the game thread
    f1::panoramica::Lector g_panoramica;   // only on the game thread

    void exitDePrueba(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::cout << "[f1-05:exit] prueba: exit(1) del juego a proposito (PS2X_PRUEBA_FALLO=exit) t="
                  << segundosDesdeArranque() << std::endl;
        SET_GPR_U32(ctx, 4, 1u);
        ctx->pc = kExit;
        runtime->lookupFunction(kExit)(rdram, ctx, runtime);
    }

    void scePadReadDelJuego(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int panoramica = f1::panoramica::leer(g_panoramica, rdram);
        if (panoramica != f1::panoramica::kSinCambios)
        {
            std::cout << "[f1-05] opcion panoramica = " << panoramica << " t=" << segundosDesdeArranque() << std::endl;
            ps2SetGameWidescreen(panoramica);
        }
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

    void instalarScePadRead(PS2Runtime &runtime)
    {
        g_scePadReadOriginal = runtime.lookupFunction(kScePadRead);
        if (g_scePadReadOriginal != nullptr && !runtime.replaceFunction(kScePadRead, scePadReadDelJuego))
            g_scePadReadOriginal = nullptr;
        if (g_scePadReadOriginal == nullptr)
            std::cout << "[f1-05] no se puede enganchar scePadRead (0x2EB168): sin opcion panoramica" << std::endl;
    }

    // After instalarScePadRead: the crash test goes in its hook.
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
        if (g_scePadReadOriginal != nullptr)
            g_pruebaFallo = prueba;
        std::cout << "[fallo] prueba: PS2X_PRUEBA_FALLO a los " << prueba.segundos << " s "
                  << (g_scePadReadOriginal != nullptr ? "preparada (en scePadRead)" : "sin enganche") << std::endl;
    }

    // Field of view (settings.ini fov, AJUSTES PC row CAMPO DE VISION; odd/tasks/opciones-pc.md T10, as F1 06's
    // patch 0160): func_2C55B0 is hooked and, only when it returns to one of the player's race camera kinds
    // (f1_campo_vision.h: the camera map, from Ghidra), its half angle f12 is widened by fov percent before the game's
    // code, at most 1.4 rad. func_2C5080 turns it into +0x80, which the projection and the frustum culling use, so
    // culling follows. The game passes its own angle on every frame: no accumulation, and a change of the row shows on
    // the next frame. fov = 0 calls the original with the game's angle untouched. Creation, scenes, menus and the TV
    // director's cameras keep theirs.
    // F1_FOV_DIAG=1 also hooks func_2C5080 and the race camera method 0x1EAA88 and logs every 5 s, per function and
    // caller (ra), the camera, the angle given and used, the resulting +0x80, the camera index and type of the method's
    // last call, its final +0x80 (after func_2C5470) and the factor *0x48FAE0.
    PS2Runtime::RecompiledFunction g_camaraEnvolturaOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_camaraProyeccionOriginal = nullptr;
    PS2Runtime::RecompiledFunction g_metodoCamaraOriginal = nullptr;

    int porcentajeFov()
    {
#if defined(PS2X_ENABLE_DEBUG_UI)
        // The runtime's model: settings.ini, PS2X_FOV over it, and the AJUSTES PC row live.
        float valor = 0.0f;
        if (PS2OptionsMenu::gameOptionNumber("fov", valor))
            return static_cast<int>(std::lround(valor));
#endif
        static const int variable = f1::campo_vision::porcentajeTexto(std::getenv("PS2X_FOV"));
        return variable;
    }

    struct FovVisto
    {
        uint32_t camara = 0u, veces = 0u;
        float dado = 0.0f, usado = 0.0f, horizontal = 0.0f;
        int indice = -1, tipo = -1;
    };
    std::map<std::pair<uint32_t, uint32_t>, FovVisto> g_fovVistos; // (function, ra)
    struct
    {
        int indice = -1, tipo = -1;
        uint32_t camara = 0u;
        float final = 0.0f;
    } g_fovMetodo;

    void anotarFov(const uint8_t *rdram, uint32_t funcion, uint32_t ra, uint32_t camara, float dado, float usado)
    {
        FovVisto &visto = g_fovVistos[{funcion, ra}];
        visto.camara = camara;
        visto.dado = dado;
        visto.usado = usado;
        visto.horizontal = leerFloat(rdram, camara + 0x80u);
        visto.indice = g_fovMetodo.indice;
        visto.tipo = g_fovMetodo.tipo;
        ++visto.veces;
        static double ultimo = 0.0;
        const double ahora = segundosDesdeArranque();
        if (ahora - ultimo < 5.0)
            return;
        ultimo = ahora;
        for (const auto &[clave, v] : g_fovVistos)
            std::cout << "[f1-05:fov] t=" << ahora << " 0x" << std::hex << clave.first << " llamador 0x" << clave.second
                      << " (" << f1::campo_vision::nombreLlamador(clave.second) << ") camara 0x" << v.camara << std::dec
                      << " f12 " << v.dado << " -> " << v.usado << " +0x80 " << v.horizontal << " indice " << v.indice
                      << " tipo " << v.tipo << " veces " << v.veces << std::endl;
        std::cout << "[f1-05:fov] t=" << ahora << " metodo 0x1EAA88: camara 0x" << std::hex << g_fovMetodo.camara
                  << std::dec << " +0x80 final " << g_fovMetodo.final << " factor *0x48FAE0 "
                  << leerFloat(rdram, f1::campo_vision::kFactorFrustum) << " fov = " << porcentajeFov() << std::endl;
        g_fovVistos.clear();
    }

    void camaraEnvoltura(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != f1::campo_vision::kCamaraEnvoltura) // only at the entry, with its arguments
        {
            g_camaraEnvolturaOriginal(rdram, ctx, runtime);
            return;
        }
        const uint32_t ra = GPR_U32(ctx, 31), camara = GPR_U32(ctx, 4);
        const float dado = ctx->f[12];
        if (f1::campo_vision::ensanchaRetorno(ra))
        {
            const int porcentaje = porcentajeFov();
            static int anterior = 0;
            if (porcentaje != anterior)
            {
                std::cout << "[f1-05:fov] t=" << segundosDesdeArranque() << " fov = " << porcentaje << std::endl;
                anterior = porcentaje;
            }
            ctx->f[12] = f1::campo_vision::ensancharCamara(ra, dado, porcentaje);
        }
        const float usado = ctx->f[12];
        g_camaraEnvolturaOriginal(rdram, ctx, runtime);
        if (g_camaraProyeccionOriginal != nullptr) // F1_FOV_DIAG
            anotarFov(rdram, f1::campo_vision::kCamaraEnvoltura, ra, camara, dado, usado);
    }

    // F1_FOV_DIAG: func_2C5080(f12, a0 = camera, ...), also the direct calls (menus, TV).
    void camaraProyeccion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != f1::campo_vision::kCamaraProyeccion)
        {
            g_camaraProyeccionOriginal(rdram, ctx, runtime);
            return;
        }
        const uint32_t ra = GPR_U32(ctx, 31), camara = GPR_U32(ctx, 4);
        const float dado = ctx->f[12];
        g_camaraProyeccionOriginal(rdram, ctx, runtime);
        anotarFov(rdram, f1::campo_vision::kCamaraProyeccion, ra, camara, dado, dado);
    }

    // F1_FOV_DIAG: the race camera method (a0 = the camera set: +0x80 index, +0x118 the types; a1 = the car object, its
    // camera at +0x330).
    void metodoCamara(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (ctx->pc != f1::campo_vision::kMetodoCamara)
        {
            g_metodoCamaraOriginal(rdram, ctx, runtime);
            return;
        }
        const uint32_t juego = GPR_U32(ctx, 4), coche = GPR_U32(ctx, 5);
        const uint32_t indice = leerU32(rdram, juego + 0x80u);
        g_fovMetodo.indice = static_cast<int>(indice);
        const uint32_t tipos = leerU32(rdram, juego + 0x118u);
        g_fovMetodo.tipo = indice < 64u ? static_cast<int>(leerU32(rdram, tipos + indice * 4u)) : -1;
        g_fovMetodo.camara = coche + 0x330u;
        g_metodoCamaraOriginal(rdram, ctx, runtime);
        g_fovMetodo.final = leerFloat(rdram, coche + 0x330u + 0x80u);
    }

    void instalarCampoVision(PS2Runtime &runtime)
    {
        auto instalar = [&runtime](uint32_t direccion, PS2Runtime::RecompiledFunction chivato,
                                   PS2Runtime::RecompiledFunction &original)
        {
            original = runtime.lookupFunction(direccion);
            if (original != nullptr && !runtime.replaceFunction(direccion, chivato))
                original = nullptr;
            return original != nullptr;
        };
        const bool gancho = instalar(f1::campo_vision::kCamaraEnvoltura, camaraEnvoltura, g_camaraEnvolturaOriginal);
        bool diag = false;
        if (gancho && std::getenv("F1_FOV_DIAG") != nullptr)
            diag = instalar(f1::campo_vision::kMetodoCamara, metodoCamara, g_metodoCamaraOriginal) &&
                   instalar(f1::campo_vision::kCamaraProyeccion, camaraProyeccion, g_camaraProyeccionOriginal);
        std::cout << "[f1-05:fov] fov = " << porcentajeFov() << " (gancho 0x2C55B0 "
                  << (gancho ? "puesto" : "sin poner") << (diag ? ", F1_FOV_DIAG" : "") << ")" << std::endl;
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
        instalarScePadRead(runtime);
        instalarFallos(runtime);
        instalarChivatosAverias(runtime);
        instalarLetras(runtime);
        instalarLua(runtime);
        instalarCampoVision(runtime);
        arrancarPulsaciones();
        arrancarAb();
        arrancarVolcadoGs(runtime);
    }
}

// The crash of PS2X_PRUEBA_FALLO (ps2_fallo.h). excepcion: std::runtime_error on the game thread (it reaches
// GameThread's catch: "[excepcion]" report, exits with 10); terminate: std::runtime_error on a helper thread
// "PruebaFallo" (nobody catches it: std::terminate, "[terminate]" report and abort); acceso and the thread:
// f1_fallos.cpp. exit goes in scePadReadDelJuego, with the EE context.
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
