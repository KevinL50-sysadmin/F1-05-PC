"""rendimiento.md T2: reparte las muestras de perfil.exe (PERFIL_SERIE) del GameThread por categorias segun la pila.

Uso: python -I scripts/perfil/analizar.py <serie.txt> [--log captura-*.out.log] [--paso 1.0]

Cada muestra (tiempo de pared, ocupado o dormido) va a la PRIMERA categoria cuya condicion cumple su pila, en el orden
de CATS. Saca: reparto total; tramos ocupados entre esperas de inactividad (EeScheduler::waitForEvent = todos los hilos
del juego bloqueados hasta el proximo evento: VBLANK, temporizador o DMA), que equivalen a la carga de un fotograma, y su
reparto; serie por segundos (con imagenes/s de [ps2x:fotogramas] si se da --log); funciones del juego mas calientes.
"""
import re
import sys
from collections import Counter, defaultdict

ESPERA = r'^(ZwWaitForAlertByThreadId|NtWaitForAlertByThreadId|RtlSleepConditionVariableSRW|SleepConditionVariableSRW|' \
         r'RtlWaitOnAddress|WaitOnAddress|NtDelayExecution|ZwDelayExecution|SwitchToThread|NtYieldExecution|' \
         r'ZwYieldExecution|_Cnd_timedwait_for_impl|ZwWaitForSingleObject|NtWaitForSingleObject|' \
         r'ZwWaitForMultipleObjects)$'

def tiene(pila, patron):
    return any(re.search(patron, f) for f in pila)

# (nombre, funcion(pila, hoja) -> bool). La pila va de la hoja hacia arriba.
CATS = [
    ('inactivo: EeScheduler::waitForEvent (espera a VBLANK/evento)', lambda p, h: tiene(p, r'EeScheduler::waitForEvent')),
    ('inactivo: EeScheduler::processDueDeadlines (espera con plazo)',
     lambda p, h: tiene(p, r'EeScheduler::processDueDeadlines') and re.search(ESPERA, h)),
    ('espera al IOP (handOffBatch/iopSync dormido)',
     lambda p, h: tiene(p, r'PS2IopHostAdapter::(handOffBatch|iopSync)|waitWhile') and re.search(ESPERA, h)),
    ('espera al GS GIF (sync/waitForRoom)', lambda p, h: tiene(p, r'GifPacketThread::(sync|waitForRoom)')),
    ('otras esperas (dormido)', lambda p, h: re.search(ESPERA, h)),
    ('IOP: entrega/espera activa en el hilo del EE',
     lambda p, h: tiene(p, r'PS2IopHostAdapter::|waitWhile|IopEmulator|ps2x::iop::|advanceIop')),
    ('GIF: envio al hilo GS (submit, arbitro, copias)', lambda p, h: tiene(p, r'GifPacketThread::|GifArbiter|ps2GsGif')),
    ('VU1: codigo del JIT', lambda p, h: h == '[sin simbolo]'),
    ('VU1: interprete/planificador/contabilidad (no JIT)',
     lambda p, h: tiene(p, r'VU1Interpreter::|BlockHost|runBlocks|lowerFast|advanceIdle|VuRunClock|jitLower|jitUpper')),
    ('VIF1: DMA y UNPACK', lambda p, h: tiene(p, r'processVIF1Data|unpackFast|Vif1|vif1')),
    ('escritura de log (WriteFile/printf)', lambda p, h: tiene(p, r'^(NtWriteFile|WriteFile|fprintf|_write|__stdio_common)')),
    ('codigo EE traducido (hoja sub_*)', lambda p, h: re.match(r'sub_[0-9A-F]{8}_0x', h)),
    ('ayudas de memoria/registros del EE (llamadas desde sub_*)',
     lambda p, h: re.search(r'Ps2Fast|Ps2SetGpr|Ps2Extract|Ps2Get|isSpecialAddress|translateAddress|PS2Memory::(read|write)'
                            r'[0-9]+|isScratchpad|__security_check_cookie', h)),
    ('despacho de saltos/funciones del EE', lambda p, h: re.search(r'dispatchGuestBranch|lookupFunction|hasFunction', h)),
    ('registros IO / DMAC', lambda p, h: tiene(p[:3], r'writeIORegister|readIORegister|Dmac|dmac|drainCompleted')),
    ('planificador del EE (EeScheduler, sin esperar)', lambda p, h: tiene(p[:4], r'EeScheduler::')),
    ('cerrojos/monton', lambda p, h: re.search(r'SRWLock|Mtx_|Cnd_|RtlSizeHeap|RtlAllocateHeap|RtlFreeHeap|malloc|free', h)),
]
OTROS = 'otros'
INACTIVO = CATS[0][0]


def categoria(pila):
    hoja = pila[0]
    for nombre, cond in CATS:
        if cond(pila, hoja):
            return nombre
    return OTROS


def funcion_juego(pila):
    """La funcion del juego traducida mas interna de la pila (sub_*), o None."""
    for f in pila:
        if re.match(r'sub_[0-9A-F]{8}_0x', f):
            return f
    return None


