// Perfilador por muestreo de un proceso en marcha (con símbolos del PDB), en una VENTANA de tiempo de la partida.
// Copia adaptada de scripts/perfil.cpp de F1-06 (rendimiento.md T2 de F1-05).
// Uso: perfil.exe <pid> <desde_s> <duracion_s> [intervalo_ms=4] [hilo=GameThread (nombre) o tid]
//   desde_s: segundos desde el ARRANQUE del proceso (como el t= de [ps2x:fotogramas] y medir-hilos.ps1).
//   Antes de esperar carga los símbolos de todos los módulos (para no congelar el hilo en la primera muestra).
// Suspende el hilo, guarda su pila (direcciones) y lo reanuda; al final resuelve los símbolos y muestra:
//  - CPU del hilo en la ventana (GetThreadTimes) frente al tiempo de pared;
//  - exclusivo: función en lo alto de la pila (400 entradas);
//  - inclusivo: funciones presentes en la pila (una vez por muestra);
//  - líneas más calientes (exclusivo).
// PERFIL_SERIE=<archivo>: una línea por muestra "t_ms<TAB>hoja;llamador;..." (hasta 24 marcos), para analizar.py.
// PERFIL_CRUDO=<archivo>: direcciones sin símbolo (JIT de la VU1) para jit_mapa.py.
// Compilar (tras entorno.cmd): cl /nologo /std:c++17 /EHsc /O2 scripts\perfil\perfil.cpp /Fo:<dir>\perfil.obj /Fe:<dir>\perfil.exe dbghelp.lib winmm.lib psapi.lib
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

static double cpuHilo(DWORD tid)
{
    HANDLE hilo = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!hilo)
        return 0.0;
    FILETIME c, e, k, u;
    GetThreadTimes(hilo, &c, &e, &k, &u);
    CloseHandle(hilo);
    return (static_cast<double>(u.dwLowDateTime) + static_cast<double>(u.dwHighDateTime) * 4294967296.0) / 1e7;
}

static std::vector<DWORD> hilosDe(DWORD pid)
{
    std::vector<DWORD> hilos;
    HANDLE instantanea = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(instantanea, &te); ok; ok = Thread32Next(instantanea, &te))
        if (te.th32OwnerProcessID == pid)
            hilos.push_back(te.th32ThreadID);
    CloseHandle(instantanea);
    return hilos;
}

struct Simbolo
{
    std::string nombre;
    DWORD64 inicio = 0;
};

static double cpuTotalHilo(HANDLE hilo)
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(hilo, &c, &e, &k, &u))
        return 0.0;
    auto s = [](const FILETIME &f)
    { return (static_cast<double>(f.dwLowDateTime) + static_cast<double>(f.dwHighDateTime) * 4294967296.0) / 1e7; };
    return s(k) + s(u);
}

