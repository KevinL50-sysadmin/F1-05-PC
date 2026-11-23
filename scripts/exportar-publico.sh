#!/usr/bin/env bash
# Exporta la parte publica del proyecto a dist/publico-repo, la copia de trabajo (clon) del repositorio publico
# F1-05-PC (este repositorio sigue privado). Adaptado de scripts/exportar-publico.sh de F1 06, con las mismas
# comprobaciones. Solo sale una lista blanca revisada de archivos del repositorio (nunca lo que .gitignore deja fuera):
# archivo a archivo los .cmd de la raiz, .gitignore, .gitattributes, LICENSE, CHANGELOG.md, config/recomp.toml,
# gpu/pgs_puente/, scripts/ y docs/recomplauncher/developer-docs.md; por tipo los .cpp y .h de src/ y los .patch de
# parches/. Diferencia con F1 06: el README.md de la raiz de este repositorio es el de trabajo; el publico es
# docs/publico/README.md, que sale como README.md, y sus capturas (docs/publico/img/*.png) salen como docs/img/*.png
# (las rutas que ya tenia el repositorio publico). Para el servidor de Discord salen el workflow
# .github/workflows/discord.yml, la copia de sus herramientas (tools/discord/sync.py, discord_api.py y juegos.json:
# lo unico de tools/ que se publica) y sus textos (discord/known-issues.md y discord/anuncios/*.md).
# Se quedan fuera las notas de trabajo (odd/, NOTAS.md, ROADMAP.md, analisis/, el README.md de trabajo), las notas de
# las releases (docs/releases/: van en la release de GitHub), el esquema de RecompLauncher
# (docs/recomplauncher/recomplauncher.schema.json: es de RecompLauncher, como en F1 06 no se publica),
# scripts/cerrojo-pc.sh (coordina el PC del autor entre sus sesiones de trabajo), scripts/pruebas/fallos-t4.ps1
# (fallos provocados en el PC del autor), scripts/exportar-personales.txt (patrones privados de datos personales, si
# existe), .git-blame-ignore-revs y, por supuesto, cualquier dato del juego (ISO, juego/, el ELF, el codigo C++ que
# PS2Recomp genera a partir de el, el logo del juego y su icono). Un archivo nuevo en src/, parches/, gpu/, config/,
# scripts/, docs/, .github/, discord/ o tools/discord/ que no este en la lista ni entre los privados conocidos hace fallar la exportacion: hay
# que revisarlo y anadirlo a una de las dos listas de abajo.
#
# La exportacion se monta en una carpeta temporal y se comprueba; falla (codigo 1, sin tocar dist/publico-repo) si
# encuentra archivos sin revisar, rutas excluidas, datos personales (tambien en UTF-16 y en el texto de los binarios,
# como los metadatos tEXt de un PNG; el texto comprimido de un PNG, zTXt/iTXt, no se mira), archivos de mas de 5 MB o
# datos del juego. Si todo esta bien sustituye el contenido de dist/publico-repo entero (sin restos de archivos
# borrados o renombrados) y conserva su .git, asi cada version es un commit nuevo encima del anterior.
#
# Despues comprueba la identidad con la que se hara el commit publico y falla (codigo 2, con la exportacion ya
# copiada) si no se ha elegido a proposito o si algun commit de dist/publico-repo tiene otro autor. Se elige con
#   F1_PUBLICO_NOMBRE="Nombre" F1_PUBLICO_EMAIL="<id>+<usuario>@users.noreply.github.com" bash scripts/exportar-publico.sh
# (el correo noreply esta en GitHub, Settings > Emails); se guarda en dist/publico-repo/.git/config y en las siguientes
# exportaciones basta si es un correo noreply de GitHub. No hace commit ni push: eso se hace a mano en
# dist/publico-repo, y solo con permiso del usuario.
#
# Preparacion (una vez): git clone https://github.com/KevinL50-sysadmin/F1-05-PC dist/publico-repo
# Uso (desde la raiz del proyecto, en Git Bash):
#   bash scripts/exportar-publico.sh             # exporta lo que hay en HEAD (git archive: solo lo que tiene commit)
#   bash scripts/exportar-publico.sh --trabajo   # exporta la carpeta tal como quedaria tras el proximo commit
#                                                # (cambios sin commit y archivos nuevos no ignorados incluidos)
set -euo pipefail