def main():
    args = sys.argv[1:]
    log = None
    paso = 1.0
    ruta = None
    i = 0
    while i < len(args):
        if args[i] == '--log':
            log = args[i + 1]; i += 2
        elif args[i] == '--paso':
            paso = float(args[i + 1]); i += 2
        else:
            ruta = args[i]; i += 1
    muestras = []
    with open(ruta, encoding='utf-8', errors='replace') as f:
        for linea in f:
            t, _, resto = linea.rstrip('\n').partition('\t')
            if resto:
                pila = resto.split(';')
                muestras.append((float(t) / 1000.0, pila, categoria(pila)))
    n = len(muestras)
    t0, t1 = muestras[0][0], muestras[-1][0]
    intervalo = (t1 - t0) / max(1, n - 1)
    print(f'{ruta}: {n} muestras, t={t0:.1f}-{t1:.1f} s, una cada {intervalo * 1000:.2f} ms')

    total = Counter(c for _, _, c in muestras)
    ocupadas = n - total[INACTIVO]
    print('\n=== Reparto (% del tiempo de pared del hilo; "% ocup." = sin la inactividad de waitForEvent)')
    for c, k in total.most_common():
        oc = '' if c == INACTIVO else f'{100.0 * k / ocupadas:6.1f} % ocup.'
        print(f'{100.0 * k / n:6.1f} %  {oc:>14}  {k:5d}  {c}')

    # Tramos ocupados entre inactividades: la carga de cada fotograma del juego (resolucion = una muestra).
    tramos = []  # (inicio, duracion, Counter de categorias, ciclo hasta el siguiente tramo)
    actual = None
    for t, pila, c in muestras:
        if c == INACTIVO:
            if actual is not None:
                tramos.append(actual)
                actual = None
            continue
        if actual is None:
            actual = [t, t, Counter()]
        actual[1] = t
        actual[2][c] += 1
    if actual is not None:
        tramos.append(actual)
    durs = sorted((b - a + intervalo) * 1000.0 for a, b, _ in tramos)
    ciclos = [(tramos[k + 1][0] - tramos[k][0]) * 1000.0 for k in range(len(tramos) - 1)]
    if durs:
        q = lambda v, p: v[min(len(v) - 1, int(p * len(v)))]
        print(f'\n=== Tramos ocupados (entre esperas de waitForEvent): {len(durs)}; ms: mediana {q(durs, .5):.1f}, '
              f'p75 {q(durs, .75):.1f}, p90 {q(durs, .9):.1f}, max {durs[-1]:.1f}; '
              f'> 20 ms: {sum(d > 20.0 for d in durs)} ({100.0 * sum(d > 20.0 for d in durs) / len(durs):.0f} %)')
        hist = Counter(min(int(c // 10) * 10, 100) for c in ciclos)
        print('    ciclo (inicio a inicio de tramo, ms): ' +
              ' '.join(f'{k}-{k + 10 if k < 100 else ""}:{v}' for k, v in sorted(hist.items())))
        largos = Counter()
        cortos = Counter()
        for a, b, cnt in tramos:
            (largos if (b - a + intervalo) * 1000.0 > 20.0 else cortos).update(cnt)
        for titulo, cnt in (('tramos > 20 ms', largos), ('tramos <= 20 ms', cortos)):
            s = sum(cnt.values())
            if s:
                print(f'    reparto en {titulo} ({s} muestras): ' +
                      '; '.join(f'{c.split(" (")[0].split(":")[0] + (":" + c.split(":")[1].split(" (")[0] if ":" in c else "")} '
                                f'{100.0 * k / s:.0f} %' for c, k in cnt.most_common(8)))

    # Serie por segundos.
    fps = {}
    if log:
        with open(log, encoding='utf-8', errors='replace') as f:
            for linea in f:
                m = re.search(r'\[ps2x:fotogramas\] t=([0-9.]+) .*?imagenes/s=([0-9.]+)', linea)
                if m:
                    fps[int(float(m.group(1)))] = float(m.group(2))
    claves = ['inactivo', 'VU1: codigo del JIT', 'VU1: interprete', 'GIF', 'VIF1', 'espera al IOP', 'IOP:',
              'codigo EE', 'ayudas']
    porSeg = defaultdict(Counter)
    for t, _, c in muestras:
        porSeg[int(t // paso)][c] += 1
    print(f'\n=== Serie cada {paso:g} s (% de las muestras del segundo): img/s, ' + ', '.join(claves))
    for s in sorted(porSeg):
        cnt = porSeg[s]
        tot = sum(cnt.values())
        partes = []
        for clave in claves:
            k = sum(v for c, v in cnt.items() if c.startswith(clave) or (clave in ('GIF', 'VIF1', 'IOP:') and c.startswith(clave)))
            partes.append(f'{100.0 * k / tot:3.0f}')
        f = fps.get(int(s * paso))
        print(f'  t={s * paso:6.1f}  img/s={f if f is not None else "-":>5}  ' + ' '.join(partes))

    # Funciones del juego (sub_* mas interna en la pila: inclusivo, sin la inactividad).
    juego = Counter()
    for _, pila, c in muestras:
        if c == INACTIVO:
            continue
        f = funcion_juego(pila)
        if f:
            juego[f] += 1
    print('\n=== Funcion del juego mas interna de la pila (incluye la VU1/GIF/VIF1 que lance; % del tiempo de pared)')
    for f, k in juego.most_common(15):
        print(f'{100.0 * k / n:6.2f} %  {k:5d}  {f}')
    hojas = Counter(p[0] for _, p, c in muestras if c != INACTIVO)
    print('\n=== Hoja (exclusivo, sin la inactividad; % del tiempo de pared)')
    for f, k in hojas.most_common(25):
        print(f'{100.0 * k / n:6.2f} %  {k:5d}  {f[:110]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
