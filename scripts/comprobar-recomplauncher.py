# Comprueba scripts/paquete/recomplauncher.json (fase 5 T3, odd/tasks/paquete.md; adaptado del de F1 06): el esquema
# oficial de RecompLauncher, que cada clave y valor de settings exista en ps2_options.cpp entre las opciones que
# F1 05 escribe (sin los extras de F1 06 que la fase 4 quita con PS2X_GAME_PC_EXTRAS 0, pcExtraOption) y que defaults
# sean los del jugador, los argumentos de prepare y start, que no haya rastro de los extras apagados (juego en red,
# fotos, mods) y (opcional) que un paquete hecho lleve el archivo junto a F1-05.exe.
# No descarga nada: el esquema es una copia de https://recomplauncher.com/schema/recomplauncher.json guardada en
# docs/recomplauncher/recomplauncher.schema.json (bajada el 2026-11-21; al actualizarla, volver a pasar esta prueba).
# Necesita jsonschema; sin instalarlo en el Python del sistema:
#   python -I -m venv build/rl-venv && build/rl-venv/Scripts/python.exe -I -m pip install jsonschema
# Uso: build/rl-venv/Scripts/python.exe -I scripts/comprobar-recomplauncher.py [--json <ruta>] [--esquema <ruta>]
#        [--paquete <carpeta con F1-05.exe>] [--zip <paquete.zip>]
# Sale con 0 si todo esta bien y con 1 si algo falla (cada fallo en una linea "FALLO: ...").
import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
OPCIONES_CPP = RAIZ / "third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_options.cpp"
IDENTIDAD_H = RAIZ / "third_party/PS2Recomp/ps2xRuntime/include/ps2_game_identity.h"
ESQUEMA = RAIZ / "docs/recomplauncher/recomplauncher.schema.json"
EXE = "F1-05.exe"
BOOT2 = "SCES_530.33"
CLAVES_BOTON = {"boton_acelerar", "boton_frenar", "boton_subir_marcha", "boton_bajar_marcha", "boton_mirar_atras"}
PREPARE_ARGS = ["--extraer-iso", "{game}"]
DONE = ["{install}/data/F1-05-disco.txt"]
# Redump 12930, "Formula One 05" Europe, Australia v1.02 (analisis/iso-hashes.txt tiene el tamano, MD5 y SHA-1).
DUMP = {"size": 3556409344, "crc32": "61ba3fcb"}
# Fase 4 (odd/tasks/sin-extras.md): sin juego en red, fotos ni mods, nada en el JSON debe apuntar a ellos.
RASTROS_EXTRAS = ("--invitado", "--anfitrion", "screenshots", "/mods", "red_", "tecla_foto", "modo_foto")

fallos = []


def fallo(texto):
    fallos.append(texto)
    print("FALLO: " + texto)


def opciones_del_juego(texto):
    """{clave: (valor por defecto del jugador, valores admitidos o None, minimo, maximo)} de optionSpecs()."""
    specs = {}
    # The value list is either inline ({{"0", "0"}, ...}) or a helper call (drivingButtonChoices()), read from its body.
    patron = re.compile(r'\{"(\w+)",\s*"PS2X_\w+",\s*"([^"]*)",\s*(?:\{(.*?)\}|(\w+)\(\)),\s*(-?\d+),\s*(-?\d+),',
                        re.S)
    for m in patron.finditer(texto):
        lista = m.group(3)
        if m.group(4):
            cuerpo = re.search(r'\b' + m.group(4) + r'\(\)\s*\{\s*return\s*\{(.*?)\};', texto, re.S)
            lista = cuerpo.group(1) if cuerpo else ""
        valores = re.findall(r'\{"([^"]+)",\s*(?:"[^"]*"|nullptr)\}', lista)
        specs[m.group(1)] = (m.group(2), valores or None, int(m.group(5)), int(m.group(6)))
    return specs


def extras_apagados(texto_opciones, texto_identidad):
    """Las claves que pcExtraOption() quita de optionSpecs() si PS2X_GAME_PC_EXTRAS es 0; si es 1, ninguna."""
    m = re.search(r'#define\s+PS2X_GAME_PC_EXTRAS\s+(\d+)', texto_identidad)
    if not m:
        fallo(f"no se encontro PS2X_GAME_PC_EXTRAS en {IDENTIDAD_H.name}")
        return set()
    if m.group(1) != "0":
        return set()
    lista = re.search(r'bool pcExtraOption\(.*?kExtras\[\]\s*=\s*\{(.*?)\};', texto_opciones, re.S)
    if not lista:
        fallo(f"no se encontro la lista kExtras de pcExtraOption() en {OPCIONES_CPP.name}")
        return set()
    return set(re.findall(r'"(\w+)"', lista.group(1)))


