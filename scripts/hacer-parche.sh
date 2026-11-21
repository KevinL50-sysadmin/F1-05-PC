#!/usr/bin/env bash
# Genera un parche nuevo para parches/ con los cambios actuales de unos archivos de third_party/PS2Recomp, medidos
# contra el arbol con todos los parches anteriores aplicados (asi no se mezclan bloques de otros parches que tocan
# los mismos archivos). Al final comprueba que base + todos los parches = arbol real.
#
# Uso (desde la raiz del proyecto, en Git Bash):
#   bash scripts/hacer-parche.sh 0016-nombre-del-parche ruta/relativa/a/PS2Recomp/archivo1 [archivo2 ...]
#   bash scripts/hacer-parche.sh --comprobar      solo la comprobacion final, sin crear ningun parche
# Las rutas son relativas a third_party/PS2Recomp (p. ej. ps2xRuntime/src/lib/ps2_memory.cpp).
set -euo pipefail

solo_comprobar=0
nombre=""
if [ "${1:-}" = "--comprobar" ]; then
    solo_comprobar=1
else
    nombre="$1"
fi
shift
raiz="$(cd "$(dirname "$0")/.." && pwd)"
repo="$raiz/third_party/PS2Recomp"
parches="$raiz/parches"
destino="$parches/$nombre.patch"
temporal="$(mktemp -d)/ps2r-base"

if [ "$solo_comprobar" = 0 ] && [ -e "$destino" ]; then
    echo "Ya existe $destino" >&2
    exit 1
fi

git -C "$repo" worktree add --detach "$temporal" HEAD >/dev/null 2>&1
limpiar() { git -C "$repo" worktree remove --force "$temporal" >/dev/null 2>&1 || true; git -C "$repo" worktree prune; }
trap limpiar EXIT

# Parches anteriores, en orden.
for p in "$parches"/[0-9][0-9][0-9][0-9]-*.patch; do
    git -C "$temporal" apply --whitespace=nowarn "$p" || { echo "No se aplica $p" >&2; exit 1; }
done
git -C "$temporal" add -A >/dev/null 2>&1

# Comprobacion: base + todos los parches debe ser igual al arbol real. Se compara archivo a archivo (los cambiados en
# cualquiera de los dos arboles, tambien los nuevos sin seguir de PS2Recomp, que `git diff HEAD` no ve), sin tener en
# cuenta CRLF/LF y sin el codigo generado (ps2xRuntime/src/runner: register_functions.cpp, las copias de src/...).
comprobar() {
    local f mal=0
    while IFS= read -r f; do
        case "$f" in ps2xRuntime/src/runner/*) continue ;; esac
        if [ -f "$temporal/$f" ] && [ -f "$repo/$f" ]; then
            cmp -s <(tr -d '\r' < "$temporal/$f") <(tr -d '\r' < "$repo/$f") && continue
        elif [ ! -e "$temporal/$f" ] && [ ! -e "$repo/$f" ]; then
            continue
        fi
        echo "  distinto: $f" >&2
        mal=1
    done < <({
        git -C "$temporal" -c core.quotepath=off diff --cached --name-only --no-renames HEAD
        git -C "$repo" -c core.quotepath=off diff --name-only --no-renames HEAD
        git -C "$repo" -c core.quotepath=off ls-files --others --exclude-standard
    } | sort -u)
    return $mal
}
if [ "$solo_comprobar" = 1 ]; then
    if comprobar; then echo "Comprobado: base + parches = arbol real"; exit 0; fi
    echo "AVISO: base + parches no coincide con el arbol real (archivos de arriba)" >&2
    exit 2
fi

for f in "$@"; do
    if [ ! -e "$repo/$f" ]; then
        rm -f "$temporal/$f" # borrado en el arbol real: el parche lo borra
        continue
    fi
    mkdir -p "$(dirname "$temporal/$f")"
    cp "$repo/$f" "$temporal/$f"
    git -C "$temporal" add -N -- "$f" >/dev/null 2>&1 # los archivos nuevos tambien salen en el diff
done
# Arbol de trabajo (archivos actuales) frente al indice (base + parches anteriores).
git -C "$temporal" diff > "$destino"
echo "Parche: $destino ($(wc -l < "$destino") lineas)"
git -C "$temporal" add -A >/dev/null 2>&1

if comprobar; then
    echo "Comprobado: base + parches = arbol real"
else
    echo "AVISO: base + parches no coincide con el arbol real (falta algun archivo en el parche?)" >&2
    exit 2
fi