raiz="$(cd "$(dirname "$0")/.." && pwd)"
destino="$raiz/dist/publico-repo"

origen=HEAD
case "${1:-}" in
    "") ;;
    --trabajo) origen="carpeta de trabajo" ;;
    *) echo "Uso: bash scripts/exportar-publico.sh [--trabajo]" >&2; exit 1 ;;
esac

# Lista blanca revisada, archivo a archivo. Un archivo de la lista que no exista no es un fallo.
revisados=(
    .gitignore .gitattributes CHANGELOG.md LICENSE .github/FUNDING.yml .github/workflows/discord.yml
    compilar-ps2recomp.cmd compilar-runner.cmd ejecutar.cmd entorno.cmd recompilar.cmd
    config/recomp.toml
    gpu/pgs_puente/CMakeLists.txt gpu/pgs_puente/LICENSE.txt gpu/pgs_puente/compilar.cmd
    gpu/pgs_puente/compose_circuits.comp gpu/pgs_puente/pgs_puente.cpp gpu/pgs_puente/pgs_puente.h
    gpu/pgs_puente/pgs_readback_ring.h
    docs/publico/README.md docs/publico/img/quick-race-menu.png docs/publico/img/race-cockpit.png
    docs/recomplauncher/developer-docs.md
    scripts/ab/ab_resumen.py scripts/ab/correr-ab.ps1 scripts/capturar-ventana.ps1
    scripts/comprobar-recomplauncher.py scripts/elf_ps2.py scripts/empaquetar.ps1 scripts/exportar-publico.sh
    scripts/hacer-icono.ps1 scripts/hacer-parche.sh scripts/instalador/F1-05.iss scripts/iso_ps2.py
    scripts/medir-hilos.ps1 scripts/obtener-parallel-gs.ps1 scripts/paquete/recomplauncher.json
    scripts/paquete/version.txt scripts/perfil/analizar.py scripts/perfil/correr.ps1 scripts/perfil/jit_mapa.py
    scripts/perfil/perfil.cpp scripts/perfil/vu_dis.py scripts/simbolizar.ps1
    tools/discord/sync.py tools/discord/discord_api.py tools/discord/juegos.json
    discord/known-issues.md
)
declare -A es_revisado
for f in "${revisados[@]}"; do es_revisado[$f]=1; done

# Deja en $clase si un archivo del repositorio es publico, privado, sin revisar (en una carpeta publicada pero en
# ninguna lista) o queda fuera sin mas (el resto del repositorio).
clasificar() {
    if [ -n "${es_revisado[$1]:-}" ]; then clase=publico; return; fi
    case "$1" in
        src/*.cpp | src/*.h | parches/*.patch | discord/anuncios/*.md) clase=publico ;;
        # cerrojo-pc.sh coordina el PC del autor entre sus sesiones de trabajo; fallos-t4.ps1 son pruebas del autor
        # en su PC (fallos provocados); exportar-personales.txt son patrones privados: privados.
        scripts/cerrojo-pc.sh | scripts/pruebas/fallos-t4.ps1 | scripts/exportar-personales.txt) clase=privado ;;
        # Notas de las releases (van en la release de GitHub) y el esquema de RecompLauncher (es suyo).
        docs/releases/* | docs/recomplauncher/recomplauncher.schema.json) clase=privado ;;
        src/* | parches/* | gpu/* | config/* | scripts/* | docs/* | .github/* | discord/* | tools/discord/*)
            clase=sin-revisar ;;
        # Un archivo nuevo en la raiz (p. ej. otro .cmd) hay que revisarlo, no dejarlo fuera sin avisar.
        */*) clase=fuera ;;
        # El README.md de la raiz es el de trabajo: el publico es docs/publico/README.md.
        *.md | ROADMAP* | NOTAS* | .git-blame-ignore-revs) clase=fuera ;;
        *) clase=sin-revisar ;;
    esac
}