def valor_admitido(spec, valor):
    _, valores, minimo, maximo = spec
    if valores is not None:
        return valor in valores
    return valor.isdigit() and len(valor) <= 9 and minimo <= int(valor) <= maximo


def comprobar_esquema(datos, ruta_esquema):
    try:
        import jsonschema
    except ImportError:
        fallo("falta jsonschema (ver la cabecera de este script: venv en build/rl-venv)")
        return
    if not ruta_esquema.is_file():
        fallo(f"falta el esquema {ruta_esquema} (bajalo de https://recomplauncher.com/schema/recomplauncher.json)")
        return
    esquema = json.loads(ruta_esquema.read_text(encoding="utf-8"))
    validador = jsonschema.Draft7Validator(esquema)
    errores = sorted(validador.iter_errors(datos), key=lambda e: list(e.path))
    for e in errores:
        fallo("esquema: " + "/".join(str(p) for p in e.path) + ": " + e.message)
    if not errores:
        print(f"esquema: valido ({ruta_esquema.name}, draft-07)")


def comprobar_ajustes(datos):
    antes = len(fallos)
    texto = OPCIONES_CPP.read_text(encoding="utf-8")
    specs = opciones_del_juego(texto)
    if len(specs) < 20:
        fallo(f"solo se leyeron {len(specs)} opciones de {OPCIONES_CPP.name}: ¿cambio el formato de optionSpecs()?")
        return
    extras = extras_apagados(texto, IDENTIDAD_H.read_text(encoding="utf-8"))
    if extras - set(specs):
        fallo(f"pcExtraOption() nombra claves que no estan en optionSpecs(): {sorted(extras - set(specs))}")
    # Solo cuentan las opciones que F1 05 escribe en settings.ini: una clave de un extra apagado se ignora al leerla.
    specs = {clave: spec for clave, spec in specs.items() if clave not in extras}
    for ajuste in datos.get("settings", []):
        nombre = ajuste.get("id")
        if ajuste.get("in", "main") != "main":
            fallo(f"settings {nombre}: in = {ajuste.get('in')} (todo va en settings.ini, main)")
        for elegido in ajuste.get("choices", []):
            for clave, valor in elegido.get("set", {}).items():
                if clave not in specs:
                    fallo(f"settings {nombre}: la clave {clave} no esta en {OPCIONES_CPP.name}")
                elif not valor_admitido(specs[clave], valor):
                    fallo(f"settings {nombre}: {clave} = {valor} no es un valor admitido")
        for clave, valor in ajuste.get("defaults", {}).items():
            if clave not in specs:
                fallo(f"settings {nombre}: defaults con la clave desconocida {clave}")
            elif valor != specs[clave][0]:
                fallo(f"settings {nombre}: defaults {clave} = {valor}, pero el juego pone {specs[clave][0]} al jugador")
        for clave in ajuste.get("when", {}):
            if clave not in specs:
                fallo(f"settings {nombre}: when con la clave desconocida {clave}")
        # Driving buttons: the launcher writes each key on its own, so a choice that sets them must set all five
        # with no button used twice (the game would reset all of them to the original buttons).
        for elegido in ajuste.get("choices", []):
            botones = {c: v for c, v in elegido.get("set", {}).items() if c in CLAVES_BOTON}
            if botones and (set(botones) != CLAVES_BOTON or len(set(botones.values())) != len(botones)):
                fallo(f"settings {nombre}: {elegido.get('label')} debe poner las {len(CLAVES_BOTON)} acciones "
                      f"con botones distintos: {botones}")
        claves = {c for e in ajuste.get("choices", []) for c in e.get("set", {})}
        if claves - set(ajuste.get("defaults", {})):
            fallo(f"settings {nombre}: faltan en defaults {sorted(claves - set(ajuste.get('defaults', {})))}")
    for entrada in datos.get("config", []):
        for nombre in ("always", "new", "ensure"):
            for clave, valor in entrada.get(nombre, {}).items():
                if clave not in specs or not valor_admitido(specs[clave], valor):
                    fallo(f"config {nombre}: {clave} = {valor} no es una opcion valida del juego")
    if len(fallos) == antes:
        print(f"settings: {len(datos.get('settings', []))} ajustes bien contra {len(specs)} opciones de "
              f"{OPCIONES_CPP.name} (sin {len(extras)} extras apagados)")


