"""Resumen de una partida A/B (F1_AB, src/f1_overrides.cpp): modo B frente al modo 0 con media e intervalo del 95 %.

Uso: python -I scripts/ab/ab_resumen.py logs/rend-ab1/captura-X.out.log [--desde 195] [--hasta 470]

Lee las lineas "[f1-05:ab] t=... modo=M seg=... imagenes/s=... mciclos/s=... mciclosFot=..." (una al acabar cada
ventana; el modo es el que corrio en ella). Solo cuentan las ventanas enteras entre --desde y --hasta (segundos desde el
arranque; por defecto la carrera de scripts/ab/correr-ab.ps1: desde 195 s, tras el semaforo). La escena cambia con el
tiempo, asi que cada ventana B se compara con la media de sus dos vecinas del modo 0 (cociente; las dos tienen que
estar), y se da la media de los logaritmos de esos cocientes con su intervalo del 95 % (t de Student). Ventanas
alargadas (> 1,3 veces la mediana: el proceso se paro) se descartan con sus pares.
- mciclosFot: millones de ciclos del hilo del EE (QueryThreadCycleTime) por fotograma del juego: menos = mas barato.
  Sirve aunque el juego este a 50 (tope del VBLANK).
- imagenes/s: fotogramas del juego por segundo (cambios de DISPFB / 2): mas = mejor.
Una A/A (F1_AB_MODO=999) tiene que dar ~0 % con el intervalo cruzando el 0.
"""
import math
import re
import sys

LINEA = re.compile(r"\[f1-05:ab\] t=([\d.]+) modo=(-?\d+) seg=([\d.]+) imagenes/s=([\d.]+) mciclos/s=([\d.]+) "
                   r"mciclosFot=([\d.]+)")
# t de Student a dos colas, 95 %, por grados de libertad (>30: 1,96 aprox.)
T95 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 12: 2.18, 15: 2.13,
       20: 2.09, 25: 2.06, 30: 2.04}


def t95(gl):
    claves = [k for k in sorted(T95) if k <= gl]
    return T95[claves[-1]] if gl <= 30 else 1.96 + 2.4 / gl


def resumir(nombre, cocientes, mayor_es_mejor):
    logs = [math.log(c) for c in cocientes]
    n = len(logs)
    media = sum(logs) / n
    desv = math.sqrt(sum((x - media) ** 2 for x in logs) / (n - 1))
    medio = t95(n - 1) * desv / math.sqrt(n)
    pct = lambda x: (math.exp(x) - 1.0) * 100.0
    print(f"  {nombre}: B/A = {math.exp(media):.4f}  ({pct(media):+.2f} %, IC95 {pct(media - medio):+.2f} .. "
          f"{pct(media + medio):+.2f} %; {n} ventanas B; desv. tipica por ventana {desv * 100:.2f} %)")
    signo = 1.0 if mayor_es_mejor else -1.0
    if media - medio > 0.0 or media + medio < 0.0:
        print(f"    => B {'mejor' if signo * media > 0 else 'peor'} que A (fuera del IC)")
    else:
        print("    => sin diferencia dentro del IC")


def main():
    args = sys.argv[1:]
    desde, hasta = 195.0, math.inf
    for nombre in ("--desde", "--hasta"):
        if nombre in args:
            i = args.index(nombre)
            valor = float(args[i + 1])
            del args[i:i + 2]
            if nombre == "--desde":
                desde = valor
            else:
                hasta = valor
    if not args:
        print(__doc__)
        return 2
    texto = open(args[0], encoding="utf-8", errors="replace").read()
    todas = [(float(m.group(1)), int(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(6)))
             for m in LINEA.finditer(texto)]
    # (t final, modo, segundos, imagenes/s, mciclosFot); la ventana empieza en t - seg.
    ventanas = [v for v in todas if v[0] - v[2] >= desde and v[0] <= hasta]
    if len(ventanas) < 3:
        print(f"Pocas ventanas entre {desde} y {hasta} s ({len(ventanas)} de {len(todas)})")
        return 1
    nominal = sorted(v[2] for v in ventanas)[len(ventanas) // 2]
    larga = [v[2] > 1.3 * nominal for v in ventanas]
    print(f"{len(ventanas)} ventanas de ~{nominal:.1f} s entre {ventanas[0][0] - ventanas[0][2]:.0f} y "
          f"{ventanas[-1][0]:.0f} s; {sum(larga)} alargadas descartadas")
    for modo in sorted({v[1] for v in ventanas}):
        lista = [v for v in ventanas if v[1] == modo]
        print(f"  modo {modo}: {len(lista)} ventanas, imagenes/s media {sum(v[3] for v in lista) / len(lista):.2f}, "
              f"mciclosFot media {sum(v[4] for v in lista) / len(lista):.3f}")
    por_modo = {}
    for i, v in enumerate(ventanas):
        if v[1] == 0 or larga[i] or i == 0 or i + 1 >= len(ventanas):
            continue
        a, b = ventanas[i - 1], ventanas[i + 1]
        if a[1] != 0 or b[1] != 0 or larga[i - 1] or larga[i + 1]:
            continue
        ref_img, ref_cic = (a[3] + b[3]) / 2.0, (a[4] + b[4]) / 2.0
        if ref_img <= 0.0 or ref_cic <= 0.0 or v[3] <= 0.0 or v[4] <= 0.0:
            continue
        por_modo.setdefault(v[1], []).append((v[3] / ref_img, v[4] / ref_cic))
    for modo, pares in sorted(por_modo.items()):
        print(f"modo {modo} (B) frente al modo 0 (A):")
        if len(pares) < 2:
            print("  pocas ventanas")
            continue
        resumir("mciclosFot", [p[1] for p in pares], mayor_es_mejor=False)
        resumir("imagenes/s", [p[0] for p in pares], mayor_es_mejor=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