case "$destino" in
    "$raiz"/dist/publico-repo) ;;
    *) echo "Destino inesperado: $destino" >&2; exit 1 ;;
esac
mkdir -p "$raiz/dist"
temporal="$(mktemp -d "$raiz/dist/.exportando-XXXXXX")"
trap 'rm -rf "$temporal"' EXIT
arbol="$temporal/arbol"
mkdir "$arbol"

fallos=0
fallo() { echo "FALLO: $*" >&2; fallos=$((fallos + 1)); }

# Listas de git separadas por NUL: los nombres con espacios o no ASCII no salen entre comillas.
git -C "$raiz" ls-tree -r -z --name-only HEAD > "$temporal/en-head"
declare -A en_head cambiado
while IFS= read -r -d '' f; do en_head[$f]=1; done < "$temporal/en-head"
if [ "$origen" = HEAD ]; then
    cp "$temporal/en-head" "$temporal/archivos"
else
    # Lo que quedaria tras el proximo commit: el indice y los nuevos sin ignorar, menos lo borrado en la carpeta. Los
    # que no cambian salen de HEAD tal cual (algunas copias de trabajo tienen CRLF y el commit no).
    git -C "$raiz" ls-files -z --cached --others --exclude-standard > "$temporal/archivos"
    git -C "$raiz" diff -z --name-only --no-renames HEAD > "$temporal/cambiados"
    while IFS= read -r -d '' f; do cambiado[$f]=1; done < "$temporal/cambiados"
fi

de_head=()
de_carpeta=()
sin_revisar=()
while IFS= read -r -d '' f; do
    if [ "$origen" != HEAD ] && [ ! -f "$raiz/$f" ]; then continue; fi
    clasificar "$f"
    case "$clase" in
        publico)
            if [ "$origen" = HEAD ] || { [ -n "${en_head[$f]:-}" ] && [ -z "${cambiado[$f]:-}" ]; }; then
                de_head+=("$f")
            else
                de_carpeta+=("$f")
            fi ;;
        sin-revisar) sin_revisar+=("$f") ;;
    esac
done < "$temporal/archivos"

if [ "${#de_head[@]}" -gt 0 ]; then
    # Por tandas (xargs) para no pasar del limite de la linea de ordenes de Windows.
    printf '%s\0' "${de_head[@]}" | xargs -0 bash -c 'set -o pipefail; arbol=$1; shift
        git -C "$0" --literal-pathspecs archive --format=tar HEAD -- "$@" | tar -x -C "$arbol"' "$raiz" "$arbol" \
        || { echo "No se pudo extraer HEAD" >&2; exit 1; }