def comprobar_arranque(datos):
    antes = len(fallos)
    prepare = datos.get("prepare", {})
    start = datos.get("start", {})
    if prepare.get("program") != [EXE] or start.get("program") != [EXE]:
        fallo(f"prepare.program y start.program deben ser [\"{EXE}\"]")
    if prepare.get("args") != PREPARE_ARGS:
        fallo(f"prepare.args = {prepare.get('args')}; el .exe solo admite {PREPARE_ARGS} (sin --ventana: sin ventana)")
    if prepare.get("done") != DONE:
        fallo(f"prepare.done = {prepare.get('done')}; debe ser {DONE}")
    if any("{game}" in a for a in start.get("args", [])):
        fallo("start.args lleva {game}: la ISO ganaria a data (main.cpp, findBootSource)")
    for nombre in start.get("env", {}):
        if nombre.upper().startswith("PS2X_"):
            fallo(f"start.env {nombre}: las PS2X_* mandan sobre settings.ini y bloquean el menu del juego")
    juego = datos.get("game", {})
    if juego.get("check", {}).get("iso9660") != BOOT2:
        fallo(f"game.check.iso9660 debe ser {BOOT2} (BOOT2 de SCES-53033)")
    volcados = juego.get("dumps", [])
    if not any(d.get("size") == DUMP["size"] and str(d.get("crc32", "")).lower() == DUMP["crc32"] for d in volcados):
        fallo(f"game.dumps no tiene el volcado de Redump {DUMP}")
    if "exit_codes" in datos:
        fallo("exit_codes: F1 05 no los lleva (odd/tasks/paquete.md L2)")
    texto = json.dumps(datos, ensure_ascii=False)
    for rastro in RASTROS_EXTRAS:
        if rastro in texto:
            fallo(f"el JSON nombra {rastro}: es de un extra de F1 06 apagado en F1 05 (fase 4)")
    if len(fallos) == antes:
        print("prepare/start/game: argumentos, disco y volcado correctos")


def comprobar_paquete(fuente, carpeta):
    exe, json_paquete = carpeta / EXE, carpeta / "recomplauncher.json"
    if not exe.is_file():
        fallo(f"paquete: falta {exe}")
    if not json_paquete.is_file():
        fallo(f"paquete: falta {json_paquete} junto a {EXE}")
    elif json_paquete.read_bytes() != fuente.read_bytes():
        fallo(f"paquete: {json_paquete} no es igual que {fuente}")
    else:
        print(f"paquete: {json_paquete} junto a {EXE}, igual que la fuente")


def comprobar_zip(fuente, ruta_zip):
    with zipfile.ZipFile(ruta_zip) as z:
        nombres = set(z.namelist())
        exes = [n for n in nombres if n.endswith("/" + EXE) or n == EXE]
        if len(exes) != 1:
            fallo(f"zip: {len(exes)} {EXE} en {ruta_zip}")
            return
        base = exes[0][: -len(EXE)]
        # El paquete publico (-SinDatos) no lleva nada del juego, ni el PDB, ni los accesos o carpetas de los extras.
        prohibidos = sorted(n for n in nombres if n.startswith((base + "data/", base + "mods/")) or n.endswith(".pdb")
                            or n.endswith(".cmd"))
        if prohibidos:
            fallo(f"zip: lleva lo que no debe (data, mods, .pdb o .cmd): {prohibidos[:5]}")
        entrada = base + "recomplauncher.json"
        if entrada not in nombres:
            fallo(f"zip: falta {entrada} junto a {exes[0]}")
        elif z.read(entrada) != fuente.read_bytes():
            fallo(f"zip: {entrada} no es igual que {fuente}")
        else:
            print(f"zip: {entrada} junto a {exes[0]}, igual que la fuente")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--json", type=Path, default=RAIZ / "scripts/paquete/recomplauncher.json")
    p.add_argument("--esquema", type=Path, default=ESQUEMA)
    p.add_argument("--paquete", type=Path)
    p.add_argument("--zip", type=Path)
    a = p.parse_args()
    datos = json.loads(a.json.read_text(encoding="utf-8"))
    comprobar_esquema(datos, a.esquema)
    comprobar_ajustes(datos)
    comprobar_arranque(datos)
    if a.paquete:
        comprobar_paquete(a.json, a.paquete)
    if a.zip:
        comprobar_zip(a.json, a.zip)
    print("RESULTADO: " + ("bien" if not fallos else f"{len(fallos)} fallos"))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