// Segundos desde el arranque del proceso (reloj de pared del sistema, como GetProcessTimes).
static double segundosDesdeArranque(HANDLE proceso)
{
    FILETIME creado, salida, k, u, ahora;
    GetProcessTimes(proceso, &creado, &salida, &k, &u);
    GetSystemTimePreciseAsFileTime(&ahora);
    ULARGE_INTEGER a, b;
    a.LowPart = creado.dwLowDateTime;
    a.HighPart = creado.dwHighDateTime;
    b.LowPart = ahora.dwLowDateTime;
    b.HighPart = ahora.dwHighDateTime;
    return static_cast<double>(b.QuadPart - a.QuadPart) / 1e7;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "Uso: perfil.exe <pid> <desde_s> <duracion_s> [intervalo_ms=4] [hilo=GameThread]\n");
        return 1;
    }
    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 0));
    const double desde = std::atof(argv[2]);
    const double duracion = std::atof(argv[3]);
    const int intervalo = argc > 4 ? std::atoi(argv[4]) : 4;
    const char *nombreHilo = argc > 5 ? argv[5] : "GameThread";
    HANDLE proceso = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proceso)
    {
        std::fprintf(stderr, "No se puede abrir el proceso %lu\n", pid);
        return 1;
    }
    auto esperarHasta = [&](double segundos) -> bool
    {
        while (segundosDesdeArranque(proceso) < segundos)
        {
            DWORD codigo = 0;
            if (GetExitCodeProcess(proceso, &codigo) && codigo != STILL_ACTIVE)
                return false;
            Sleep(20);
        }
        return true;
    };

    // Los modulos (pgs_puente.dll, etc.) y el hilo con nombre aparecen tras el arranque: se espera a 20 s antes de la
    // ventana (o a 30 s de partida) para inicializar los simbolos y cargarlos ya (con SYMOPT_DEFERRED_LOADS se
    // cargarian en la primera muestra, con el hilo suspendido: el PDB del juego pesa ~470 MB).
    if (!esperarHasta(std::max(30.0, desde - 20.0)))
    {
        std::fprintf(stderr, "El proceso termino antes de la ventana\n");
        return 1;
    }
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proceso, nullptr, TRUE);
    {
        HMODULE modulos[1024];
        DWORD bytes = 0;
        if (EnumProcessModules(proceso, modulos, sizeof(modulos), &bytes))
        {
            for (DWORD i = 0; i < bytes / sizeof(HMODULE); ++i)
            {
                char buf[sizeof(SYMBOL_INFO) + 256]{};
                auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 255;
                DWORD64 desp = 0;
                SymFromAddr(proceso, reinterpret_cast<DWORD64>(modulos[i]) + 0x1000u, &desp, sym);
                SymFunctionTableAccess64(proceso, reinterpret_cast<DWORD64>(modulos[i]) + 0x1000u);
            }
        }
        std::printf("Simbolos cargados a t=%.1f s\n", segundosDesdeArranque(proceso));
        std::fflush(stdout);
    }
    if (!esperarHasta(desde))
    {
        std::fprintf(stderr, "El proceso termino antes de la ventana\n");
        return 1;
    }

    DWORD tid = static_cast<DWORD>(std::strtoul(nombreHilo, nullptr, 0));
    // El hilo tambien se puede dar por su nombre (SetThreadDescription), p. ej. "GS raster".
    if (tid == 0u)
    {
        wchar_t buscado[128] = {};
        MultiByteToWideChar(CP_UTF8, 0, nombreHilo, -1, buscado, 127);
        for (DWORD h : hilosDe(pid))
        {
            HANDLE consulta = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, h);
            if (!consulta)
                continue;
            PWSTR nombre = nullptr;
            if (SUCCEEDED(GetThreadDescription(consulta, &nombre)) && nombre)
            {
                if (std::wcsstr(nombre, buscado))
                    tid = h;
                LocalFree(nombre);
            }
            CloseHandle(consulta);
            if (tid != 0u)
                break;
        }
        if (tid == 0u)
        {
            std::fprintf(stderr, "No hay ningun hilo llamado \"%s\"\n", nombreHilo);
            return 1;
        }
        std::printf("Se muestrea el hilo %lu (%s)\n", tid, nombreHilo);
    }

    HANDLE hilo = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!hilo)
    {
        std::fprintf(stderr, "No se puede abrir el hilo %lu\n", tid);
        return 1;
    }

    std::vector<std::vector<DWORD64>> pilas;
    std::vector<double> tiempos; // ms desde el arranque del proceso, por muestra (la de pilas con el mismo indice)
    pilas.reserve(static_cast<size_t>(duracion * 1000.0 / intervalo) + 16u);
    timeBeginPeriod(1);
    const double t0 = segundosDesdeArranque(proceso);
    const double cpu0 = cpuTotalHilo(hilo);
    int intentos = 0;
    for (;;)
    {
        Sleep(static_cast<DWORD>(intervalo));
        const double t = segundosDesdeArranque(proceso);
        if (t >= desde + duracion)
            break;
        ++intentos;
        if (SuspendThread(hilo) == static_cast<DWORD>(-1))
            break;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        std::vector<DWORD64> pila;
        if (GetThreadContext(hilo, &ctx))
        {
            STACKFRAME64 marco{};
            marco.AddrPC.Offset = ctx.Rip;
            marco.AddrPC.Mode = AddrModeFlat;
            marco.AddrFrame.Offset = ctx.Rbp;
            marco.AddrFrame.Mode = AddrModeFlat;
            marco.AddrStack.Offset = ctx.Rsp;
            marco.AddrStack.Mode = AddrModeFlat;
            for (int i = 0; i < 48; ++i)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proceso, hilo, &marco, &ctx, nullptr,
                                 SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                    break;
                if (marco.AddrPC.Offset == 0)
                    break;
                pila.push_back(marco.AddrPC.Offset);
            }
        }
        ResumeThread(hilo);
        if (!pila.empty())
        {
            pilas.push_back(std::move(pila));
            tiempos.push_back(t * 1000.0);
        }
    }
    timeEndPeriod(1);
    const double t1 = segundosDesdeArranque(proceso);
    const double cpu1 = cpuTotalHilo(hilo);
    std::printf("Ventana t=%.1f-%.1f s: %zu muestras de %d intentos (intervalo pedido %d ms); CPU del hilo %.1f %% de un "
                "nucleo (GetThreadTimes, usuario+nucleo)\n",
                t0, t1, pilas.size(), intentos, intervalo, t1 > t0 ? 100.0 * (cpu1 - cpu0) / (t1 - t0) : 0.0);

    // Resolución de símbolos con caché.
    std::unordered_map<DWORD64, Simbolo> cache;
    auto simbolo = [&](DWORD64 dir) -> const Simbolo &
    {
        auto it = cache.find(dir);
        if (it != cache.end())
            return it->second;
        Simbolo s;
        char buf[sizeof(SYMBOL_INFO) + 512]{};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 desp = 0;
        if (SymFromAddr(proceso, dir, &desp, sym))
        {
            s.nombre = sym->Name;
            s.inicio = dir - desp;
        }
        else
        {
            // Sin simbolo (codigo generado, p. ej. el JIT de la VU1): todo en una entrada, para ver cuanto suma; la
            // direccion queda como desplazamiento en "Direcciones".
            s.nombre = "[sin simbolo]";
            s.inicio = 0;
        }
        return cache.emplace(dir, s).first->second;
    };

    std::map<std::string, int> exclusivo;
    std::map<std::string, int> inclusivo;
    std::map<std::string, int> lineas;
    std::map<std::string, int> lineasInternas; // la linea mas interna del codigo en linea (SymQueryInlineTrace)
    std::map<std::string, int> lineasMarcos;   // las lineas de todos los marcos en linea (inclusivo)
    std::map<std::string, int> numMarcos;
    std::map<std::string, int> llamadorDeHoja; // "hoja <- llamador"
    std::map<std::string, int> direcciones;    // "funcion+0xdesp" de la hoja (para cruzar con dumpbin /disasm)
    for (const auto &pila : pilas)
    {
        const Simbolo &hoja = simbolo(pila[0]);
        ++exclusivo[hoja.nombre];
        std::set<std::string> vistos;
        for (DWORD64 dir : pila)
        {
            const Simbolo &s = simbolo(dir);
            if (vistos.insert(s.nombre).second)
                ++inclusivo[s.nombre];
        }
        {
            char tmp[600];
            std::snprintf(tmp, sizeof(tmp), "%s+0x%llx", hoja.nombre.c_str(),
                          static_cast<unsigned long long>(pila[0] - hoja.inicio));
            ++direcciones[tmp];
        }
        if (pila.size() > 1)
            ++llamadorDeHoja[hoja.nombre + "  <-  " + simbolo(pila[1]).nombre];
        IMAGEHLP_LINE64 linea{};
        linea.SizeOfStruct = sizeof(linea);
        DWORD despLinea = 0;
        if (SymGetLineFromAddr64(proceso, pila[0], &despLinea, &linea))
        {
            const char *archivo = linea.FileName;
            if (const char *barra = std::strrchr(archivo, '\\'))
                archivo = barra + 1;
            char tmp[600];
            std::snprintf(tmp, sizeof(tmp), "%s:%lu  (%s)", archivo, linea.LineNumber, hoja.nombre.c_str());
            ++lineas[tmp];
        }
        // Codigo en linea: la linea y la funcion de cada marco en linea (contexto + k, del mas interno hacia fuera):
        // el mas interno en lineasInternas y todos, una vez por muestra, en lineasMarcos.
        const DWORD marcos = SymAddrIncludeInlineTrace(proceso, pila[0]);
        {
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%lu marcos en linea", marcos);
            ++numMarcos[tmp];
        }
        DWORD contexto = 0, indice = 0;
        if (marcos > 0 && SymQueryInlineTrace(proceso, pila[0], 0, pila[0], pila[0], &contexto, &indice))
        {
            std::set<std::string> vistosLinea;
            for (DWORD k = 0; k < marcos; ++k)
            {
                IMAGEHLP_LINE64 li{};
                li.SizeOfStruct = sizeof(li);
                DWORD dl = 0;
                char buf[sizeof(SYMBOL_INFO) + 512]{};
                auto *sym = reinterpret_cast<SYMBOL_INFO *>(buf);
                sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                sym->MaxNameLen = 511;
                DWORD64 ds = 0;
                const char *funcion = SymFromInlineContext(proceso, pila[0], contexto + k, &ds, sym) ? sym->Name : "?";
                if (!SymGetLineFromInlineContext(proceso, pila[0], contexto + k, 0, &dl, &li))
                    continue;
                const char *archivo = li.FileName;
                if (const char *barra = std::strrchr(archivo, '\\'))
                    archivo = barra + 1;
                char tmp[700];
                std::snprintf(tmp, sizeof(tmp), "%s:%lu  (%s)", archivo, li.LineNumber, funcion);
                if (k == 0)
                    ++lineasInternas[tmp];
                if (vistosLinea.insert(tmp).second)
                    ++lineasMarcos[tmp];
            }
        }
    }

    const double total = static_cast<double>(pilas.size());
    auto mostrar = [&](const char *titulo, const std::map<std::string, int> &tabla, size_t maximo)
    {
        std::vector<std::pair<int, std::string>> orden;
        for (const auto &[k, v] : tabla)
            orden.emplace_back(v, k);
        std::sort(orden.begin(), orden.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::printf("\n=== %s (%zu muestras)\n", titulo, pilas.size());
        for (size_t i = 0; i < orden.size() && i < maximo; ++i)
            std::printf("%6.2f %%  %5d  %s\n", 100.0 * orden[i].first / total, orden[i].first, orden[i].second.c_str());
    };
    mostrar("Exclusivo", exclusivo, 400);
    mostrar("Inclusivo", inclusivo, 45);
    mostrar("Hoja <- llamador", llamadorDeHoja, 30);
    mostrar("Lineas (exclusivo)", lineas, 60);
    mostrar("Lineas en linea (exclusivo; el marco en linea mas interno)", lineasInternas, 80);
    mostrar("Lineas en linea (inclusivo; todos los marcos en linea)", lineasMarcos, 120);
    mostrar("Marcos en linea por muestra", numMarcos, 12);
    mostrar("Direcciones (exclusivo; desplazamiento dentro de la funcion, como en dumpbin /disasm del .obj)",
            direcciones, 80);

    // PERFIL_PILAS=<texto>: las pilas completas (de la hoja hacia arriba, hasta 14 marcos) de las muestras con una
    // funcion cuyo nombre contiene ese texto, agrupadas y las 12 mas frecuentes.
    // Varios textos separados por '|': una tabla por texto.
    std::vector<std::string> textosPilas;
    if (const char *lista = std::getenv("PERFIL_PILAS"); lista != nullptr)
    {
        std::string resto = lista;
        for (size_t corte; (corte = resto.find('|')) != std::string::npos; resto = resto.substr(corte + 1))
            textosPilas.push_back(resto.substr(0, corte));
        textosPilas.push_back(resto);
    }
    for (const std::string &textoPila : textosPilas)
    {
        const char *buscar = textoPila.c_str();
        if (buscar[0] == '\0')
            continue;
        std::map<std::string, int> pilasCon;
        for (const auto &pila : pilas)
        {
            bool tiene = false;
            for (DWORD64 dir : pila)
                if (simbolo(dir).nombre.find(buscar) != std::string::npos)
                    tiene = true;
            if (!tiene)
                continue;
            std::string texto;
            for (size_t i = 0; i < pila.size() && i < 14u; ++i)
                texto += "\n           " + simbolo(pila[i]).nombre.substr(0, 150);
            ++pilasCon[texto];
        }
        mostrar((std::string("Pilas con ") + buscar).c_str(), pilasCon, 12);
    }

    // PERFIL_SERIE=<archivo>: cada muestra con su tiempo y su pila simbolizada (de la hoja hacia arriba).
    if (const char *serie = std::getenv("PERFIL_SERIE"); serie != nullptr && serie[0] != 0)
    {
        if (FILE *f = std::fopen(serie, "w"))
        {
            for (size_t n = 0; n < pilas.size(); ++n)
            {
                std::fprintf(f, "%.1f\t", tiempos[n]);
                for (size_t i = 0; i < pilas[n].size() && i < 24u; ++i)
                {
                    const Simbolo &s = simbolo(pilas[n][i]);
                    std::string nombre = s.nombre.substr(0, 120);
                    for (char &c : nombre)
                        if (c == ';' || c == '\t' || c == '\n')
                            c = ' ';
                    std::fprintf(f, "%s%s", i ? ";" : "", nombre.c_str());
                }
                std::fprintf(f, "\n");
            }
            std::fclose(f);
        }
    }

    // PERFIL_CRUDO=<archivo>: cada direccion sin simbolo en lo alto de la pila con sus muestras ("<dir hex> <n>"), para
    // repartirlas por pares de la VU con el mapa del JIT (PS2X_JIT_MAPA, scripts/jit_mapa.py).
    if (const char *crudo = std::getenv("PERFIL_CRUDO"); crudo != nullptr && crudo[0] != 0)
    {
        std::map<DWORD64, int> sinSimbolo;
        for (const auto &pila : pilas)
            if (simbolo(pila[0]).inicio == 0)
                ++sinSimbolo[pila[0]];
        if (FILE *f = std::fopen(crudo, "w"))
        {
            std::fprintf(f, "# %zu muestras\n", pilas.size());
            for (const auto &[dir, n] : sinSimbolo)
                std::fprintf(f, "%llx %d\n", static_cast<unsigned long long>(dir), n);
            std::fclose(f);
        }
    }

    CloseHandle(hilo);
    SymCleanup(proceso);
    CloseHandle(proceso);
    return 0;
}
