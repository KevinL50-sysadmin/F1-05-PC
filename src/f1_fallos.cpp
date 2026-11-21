// Crash report of the F1 05 port (fase 5 T1, ported from F1 06's src/f1_fallos.cpp, odd/tasks/paquete.md S3): if the
// process goes down (unhandled exception), it writes to the log the code, the address and the ps2_fallo.h report
// ("[fallo]": build identity, thread, EE pc and ra and the stack as module+0xoffset, which scripts/simbolizar.ps1
// translates with the saved PDB of that version). Every game function is a host function (sub_XXXXXX), so the stack
// is also the EE's. The filter returns EXCEPTION_CONTINUE_SEARCH: Windows ends the process with the exception code
// (e.g. 0xC0000005), which RecompLauncher sees as a crash. The runner is built in batches (unity) together with files
// that include raylib, which clashes with windows.h: so the few Win32 declarations needed are here. The names in the
// anonymous namespace start with "fallo" so they do not clash with f1_overrides.cpp's in the same batch.
// F1 06's processor-core helpers (f1EnNucleoEficiente...) are not ported: F1 05's runtime does not use them.
#include "ps2_fallo.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

extern "C"
{
    __declspec(dllimport) void *__stdcall SetUnhandledExceptionFilter(void *filtro);
    __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId();
    __declspec(dllimport) void *__stdcall GetCurrentThread();
    __declspec(dllimport) void *__stdcall GetModuleHandleW(const wchar_t *modulo);
    __declspec(dllimport) void *__stdcall GetProcAddress(void *modulo, const char *nombre);
}

namespace
{
    // EXCEPTION_RECORD (x64), the fields used.
    struct FalloRegistro
    {
        unsigned long ExceptionCode;
        unsigned long ExceptionFlags;
        FalloRegistro *ExceptionRecord;
        void *ExceptionAddress;
        unsigned long NumberParameters;
        uintptr_t ExceptionInformation[15];
    };
    struct FalloPunteros
    {
        FalloRegistro *ExceptionRecord;
        uint8_t *ContextRecord; // CONTEXT: Rsp at +0x98, Rip at +0xF8
    };

    long __stdcall falloInformar(FalloPunteros *info)
    {
        static volatile long s_dentro = 0;
        if (s_dentro++ != 0)
            return 0; // EXCEPTION_CONTINUE_SEARCH
        const FalloRegistro *r = info->ExceptionRecord;
        uint64_t rsp = 0, rip = 0;
        std::memcpy(&rsp, info->ContextRecord + 0x98, 8);
        std::memcpy(&rip, info->ContextRecord + 0xF8, 8);
        std::fprintf(stderr, "[fallo] excepcion 0x%08lx en 0x%p (hilo %lu)", r->ExceptionCode, r->ExceptionAddress,
                     GetCurrentThreadId());
        if (r->ExceptionCode == 0xC0000005ul && r->NumberParameters >= 2)
            std::fprintf(stderr, ", %s 0x%llx",
                         r->ExceptionInformation[0] == 8 ? "ejecutar" : r->ExceptionInformation[0] == 1 ? "escribir" : "leer",
                         static_cast<unsigned long long>(r->ExceptionInformation[1]));
        std::fprintf(stderr, "\n");
        // From here the stack goes through the exception dispatcher and continues at the faulting point. After a jump
        // to address 0 there is no frame of the fault: #0 is then the caller (the return address on top of the stack).
        void *pila[64] = {};
        int n = 0;
        const bool saltoACero = rip == 0 && rsp != 0;
        if (saltoACero)
            std::memcpy(&pila[n++], reinterpret_cast<const void *>(rsp), 8);
        n += ps2x::fallo::pilaActual(pila + n, 64 - n, 0);
        ps2x::fallo::escribirInforme("[fallo] ",
                                     saltoACero ? "pila (salto a 0: #0 es quien llamo; luego el despachador de excepciones)"
                                                : "pila (incluye el despachador de excepciones)",
                                     pila, n);
        std::fflush(stderr);
        std::fflush(stdout);
        return 0;
    }

    const bool s_falloInstalado = []
    {
        SetUnhandledExceptionFilter(reinterpret_cast<void *>(&falloInformar));
        return true;
    }();
}

// PS2X_PRUEBA_FALLO (ps2_fallo.h, only to test the reports): each kind of crash on its own (not inlined) so it shows
// up by name in the stack. acceso: a write to address 0 on the game thread (the "[fallo]" report).
__declspec(noinline) void f1FalloDeAcceso()
{
    volatile int *volatile nulo = nullptr;
    std::fprintf(stderr, "[fallo] prueba: escritura en la direccion 0 a proposito (PS2X_PRUEBA_FALLO=acceso)\n");
    *nulo = 0x0F105;
}

// terminate: std::runtime_error on a helper thread "PruebaFallo" that nobody catches (std::terminate, "[terminate]").
__declspec(noinline) void f1FalloEnHiloAuxiliar()
{
    std::thread([]
                {
                    using Nombrar = long(__stdcall *)(void *, const wchar_t *); // SetThreadDescription (Windows 10 1607)
                    if (const auto nombrar = reinterpret_cast<Nombrar>(
                            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")))
                        nombrar(GetCurrentThread(), L"PruebaFallo");
                    throw std::runtime_error("prueba: excepcion en un hilo auxiliar (PS2X_PRUEBA_FALLO=terminate)");
                })
        .detach();
}