fi
for f in ${de_carpeta[@]+"${de_carpeta[@]}"}; do
    case "$f" in */*) mkdir -p "$arbol/${f%/*}" ;; esac
    cp -- "$raiz/$f" "$arbol/$f"
done
for f in ${sin_revisar[@]+"${sin_revisar[@]}"}; do
    fallo "archivo sin revisar (anadelo a la lista blanca o a los privados de scripts/exportar-publico.sh): $f"
done

# Comprobaciones, sobre la carpeta temporal.
cd "$arbol"

# El README publico y sus capturas, a las rutas del repositorio publico.
if [ -f docs/publico/README.md ]; then
    mv docs/publico/README.md README.md
else
    fallo "falta docs/publico/README.md (el README publico)"
fi
if [ -d docs/publico/img ]; then
    mkdir -p docs/img
    find docs/publico/img -mindepth 1 -maxdepth 1 -type f -exec mv -t docs/img {} +
fi
rmdir docs/publico/img docs/publico 2>/dev/null || true
[ ! -e docs/publico ] || fallo "quedan archivos en docs/publico sin ruta publica"

excluidas=(odd NOTAS.md ROADMAP.md analisis .git-blame-ignore-revs logs dist build third_party juego mc0 mc1
           datos data saves cache docs/releases docs/recomplauncher/recomplauncher.schema.json
           scripts/cerrojo-pc.sh scripts/pruebas scripts/exportar-personales.txt)
for ruta in "${excluidas[@]}"; do
    [ ! -e "$ruta" ] || fallo "ruta excluida presente: $ruta"
done
# tools/ (herramientas portables) tambien esta excluida, salvo la copia de las herramientas de Discord.
if [ -e tools ]; then
    otras_tools="$(find tools -mindepth 1 ! -path tools/discord ! -path tools/discord/sync.py \
        ! -path tools/discord/discord_api.py ! -path tools/discord/juegos.json -print)"
    [ -z "$otras_tools" ] || fallo "ruta excluida presente en tools/:"$'\n'"$otras_tools"
fi

# Datos personales, sin distinguir mayusculas: rutas de la unidad J: y de la carpeta del usuario de Windows, su nombre,
# su correo y las carpetas del proyecto y de la ISO. Se permiten el enlace a los repositorios publicos
# (github.com/<cuenta>/...), los perfiles publicos de Ko-fi y X del autor (tambien el ko_fi de .github/FUNDING.yml)
# y las carpetas de usuario genericas (Users/Public y Users/Default). Se anaden los patrones del archivo privado
# scripts/exportar-personales.txt si existe (una ERE por linea, sin los espacios del final; # y lineas en blanco no
# cuentan): asi los datos que no deben salir ni como patron no estan en este script, que se publica. Un patron no
# valido para grep -E hace fallar la exportacion. Con -i no siempre se igualan mayusculas fuera del ASCII: una letra
# acentuada se escribe alli en las dos formas (en los binarios se busca byte a byte: el texto debe ir en UTF-8).
# Los corchetes evitan que este mismo script coincida con sus patrones.
personales='J:[\\/]|[a-z]:[\\/]+users[\\/]+[a-z0-9._-]{2,}|K[e]vin|@[g]mail|IAs[\\/]+Claude|(Descargas|Downloads)[\\/]+Formula'
permitidos='s#github\.com/K[e]vinL50-sysadmin/##gI; s#ko-fi\.com/k[e]vinl50##gI; s#(recompstore|recomplauncher)\.com/dev/k[e]vinl50-sysadmin\b##gI; s#ko_fi: k[e]vinl50##gI; s#(x|twitter)\.com/K[e]vinLA50##gI; s#@K[e]vinLA50##gI; s#users[\\/]+(public|default)\b##gI'
# En los binarios solo los patrones largos (los cortos darian falsos positivos en datos comprimidos).
personales_binario='K[e]vin|@[g]mail|IAs[\\/]+Claude|(Descargas|Downloads)[\\/]+Formula|[a-z]:[\\/]+users[\\/]+k[e]vin'
# grep sale con 2 si el patron no vale: no se puede tomar por "sin coincidencias".
patron_valido() { local r=0; printf '' | grep -E -e "$1" || r=$?; [ "$r" -le 1 ]; }
if [ -f "$raiz/scripts/exportar-personales.txt" ]; then
    linea=0
    while IFS= read -r patron || [ -n "$patron" ]; do
        linea=$((linea + 1))
        patron="${patron%$'\r'}"
        patron="${patron%"${patron##*[![:space:]]}"}"
        case "$patron" in "" | "#"*) continue ;; esac
        if ! patron_valido "($patron)" 2>/dev/null; then
            echo "FALLO: patron no valido para grep -E en scripts/exportar-personales.txt, linea $linea." >&2
            echo "Exportacion NO valida. dist/publico-repo no se ha tocado." >&2
            exit 1
        fi
        personales+="|($patron)"
        personales_binario+="|($patron)"
    done < "$raiz/scripts/exportar-personales.txt"
fi
for p in "$personales" "$personales_binario"; do
    patron_valido "$p" || { echo "FALLO: patron de datos personales no valido" >&2; exit 1; }
done
buscar_personales() { sed -E "$permitidos" | grep -i -E "$personales"; }
# grep_personales <salida> <grep...>: deja las lineas en <salida>; un 2 de grep (no pudo leer) es un fallo, no "limpio".
grep_personales() {
    local salida=$1 r=0
    shift
    "$@" > "$salida" || r=$?
    [ "$r" -le 1 ] || fallo "grep no pudo buscar datos personales (codigo $r): ${*: -1}"
}

grep_personales "$temporal/coinciden" grep -r -I -n -i -E "$personales" .
coincidencias="$(buscar_personales < "$temporal/coinciden" || true)"
if [ -n "$coincidencias" ]; then
    fallo "datos personales en archivos de texto:"
    echo "$coincidencias" >&2
fi
# Los que grep toma por binarios: texto en UTF-16 (con o sin BOM, .ps1/.iss/.rc guardados asi) o binarios de verdad.
while IFS= read -r -d '' archivo; do
    [ -s "$archivo" ] || continue
    bom="$(head -c 2 "$archivo" | od -An -tx1 | tr -d ' \n')"
    case "$bom" in
        fffe | feff) codificacion=UTF-16 ;;
        *) codificacion=UTF-16LE ;;
    esac
    if iconv -f "$codificacion" -t UTF-8 "$archivo" > "$temporal/texto" 2>/dev/null && grep -I -q . "$temporal/texto"; then
        grep_personales "$temporal/coinciden" grep -n -i -E "$personales" "$temporal/texto"
        coincidencias="$(buscar_personales < "$temporal/coinciden" || true)"
        if [ -n "$coincidencias" ]; then
            fallo "datos personales en texto UTF-16: $archivo"
            echo "$coincidencias" >&2
        fi
    else
        grep_personales "$temporal/coinciden" grep -a -c -i -E "$personales_binario" "$archivo"
        tr -d '\000' < "$archivo" > "$temporal/sin-nul"
        grep_personales "$temporal/coinciden-sin-nul" grep -a -c -i -E "$personales_binario" "$temporal/sin-nul"
        if [ "$(cat "$temporal/coinciden")" != 0 ] || [ "$(cat "$temporal/coinciden-sin-nul")" != 0 ]; then
            fallo "datos personales en binario: $archivo"
        fi
    fi
done < <(grep -r -I -L -Z . . || true)

grandes="$(find . -type f -size +5242880c -print)"
[ -z "$grandes" ] || fallo "archivos de mas de 5 MB:"$'\n'"$grandes"

# Datos del juego por nombre: imagenes de disco, el ELF (SCES_530.33) y otros ejecutables o modulos de la PS2, videos,
# tarjetas de memoria, carpetas de datos, claves y el icono hecho con el logo del juego (.ico).
juego="$(find . \( -iname '*.iso' -o -iname '*.wad' -o -iname '*.elf' -o -iname '*.irx' -o -iname '*.pss' \
    -o -iname '*.bin' -o -iname '*.ico' -o -iname 'SCES_*' -o -iname 'prod.keys' -o -iname 'mc0' -o -iname 'mc1' \
    -o -iname 'datos' -o -iname 'data' -o -iname 'juego' \) -print)"
[ -z "$juego" ] || fallo "datos del juego:"$'\n'"$juego"
# El codigo que PS2Recomp genera a partir del ELF (ps2_recompiled_*, src/runner) nunca se publica.
generado="$(find . \( -iname 'ps2_recompiled_*' -o -path '*/src/runner/*' \) -print)"
[ -z "$generado" ] || fallo "codigo generado a partir del ELF del juego:"$'\n'"$generado"
# Imagenes: solo las capturas revisadas de docs/img.
imagenes="$(find . -type f \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.bmp' -o -iname '*.gif' \
    -o -iname '*.webp' \) ! -path './docs/img/*' -print)"
[ -z "$imagenes" ] || fallo "imagenes fuera de docs/img:"$'\n'"$imagenes"

privados='NOTAS\.md|ROADMAP\.md|odd[/\\]|analisis[/\\]'
if [ -f README.md ] && grep -n -E "$privados" README.md >&2; then fallo "README.md nombra archivos privados"; fi
menciones="$(grep -r -I -l -E "$privados" . | grep -v '^\./README\.md$' || true)"
if [ -n "$menciones" ]; then
    echo "Aviso: comentarios que nombran notas privadas (no se publican; no es un fallo):" >&2
    echo "$menciones" | sed 's/^/  /' >&2
fi

archivos="$(find . -type f | wc -l)"
tamano="$(du -sh . | cut -f1)"
echo "Archivos: $archivos; tamano: $tamano"
if [ "$fallos" -gt 0 ]; then
    echo "Exportacion NO valida: $fallos fallo(s). dist/publico-repo no se ha tocado." >&2
    exit 1
fi

# Sustituir el contenido de la exportacion anterior, menos su .git.
cd "$raiz"
mkdir -p "$destino"
find "$destino" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
find "$arbol" -mindepth 1 -maxdepth 1 -exec mv -t "$destino" {} +
echo "Exportado ($origen) a $destino"
echo "Comprobaciones superadas."

# Identidad del commit publico: nunca la del repositorio privado sin mas.
nombre="${F1_PUBLICO_NOMBRE:-}"
correo="${F1_PUBLICO_EMAIL:-}"
identidad=0
como="Elige la identidad publica: F1_PUBLICO_NOMBRE=\"Nombre\" F1_PUBLICO_EMAIL=\"<id>+<usuario>@users.noreply.github.com\" bash scripts/exportar-publico.sh (el correo noreply esta en GitHub, Settings > Emails)."
if [ -n "$nombre$correo" ] && { [ -z "$nombre" ] || [ -z "$correo" ]; }; then
    echo "FALLO: hay que dar F1_PUBLICO_NOMBRE y F1_PUBLICO_EMAIL juntos." >&2
    identidad=1
elif [ -n "$nombre" ]; then
    if [ -d "$destino/.git" ]; then
        git -C "$destino" config --local user.name "$nombre"
        git -C "$destino" config --local user.email "$correo"
    fi
elif [ -d "$destino/.git" ]; then
    nombre="$(git -C "$destino" config --local user.name || true)"
    correo="$(git -C "$destino" config --local user.email || true)"
    case "$correo" in
        *@users.noreply.github.com) ;;
        *)
            echo "FALLO: la identidad de dist/publico-repo/.git (\"$nombre <$correo>\") no es un correo noreply de GitHub" \
                 "ni se ha elegido con F1_PUBLICO_NOMBRE/F1_PUBLICO_EMAIL; puede ser la copiada del repositorio privado." >&2
            identidad=1 ;;
    esac
else
    echo "FALLO: falta la identidad del commit publico (F1_PUBLICO_NOMBRE y F1_PUBLICO_EMAIL)." >&2
    identidad=1
fi
if [ "$identidad" = 0 ]; then
    echo "Identidad del repositorio publico: $nombre <$correo>"
    if [ ! -d "$destino/.git" ]; then
        echo "Aviso: dist/publico-repo aun no tiene .git; clona antes el repositorio publico y vuelve a exportar." >&2
    elif git -C "$destino" rev-parse -q --verify HEAD > /dev/null; then
        otros=""
        while IFS=$'\t' read -r h an ae cn ce; do
            if [ "$an" != "$nombre" ] || [ "$ae" != "$correo" ] || [ "$cn" != "$nombre" ] || [ "$ce" != "$correo" ]; then
                otros+="  $h autor $an <$ae>, committer $cn <$ce>"$'\n'
            fi
        done < <(git -C "$destino" log --format='%h%x09%an%x09%ae%x09%cn%x09%ce')
        if [ -n "$otros" ]; then
            echo "FALLO: commits de dist/publico-repo con otra identidad:" >&2
            printf '%s' "$otros" >&2
            echo "Hay que decidir antes de subir: si ya estan en GitHub, cambiarlos obliga a reescribir la historia publica" \
                 "(push --force); si no, se corrige con git -C dist/publico-repo commit --amend --reset-author --no-edit." >&2
            identidad=1
        fi
    fi
fi
if [ "$identidad" != 0 ]; then
    echo "$como" >&2
    echo "La exportacion es valida y esta copiada, pero no hagas commit ni push hasta arreglar la identidad." >&2
    exit 2
fi
