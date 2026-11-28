# Empaqueta F1 05 recompilado como un juego de PC: carpeta portable, zip e instalador (fase 5 T2, odd/tasks/paquete.md).
# Adaptado de scripts\empaquetar.ps1 de F1 06, sin lo que la fase 4 apago (odd/tasks/sin-extras.md): ni el acceso del
# invitado del juego en red, ni la carpeta mods\, ni textos del juego en red, el modo foto o las fotos (F1 05 no los
# tiene; sus OPCIONES PC van en los menus del juego desde la 1.1.0).
# Portable: una carpeta (por defecto dist\F1-05-portable; con -SinDatos, dist\F1-05-publico) con el .exe, sus DLL,
# portable.txt, README.txt (ingles), LEEME.txt (espanol), recomplauncher.json (de scripts\paquete, para RecompLauncher;
# se comprueba con scripts\comprobar-recomplauncher.py) y licenses\ (licencias de terceros y codigo fuente de
# paraLLEl-GS, LGPL). portable.txt hace que el .exe guarde config\, saves\, logs\ y cache\ en su carpeta (los crea al
# abrirse); sin el (instalado) van a Documentos\My Games\F1-05 y %LOCALAPPDATA%\F1-05\logs y cache.
# -SinDatos: la version publica, sin nada del juego (ni data\ ni la ISO). El .exe pide la ISO del jugador la primera
# vez y la copia en data\ (o el instalador, con F1-05.exe --extraer-iso). Un data\ que ya haya en esa carpeta (p. ej.
# de probarlo) se deja, pero no entra en el paquete.
# Datos (solo para uso personal): -Iso extrae la ISO en data\ con scripts\iso_ps2.py (la ISO solo se lee); -Datos
# copia una carpeta ya extraida (p. ej. juego) sin mc0, mc1 ni *.log. data\ no se vuelve a copiar si ya existe (salvo
# con -Rehacer).
# -Zip: ademas, <carpeta>.zip (dentro, una carpeta F1-05 con lo mismo, sin lo que haya creado el juego).
# -Instalador: ademas, compila scripts\instalador\F1-05.iss con tools\innosetup\ISCC.exe en dist\instalador (o
# dist\instalador-publico con -SinDatos). Si falta el .iss (fase 5 T4), se para con un mensaje.
# Comprobaciones duras: tienen que estar pgs_puente.dll junto a -Exe (sin ella el juego cae al modo CPU) y el icono
# build\icono\F1-05.ico (lo lleva el .exe si se compilo con el ya hecho, compilar-runner.cmd; el instalador lo usa
# para Setup.exe). El icono no se hace aqui: scripts\hacer-icono.ps1 arranca el juego (cerrojo del PC).
# Solo se borra lo que crea este script (.exe, .dll, README.txt, LEEME.txt, portable.txt, recomplauncher.json,
# licenses\, el .zip y el instalador); config\, saves\, logs\, cache\, data\ y mods\ (si el jugador la crea) nunca se
# tocan.
# Simbolos: el paquete no lleva el PDB; el .exe empaquetado y su PDB se guardan aparte en
# <Simbolos>\<version>_<guid>_<edad>\ (por defecto dist\simbolos, que no entra en el paquete, ni en el zip, ni en el
# instalador, ni en el repositorio) con identidad.txt (version, exe, fecha_pe, tam_imagen, pdb, guid, edad y SHA-256
# del .exe). Antes de tocar el paquete se comprueba que el PDB junto a -Exe es el de ese .exe (mismo GUID y edad); si
# no, se para. scripts\simbolizar.ps1 busca ahi el PDB de los informes de fallo de los jugadores.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\empaquetar.ps1 [-Iso 'ruta\juego.iso' |
#        -Datos 'juego' | -SinDatos] [-Rehacer] [-Zip] [-Instalador] [-Destino 'dist\F1-05-portable']
#        [-Exe 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe'] [-Nombre 'F1-05.exe'] [-Simbolos 'dist\simbolos']
param(
    [string]$Iso = '',
    [string]$Datos = '',
    [switch]$SinDatos,
    [switch]$Rehacer,
    [switch]$Zip,
    [switch]$Instalador,
    [string]$Destino = 'dist\F1-05-portable',
    [string]$Exe = 'build\runner-dev\ps2xRuntime\ps2EntryRunner.exe',
    [string]$Nombre = 'F1-05.exe',
    [string]$Simbolos = 'dist\simbolos'
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
Set-Location $raiz
# Version del paquete (README.txt, LEEME.txt e instalador): se cambia solo en scripts\paquete\version.txt, que tambien
# lee CMake al compilar el .exe (VersionInfo y la linea [version] del registro).
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'paquete\version.txt') -TotalCount 1).Trim()
if ($version -notmatch '^[0-9A-Za-z.+-]+$') { throw "scripts\paquete\version.txt no tiene una version valida: '$version'" }
$fecha = Get-Date -Format 'yyyy-MM-dd'
if ($Iso -and $Datos) { throw 'Usa -Iso o -Datos, no los dos' }
if ($SinDatos -and ($Iso -or $Datos)) { throw '-SinDatos no lleva -Iso ni -Datos' }
if ($SinDatos -and -not $PSBoundParameters.ContainsKey('Destino')) { $Destino = 'dist\F1-05-publico' }
$exeOrigen = [System.IO.Path]::GetFullPath((Join-Path $raiz $Exe))
$carpeta = [System.IO.Path]::GetFullPath((Join-Path $raiz $Destino)).TrimEnd('\')
$carpetaDatos = Join-Path $carpeta 'data'
if (-not (Test-Path $exeOrigen)) { throw "Falta $exeOrigen (compila antes con compilar-runner.cmd)" }
$puente = Join-Path (Split-Path -Parent $exeOrigen) 'pgs_puente.dll'
if (-not (Test-Path $puente)) {
    throw "Falta $puente (el GS por tarjeta grafica; sin el el juego cae al modo CPU): compilalo con gpu\pgs_puente\compilar.cmd y vuelve a compilar con compilar-runner.cmd"
}
$icono = Join-Path $raiz 'build\icono\F1-05.ico'
if (-not (Test-Path $icono)) {
    throw ("Falta ${icono}: hazlo con scripts\hacer-icono.ps1 (arranca el juego: toma antes el cerrojo del PC, " +
        "bash scripts/cerrojo-pc.sh tomar `"icono`" 5) y vuelve a compilar con compilar-runner.cmd para que el .exe lo lleve")
}
if ((Get-Item $icono).LastWriteTime -gt (Get-Item $exeOrigen).LastWriteTime) {
    Write-Warning 'El icono es mas nuevo que el .exe: vuelve a compilar con compilar-runner.cmd para que lo lleve dentro'
}
# -Instalador sin su guion se para antes de tocar nada (el .iss llega con la fase 5 T4).
$iss = Join-Path $raiz 'scripts\instalador\F1-05.iss'
if ($Instalador -and -not (Test-Path $iss)) {
    throw "Falta $iss (el guion de Inno Setup del instalador, fase 5 T4): sin el no se puede hacer el instalador"
}
$clonPgs = Join-Path $raiz 'third_party\parallel-gs'
if (-not (Test-Path (Join-Path $clonPgs 'COPYING.LGPLv3'))) {
    throw "Falta $clonPgs (ejecuta scripts\obtener-parallel-gs.ps1): hace falta para las licencias y el codigo fuente de pgs_puente.dll"
}
function Medir([scriptblock]$bloque) { $t = [Diagnostics.Stopwatch]::StartNew(); & $bloque; $t.Elapsed.TotalSeconds }

# Identidad de un .exe o .pdb (scripts\simbolizar.ps1 -Identidad): clave=valor separados por espacios.
function Identidad([string]$ruta) {
    $texto = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'simbolizar.ps1') -Identidad $ruta
    if ($LASTEXITCODE -ne 0) { throw "simbolizar.ps1 -Identidad $ruta ha fallado" }
    $id = [ordered]@{}
    foreach ($par in ("$texto".Trim() -split ' ')) { $k, $v = $par -split '=', 2; $id[$k] = $v }
    $id
}
# El PDB del .exe que se va a empaquetar (el que nombra su registro CodeView, en su carpeta), comprobado antes de
# tocar el paquete: mismo GUID y misma edad.
$idExe = Identidad $exeOrigen
if (-not $idExe.guid) { throw "$exeOrigen no lleva registro CodeView (se compilo sin /DEBUG): no hay PDB que guardar" }
$pdbOrigen = Join-Path (Split-Path -Parent $exeOrigen) $idExe.pdb
if (-not (Test-Path $pdbOrigen)) { throw "Falta $pdbOrigen, el PDB de ${exeOrigen}: sin el no se podran traducir los informes de fallo" }
$idPdb = Identidad $pdbOrigen
if ($idPdb.guid -ne $idExe.guid -or $idPdb.edad -ne $idExe.edad) {
    throw ("$pdbOrigen (guid=$($idPdb.guid) edad=$($idPdb.edad)) no es el PDB de $exeOrigen (guid=$($idExe.guid) " +
        "edad=$($idExe.edad)): vuelve a compilar con compilar-runner.cmd")
}

$carpetaLicencias = Join-Path $carpeta 'licenses'
if (Test-Path $carpeta) {
    Get-ChildItem $carpeta -File |
        Where-Object { $_.Extension -in '.exe', '.dll' -or
            $_.Name -in 'README.txt', 'LEEME.txt', 'portable.txt', 'recomplauncher.json' } |
        Remove-Item -Force
    if (Test-Path -LiteralPath $carpetaLicencias) { Remove-Item -LiteralPath $carpetaLicencias -Recurse -Force }
} else {
    New-Item -ItemType Directory -Path $carpeta | Out-Null
}
Get-ChildItem (Split-Path -Parent $exeOrigen) -Filter '*.dll' | Copy-Item -Destination $carpeta
$exeDestino = Join-Path $carpeta $Nombre
Copy-Item $exeOrigen $exeDestino

# Simbolos: el .exe empaquetado y su PDB, aparte y fuera del paquete (ver la cabecera).
$hashExe = (Get-FileHash -Algorithm SHA256 -LiteralPath $exeDestino).Hash
$carpetaSimbolos = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $raiz $Simbolos))) "${version}_$($idExe.guid)_$($idExe.edad)"
if (-not (Test-Path $carpetaSimbolos)) { New-Item -ItemType Directory -Path $carpetaSimbolos | Out-Null }
$exeSimbolos = Join-Path $carpetaSimbolos $Nombre
Copy-Item $exeDestino $exeSimbolos -Force
Copy-Item $pdbOrigen (Join-Path $carpetaSimbolos $idExe.pdb) -Force
$idGuardado = Identidad (Join-Path $carpetaSimbolos $idExe.pdb)
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $exeSimbolos).Hash -ne $hashExe -or
    $idGuardado.guid -ne $idExe.guid -or $idGuardado.edad -ne $idExe.edad) {
    throw "Los simbolos guardados en $carpetaSimbolos no coinciden con $exeDestino"
}
Set-Content -Path (Join-Path $carpetaSimbolos 'identidad.txt') -Encoding Ascii -Value @(
    "version=$version fecha=$fecha",
    ("exe=$Nombre fecha_pe=$($idExe.fecha_pe) tam_imagen=$($idExe.tam_imagen) pdb=$($idExe.pdb) guid=$($idExe.guid) " +
        "edad=$($idExe.edad)"),
    "sha256=$hashExe",
    "paquete=$carpeta")
Write-Host ("Simbolos: {0} y {1} en {2} (guid={3} edad={4})" -f $Nombre, $idExe.pdb, $carpetaSimbolos, $idExe.guid,
    $idExe.edad)
Set-Content -Path (Join-Path $carpeta 'portable.txt') -Encoding UTF8 -Value (('Con este archivo junto a {0}, el juego ' +
    'guarda config\, saves\, logs\ y cache\ en esta carpeta; borralo para usar Documentos\My Games\F1-05. / With ' +
    'this file next to {0}, the game keeps config\, saves\, logs\ and cache\ in this folder; delete it to use ' +
    'Documents\My Games\F1-05.') -f $Nombre)
# recomplauncher.json: RecompLauncher (instalar con la ISO del jugador, opciones en su ficha) lo lee junto al .exe. Se
# copia tal cual de scripts\paquete (se comprueba con scripts\comprobar-recomplauncher.py); antes, que sea JSON.
$fuenteLauncher = Join-Path $raiz 'scripts\paquete\recomplauncher.json'
if (-not (Test-Path $fuenteLauncher)) { throw "Falta $fuenteLauncher" }
try { Get-Content -LiteralPath $fuenteLauncher -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null }
catch { throw "$fuenteLauncher no es JSON valido: $($_.Exception.Message)" }
Copy-Item -LiteralPath $fuenteLauncher -Destination (Join-Path $carpeta 'recomplauncher.json') -Force

# Licencias: pgs_puente.dll lleva dentro paraLLEl-GS (LGPL-3.0+), asi que el paquete incluye su licencia, la de lo que
# usa (Granite, volk y Vulkan-Headers, MIT/Apache) y el codigo fuente de paraLLEl-GS, de Granite y del puente, para
# que cualquiera pueda recompilar y sustituir la DLL. Se rehace siempre (es pequeno) y solo dentro de licenses\.
$carpetaFuente = Join-Path $carpetaLicencias 'fuente'
New-Item -ItemType Directory -Path $carpetaFuente -Force | Out-Null
Copy-Item (Join-Path $clonPgs 'COPYING.LGPLv3') (Join-Path $carpetaLicencias 'paraLLEl-GS-COPYING.LGPLv3.txt')
Copy-Item (Join-Path $clonPgs 'Granite\LICENSE') (Join-Path $carpetaLicencias 'Granite-LICENSE.txt')
Copy-Item (Join-Path $clonPgs 'Granite\third_party\volk\LICENSE.md') (Join-Path $carpetaLicencias 'volk-LICENSE.txt')
Copy-Item (Join-Path $clonPgs 'Granite\third_party\khronos\vulkan-headers\LICENSE.md') (Join-Path $carpetaLicencias 'Vulkan-Headers-LICENSE.txt')
Copy-Item (Join-Path $raiz 'gpu\pgs_puente\LICENSE.txt') (Join-Path $carpetaLicencias 'pgs_puente-LICENSE.txt')
$commitPgs = (& git -C $clonPgs rev-parse HEAD).Trim()
$commitGranite = (& git -C (Join-Path $clonPgs 'Granite') rev-parse HEAD).Trim()
& git -C $clonPgs archive --format=zip -o (Join-Path $carpetaFuente "parallel-gs-$($commitPgs.Substring(0, 7)).zip") $commitPgs
if ($LASTEXITCODE -ne 0) { throw 'git archive de paraLLEl-GS ha fallado' }
& git -C (Join-Path $clonPgs 'Granite') archive --format=zip -o (Join-Path $carpetaFuente "Granite-$($commitGranite.Substring(0, 7)).zip") $commitGranite
if ($LASTEXITCODE -ne 0) { throw 'git archive de Granite ha fallado' }
Compress-Archive -Path (Join-Path $raiz 'gpu\pgs_puente\*') -DestinationPath (Join-Path $carpetaFuente 'pgs_puente.zip') -Force
$submodulos = (& git -C $clonPgs submodule status --recursive) -join "`r`n"
Set-Content -Path (Join-Path $carpetaLicencias 'LICENSES.txt') -Encoding UTF8 -Value @"
Licencias de los componentes de terceros de $Nombre / Third-party licenses of $Nombre
=========================================================================================

pgs_puente.dll (el GS por tarjeta grafica / the graphics-card GS) incluye / includes:
- paraLLEl-GS (https://github.com/Arntzen-Software/parallel-gs), commit $commitPgs.
  Licencia / License: GNU LGPL 3.0 o posterior / or later (paraLLEl-GS-COPYING.LGPLv3.txt).
- Granite (https://github.com/Themaister/Granite), commit $commitGranite. Licencia MIT (Granite-LICENSE.txt).
- volk (MIT, volk-LICENSE.txt) y Vulkan-Headers (Apache 2.0 o MIT, Vulkan-Headers-LICENSE.txt).

Codigo fuente (carpeta fuente\): paraLLEl-GS y Granite en esos commits, y el puente pgs_puente (gpu\pgs_puente del
proyecto, con su CMakeLists.txt y compilar.cmd). Para sustituir pgs_puente.dll por una version propia, compila el
puente contra tu copia de paraLLEl-GS y pon la DLL nueva junto a ${Nombre}: el juego solo usa su API C (pgs_puente.h).
Source code (fuente\ folder): paraLLEl-GS and Granite at those commits, and the pgs_puente bridge. To replace
pgs_puente.dll with your own build, build the bridge against your copy of paraLLEl-GS and put the new DLL next to
${Nombre}: the game only uses its C API (pgs_puente.h).

Submodulos de paraLLEl-GS en el momento de compilar (git submodule status --recursive):
$submodulos

$Nombre incluye el filtro de imagen fsr: AMD FidelityFX Super Resolution 1 (EASU y RCAS) adaptado a GLSL.
Licencia MIT:
  Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
  documentation files (the "Software"), to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of
  the Software.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
  WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
  OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Las DLL de FFmpeg que acompanan al juego se distribuyen con su propia licencia LGPL.
The FFmpeg DLLs that come with the game are distributed under their own LGPL license.
"@

if ($Iso -or $Datos) {
    if ((Test-Path $carpetaDatos) -and -not $Rehacer) {
        Write-Host "data\ ya existe: no se vuelve a copiar (usa -Rehacer para rehacerlo)"
    } else {
        $origenDatos = ''
        if ($Datos) { $origenDatos = [System.IO.Path]::GetFullPath((Join-Path $raiz $Datos)) }
        if ($origenDatos -and $origenDatos.TrimEnd('\') -eq $carpetaDatos) { throw "No se puede copiar data\ sobre si mismo" }
        if (Test-Path $carpetaDatos) { Remove-Item $carpetaDatos -Recurse -Force }
        $segundos = Medir {
            if ($Iso) {
                $isoRuta = [System.IO.Path]::GetFullPath($Iso)
                if (-not (Test-Path $isoRuta)) { throw "Falta la ISO $isoRuta" }
                & python -I -X utf8 (Join-Path $raiz 'scripts\iso_ps2.py') extraer $isoRuta $carpetaDatos '*' | Out-Null
                if ($LASTEXITCODE -ne 0) { throw "iso_ps2.py no pudo extraer $isoRuta" }
            } else {
                if (-not (Test-Path (Join-Path $origenDatos 'SYSTEM.CNF'))) { throw "$origenDatos no tiene SYSTEM.CNF" }
                # robocopy: /E subcarpetas, /XD y /XF exclusiones; codigos de salida < 8 son correctos.
                & robocopy $origenDatos $carpetaDatos /E /XD (Join-Path $origenDatos 'mc0') (Join-Path $origenDatos 'mc1') `
                    /XF '*.log' /NFL /NDL /NJH /NJS /NP | Out-Null
                if ($LASTEXITCODE -ge 8) { throw "robocopy no pudo copiar $origenDatos (codigo $LASTEXITCODE)" }
            }
        }
        if (-not (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF'))) { throw "data\ no tiene SYSTEM.CNF" }
        Write-Host ("data\ {0} en {1:N0} s" -f $(if ($Iso) { "extraido de $Iso" } else { "copiado de $origenDatos" }), $segundos)
    }
}

$conDatos = (-not $SinDatos) -and (Test-Path (Join-Path $carpetaDatos 'SYSTEM.CNF'))
# El instalador publico es otra aplicacion que el de uso personal, pero sus accesos se llaman igual: "F1 05".
$accesoJuego = 'F1 05'
if ($SinDatos -and (Test-Path $carpetaDatos)) {
    Write-Warning "$carpetaDatos existe: se deja en la carpeta, pero no entra en el zip ni en el instalador"
}
$repositorio = 'https://github.com/KevinL50-sysadmin/F1-05-PC'
# PowerShell 5 lee este .ps1 como ANSI: las letras con tilde del texto de Windows van como codigos.
$masInformacion = "M$([char]0xE1)s informaci$([char]0xF3)n"
$protegio = "Windows protegi$([char]0xF3) su PC"

if ($conDatos) {
    $comoEs = 'Los datos del juego ya estan en la carpeta data\: no hace falta nada mas.'
    $comoEn = 'The game data is already in the data\ folder: nothing else is needed.'
} else {
    $comoEs = @"
Este paquete no trae nada del juego: hace falta tu copia de Formula One 05 de PS2, version europea PAL
(SCES-53033), como imagen .iso (unos 3,5 GB; tambien vale una carpeta con el disco ya extraido). Solo se
pide una vez:
- Instalador: en la pagina "Elige tu copia del juego (ISO)" busca tu .iso (si esta en Descargas o en el
  Escritorio ya sale puesta). Al instalar se comprueba y se copian sus archivos en data\, con una barra de
  progreso. Si dejas la pagina en blanco, el juego la pide al abrirlo.
- Portable, o si no la elegiste al instalar: la primera vez que abres $Nombre, el juego busca la ISO en
  Descargas, en el Escritorio y en su carpeta y te la ofrece; si no la encuentra, eligela en la ventana de
  Windows (o el SYSTEM.CNF de la carpeta del disco extraido). Comprueba que es Formula One 05 PAL, copia sus
  archivos en la carpeta data\ (unos minutos, con una barra de progreso que se puede cancelar) y arranca el
  juego. Las siguientes veces arranca directamente.
- La ISO solo se lee, nunca se cambia: despues puedes guardarla donde quieras. Hacen falta unos 3,6 GB libres.
- Si la copia se corta o la cancelas, no queda nada a medias y el juego la vuelve a pedir. Una carpeta data\
  incompleta no se borra: se aparta como data.anterior\ (puedes borrarla tu).
- Instalado en Archivos de programa sin elegir la ISO al instalar: la carpeta del juego no se puede escribir
  sin permisos de administrador, asi que data\ va a %LOCALAPPDATA%\F1-05\data.
- Por linea de ordenes: $Nombre --extraer-iso "ruta\juego.iso" copia el disco en data\ sin abrir el juego
  (codigos de salida: 0 hecho o ya estaba, 1 uso incorrecto, 2 no es Formula One 05 PAL, 3 error de lectura,
  escritura o espacio, 4 cancelado; con --ventana muestra la barra y los errores en ventanas) y
  $Nombre --comprobar-iso "ruta\juego.iso" solo lo comprueba.
"@
    $comoEn = @"
This package includes nothing from the game: you need your own copy of Formula One 05 for PS2, European PAL
version (SCES-53033), as an .iso image (about 3.5 GB; a folder with the disc already extracted also works). It is
only asked for once:
- Installer: on the page "Choose your copy of the game (ISO)" browse to your .iso (if it is in Downloads or on
  the Desktop it is already filled in). During the installation it is checked and its files are copied to data\,
  with a progress bar. If you leave the page blank, the game asks for it when you open it.
- Portable, or if you did not choose it when installing: the first time you open $Nombre, the game looks for the
  ISO in Downloads, on the Desktop and in its own folder and offers it to you; if it does not find it, choose it
  in the Windows window (or the SYSTEM.CNF of the extracted disc folder). It checks that it is Formula One 05 PAL,
  copies its files to the data\ folder (a few minutes, with a progress bar that can be cancelled) and starts the
  game. The next times it starts directly.
- The ISO is only read, never changed: afterwards you can keep it wherever you like. About 3.6 GB of free space
  is needed.
- If the copy is interrupted or you cancel it, nothing is left half done and the game asks for it again. An
  incomplete data\ folder is not deleted: it is moved aside as data.anterior\ (you can delete it yourself).
- Installed in Program Files without choosing the ISO when installing: the game folder cannot be written without
  administrator rights, so data\ goes to %LOCALAPPDATA%\F1-05\data.
- From the command line: $Nombre --extraer-iso "path\game.iso" copies the disc to data\ without opening the
  game (exit codes: 0 done or already there, 1 wrong usage, 2 not Formula One 05 PAL, 3 read, write or space
  error, 4 cancelled; with --ventana it shows the progress bar and the errors in windows) and
  $Nombre --comprobar-iso "path\game.iso" only checks it.
"@
}
$avisoDatosEs = if ($conDatos) { "`r`nAVISO: la carpeta data\ contiene los archivos de tu disco. Es solo para uso personal: no la compartas." } else { '' }
$avisoDatosEn = if ($conDatos) { "`r`nWARNING: the data\ folder contains the files of your disc. It is for personal use only: do not share it." } else { '' }
$dataEs = if ($conDatos) { '' } else { "Los copia el juego de tu ISO la primera vez;`r`n  data\F1-05-disco.txt es la lista con la que comprueba en cada arranque que esta completa." }
$dataEn = if ($conDatos) { '' } else { "The game copies them from your ISO the first time;`r`n  data\F1-05-disco.txt is the list it uses to check on every start that it is complete." }

# README.txt (ingles) y LEEME.txt (espanol): las mismas secciones y el mismo contenido; al cambiar uno, cambiar el otro.
$leeme = @"
English: see README.txt.

Formula One 05 (PS2) recompilado para Windows
=============================================
Version $version ($fecha)
Que ha cambiado en cada version (en ingles): $repositorio/blob/main/CHANGELOG.md

Formula One 05 es el juego oficial de la temporada 2005 de Formula 1 para PlayStation 2 (Studio Liverpool, Sony,
2005). Este es el juego original de PS2 traducido a un programa de Windows (recompilacion estatica con PS2Recomp):
se juega como en la consola, con mando, teclado o volante, en una ventana o a pantalla completa y con la tarjeta
grafica. No es un emulador ni trae nada del juego: necesitas tu propio disco.

Como jugar
----------
$comoEs
- Portable (.zip): descomprime la carpeta donde quieras y abre $Nombre. Todo lo tuyo se guarda dentro de esa
  carpeta, asi que puedes moverla o llevarla en un USB.
- Instalado: abre el acceso directo "$accesoJuego" del menu Inicio o del escritorio. Se desinstala desde
  Configuracion > Aplicaciones; pregunta si borrar tambien tus partidas y opciones.

La primera vez, Windows puede avisar con "$protegio" (SmartScreen) porque el .exe no esta firmado:
pulsa "$masInformacion" y luego "Ejecutar de todas formas".

Controles y ventana
-------------------
- Mando, teclado o volante. Los botones y las teclas se cambian en
  config\controls.ini, que viene explicado dentro.
- F11: pantalla completa (sin bordes, o la de pantalla en settings.ini) o ventana.
- F10: cambia el aspecto (4:3, 16:9, pixeles de la consola).
- Para salir del juego, cierra la ventana (la X o Alt+F4).
- Las opciones del PC (modo de la ventana, tamano, aspecto, filtro de la imagen, supersampling, sincronizacion
  vertical, contador de FPS, campo de vision, volumen, sonido y vibracion) se cambian en el propio juego, en
  OPCIONES PC (en las opciones del perfil y en las del menu de pausa), y se guardan en config\settings.ini, cada
  una explicada dentro; borralo para volver a los valores por defecto. Si usas RecompLauncher, tambien se cambian
  en la ficha del juego.

Graficos
--------
El juego dibuja en la tarjeta grafica con Vulkan (gs = gpu en settings.ini, lo normal; pgs_puente.dll, junto al
.exe, es esa parte). Sin Vulkan, o sin pgs_puente.dll, dibuja con el procesador (gs = cpu, modo Software, mucho mas
lento). El supersampling (bordes mas suaves; 2 de serie) y la doble resolucion (alta_resolucion, con supersampling
4 o mas) solo cargan la tarjeta grafica: subelos si le sobra, bajalos si va muy cargada. La velocidad del juego
depende sobre todo de un nucleo del procesador: si no llega, el juego va a camara lenta (no se salta imagenes).

La carpeta licenses\ tiene las licencias de los componentes de terceros (paraLLEl-GS, LGPL) y su codigo fuente.

Donde esta cada cosa
--------------------
Portable: todo en la carpeta del juego. Instalado: en Documentos\Mis juegos\F1-05 (en disco,
Documents\My Games\F1-05), salvo el registro y la cache, que van en %LOCALAPPDATA%\F1-05\logs y
%LOCALAPPDATA%\F1-05\cache.
Las carpetas se crean solas al abrir el juego.

- $Nombre y las .dll: el juego. No los separes.
- data\: los archivos del disco. No se modifican nunca. $dataEs
- config\settings.ini: las opciones del PC (ver "Controles y ventana"), el dibujo de los graficos (gs), si se
  guarda el registro (registro) y la espera del vigilante (vigilante, ver logs\).
- config\controls.ini: los controles (mando, teclado y volante).
- saves\mc0 y saves\mc1: las tarjetas de memoria (tus partidas). Copialas para hacer una copia de seguridad.
- logs\F1-05.log: el registro de la ultima partida (la anterior queda en F1-05.anterior.log). Si algo falla, es
  lo que hay que mirar. Si el juego se queda congelado 10 segundos, anota ahi donde estaba parada cada parte del
  juego; esa espera se cambia con vigilante en config\settings.ini (3 a 600 segundos; 0 lo apaga).
- cache\: los graficos ya preparados para tu tarjeta grafica, para que no haya tirones la segunda vez que ves
  cada cosa. Se puede borrar: el juego lo vuelve a crear.
- portable.txt (solo en la version portable): hace que todo se guarde en la carpeta del juego. Sin el, el
  juego usa las carpetas de la version instalada.
- recomplauncher.json: lo lee RecompLauncher para instalar el juego con tu ISO y mostrar sus opciones en la ficha
  del juego. El juego no lo usa; si no usas RecompLauncher, puedes ignorarlo.

Si el juego falla
-----------------
Si el juego se cierra solo, se cuelga o se ve mal:
- Si juegas desde RecompLauncher y el juego se cierra por un fallo, RecompLauncher te ofrece enviar el informe
  con el registro ya adjunto.
- Si no, abre un issue en $repositorio/issues (hace falta una cuenta de GitHub), cuenta que
  pasaba y adjunta logs\F1-05.log (instalado: %LOCALAPPDATA%\F1-05\logs\F1-05.log; si ya volviste a abrir el
  juego, el del fallo es F1-05.anterior.log). El registro puede llevar tu nombre de usuario de Windows en las rutas.

Requisitos
----------
Windows 10/11 de 64 bits. Necesitas tu propia copia del juego (Formula One 05 de PS2, PAL, SCES-53033, en
.iso) y unos 3,6 GB libres para sus archivos. Una tarjeta grafica con Vulkan para el modo normal; sin ella el
juego dibuja en modo Software (mas lento).

Licencias
---------
El codigo de este proyecto: ver $repositorio. Formula One 05 es de sus duenos; este
paquete no incluye nada del juego. Los componentes de terceros (paraLLEl-GS, LGPL 3.0; Granite, volk,
Vulkan-Headers, AMD FSR 1, FFmpeg y las demas DLL) van con sus licencias: ver licenses\LICENSES.txt.
$avisoDatosEs
"@
Set-Content -Path (Join-Path $carpeta 'LEEME.txt') -Value $leeme -Encoding UTF8

$readme = @"
Spanish: see LEEME.txt (en espanol).

Formula One 05 (PS2) recompiled for Windows
===========================================
Version $version ($fecha)
What changed in each version: $repositorio/blob/main/CHANGELOG.md

Formula One 05 is the official game of the 2005 Formula One season for PlayStation 2 (Studio Liverpool, Sony,
2005). This is the original PS2 game translated into a Windows program (static recompilation with PS2Recomp): it
plays like on the console, with a controller, keyboard or wheel, in a window or full screen, drawn on the graphics
card. It is not an emulator and includes nothing from the game: you need your own disc.

How to play
-----------
$comoEn
- Portable (.zip): unzip the folder wherever you like and open $Nombre. Everything of yours is saved inside that
  folder, so you can move it or carry it on a USB stick.
- Installed: open the "$accesoJuego" shortcut in the Start menu or on the desktop. It is uninstalled from
  Settings > Apps; it asks whether to delete your saves and options too.

The first time, Windows may warn with "Windows protected your PC" (SmartScreen) because the .exe is not signed:
click "More info" and then "Run anyway".

Controls and window
-------------------
- Controller, keyboard or wheel. Buttons and keys are changed in
  config\controls.ini, which is explained inside.
- F11: full screen (borderless, or the one in pantalla in settings.ini) or window.
- F10: changes the aspect ratio (4:3, 16:9, console pixels).
- To quit the game, close the window (the X or Alt+F4).
- The PC options (window mode, window size, aspect ratio, scaling filter, supersampling, V-Sync, FPS counter,
  field of view, volume, sound and vibration) are changed in the game itself, in PC OPTIONS (in the profile
  options and in the pause menu's options), and are saved in config\settings.ini, each one explained inside (in
  Spanish); delete it to go back to the defaults. If you use RecompLauncher, they can also be changed on the
  game's page.

Graphics
--------
The game draws on the graphics card with Vulkan (gs = gpu in settings.ini, the normal mode; pgs_puente.dll, next to
the .exe, is that part). Without Vulkan, or without pgs_puente.dll, it draws with the processor (gs = cpu, Software
mode, much slower). Supersampling (smoother edges; 2 by default) and double resolution (alta_resolucion, with
supersampling 4 or more) only load the graphics card: raise them if it has room, lower them if it is very busy. The
game's speed depends mostly on one processor core: if it cannot keep up, the game runs in slow motion (it does not
skip frames).

The licenses\ folder has the licenses of the third-party components (paraLLEl-GS, LGPL) and their source code.

Where everything is
-------------------
Portable: everything in the game folder. Installed: in Documents\My Games\F1-05 (shown as Documentos\Mis juegos\F1-05
on a Spanish Windows), except the log and the cache, which go to %LOCALAPPDATA%\F1-05\logs and
%LOCALAPPDATA%\F1-05\cache.
The folders are created by themselves when you open the game.

- $Nombre and the .dll files: the game. Do not separate them.
- data\: the files of the disc. They are never modified. $dataEn
- config\settings.ini: the PC options (see "Controls and window"), the graphics renderer (gs), whether the log is
  written (registro) and the watchdog wait (vigilante, see logs\).
- config\controls.ini: the controls (controller, keyboard and wheel).
- saves\mc0 and saves\mc1: the memory cards (your saves). Copy them to make a backup.
- logs\F1-05.log: the log of the last session (the previous one stays in F1-05.anterior.log). If something goes
  wrong, it is what to look at. If the game freezes for 10 seconds, it writes there where each part of the game
  was stopped; that wait is changed with vigilante in config\settings.ini (3 to 600 seconds; 0 turns it off).
- cache\: the graphics already prepared for your graphics card, so there is no stutter the second time you see
  each thing. It can be deleted: the game creates it again.
- portable.txt (only in the portable version): makes everything be saved in the game folder. Without it, the
  game uses the folders of the installed version.
- recomplauncher.json: RecompLauncher reads it to install the game with your ISO and show its options on the
  game's page. The game does not use it; if you do not use RecompLauncher, you can ignore it.

If the game fails
-----------------
If the game closes by itself, freezes or looks wrong:
- If you play from RecompLauncher and the game closes because of an error, RecompLauncher offers to send the
  report with the log already attached.
- Otherwise, open an issue at $repositorio/issues (a GitHub account is needed), say what was
  happening and attach logs\F1-05.log (installed: %LOCALAPPDATA%\F1-05\logs\F1-05.log; if you have opened the game
  again since, the one from the crash is F1-05.anterior.log). The log may contain your Windows user name in paths.

Requirements
------------
64-bit Windows 10/11. You need your own copy of the game (Formula One 05 for PS2, PAL, SCES-53033, as an .iso)
and about 3.6 GB of free space for its files. A graphics card with Vulkan for the normal mode; without it the game
draws in Software mode (slower).

Licenses
--------
This project's code: see $repositorio. Formula One 05 belongs to its owners; this
package includes nothing from the game. The third-party components (paraLLEl-GS, LGPL 3.0; Granite, volk,
Vulkan-Headers, AMD FSR 1, FFmpeg and the other DLLs) come with their licenses: see licenses\LICENSES.txt.
$avisoDatosEn
"@
Set-Content -Path (Join-Path $carpeta 'README.txt') -Value $readme -Encoding UTF8
$tamExe = (Get-Item $exeDestino).Length
Write-Host ("Listo: {0} ({1:N0} bytes), portable.txt, recomplauncher.json, README.txt, LEEME.txt y licenses\ en {2}" -f
    $Nombre, $tamExe, $carpeta)
if ($conDatos) {
    $tamDatos = (Get-ChildItem $carpetaDatos -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("data\: {0:N0} bytes" -f $tamDatos)
} else {
    Write-Host "Sin data\: el juego pide la ISO del jugador la primera vez (ver LEEME.txt)"
}

# Lo que va en el paquete: lo que pone este script, nunca lo que crea el juego al abrirse.
$archivosPaquete = @(Get-ChildItem $carpeta -File |
    Where-Object { $_.Extension -in '.exe', '.dll' -or
        $_.Name -in 'README.txt', 'LEEME.txt', 'portable.txt', 'recomplauncher.json' })
$archivosPaquete += @(Get-ChildItem $carpetaLicencias -Recurse -File)
if ($conDatos) { $archivosPaquete += @(Get-ChildItem $carpetaDatos -Recurse -File) }

if ($Zip) {
    if (-not $conDatos -and -not $SinDatos) { throw '-Zip necesita data\ (o -SinDatos para la version publica)' }
    $rutaZip = "$carpeta.zip"
    if (Test-Path $rutaZip) { Remove-Item $rutaZip -Force }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $segundos = Medir {
        $zipArchivo = [System.IO.Compression.ZipFile]::Open($rutaZip, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($a in $archivosPaquete) {
                $entrada = 'F1-05/' + $a.FullName.Substring($carpeta.Length + 1).Replace('\', '/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zipArchivo, $a.FullName, $entrada,
                    [System.IO.Compression.CompressionLevel]::Optimal)
            }
        } finally { $zipArchivo.Dispose() }
    }
    Write-Host ("Zip: {0} ({1:N0} bytes, {2} archivos) en {3:N0} s" -f $rutaZip, (Get-Item $rutaZip).Length,
        $archivosPaquete.Count, $segundos)
}

if ($Instalador) {
    if (-not $conDatos -and -not $SinDatos) { throw '-Instalador necesita data\ (o -SinDatos para la version publica)' }
    $iscc = Join-Path $raiz 'tools\innosetup\ISCC.exe'
    if (-not (Test-Path $iscc)) { throw "Falta $iscc (Inno Setup portable)" }
    $salida = Join-Path $raiz $(if ($SinDatos) { 'dist\instalador-publico' } else { 'dist\instalador' })
    if (Test-Path $salida) {
        Get-ChildItem $salida -File | Where-Object { $_.Name -like 'F1-05-Setup*.exe' -or $_.Name -like 'F1-05-Setup-*.bin' } |
            Remove-Item -Force
    } else {
        New-Item -ItemType Directory -Path $salida | Out-Null
    }
    # Imagenes del asistente: se usan si ya estan en build\instalador-imagen; sin ellas, las de Inno Setup.
    $carpetaImagen = Join-Path $raiz 'build\instalador-imagen'
    $argumentosImagen = @()
    $grandes = @(Get-ChildItem $carpetaImagen -Filter 'asistente-*x*.bmp' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike 'asistente-pequena-*' } | Sort-Object Length | ForEach-Object FullName)
    $pequenas = @(Get-ChildItem $carpetaImagen -Filter 'asistente-pequena-*.bmp' -ErrorAction SilentlyContinue |
        Sort-Object Length | ForEach-Object FullName)
    if ($grandes.Count) { $argumentosImagen += "/DImagenAsistente=$($grandes -join ',')" }
    if ($pequenas.Count) { $argumentosImagen += "/DImagenPequena=$($pequenas -join ',')" }
    # Sin data\, el instalador lleva la pagina de la ISO y no la carpeta data.
    if ($SinDatos) { $argumentosImagen += '/DSinDatos' }
    $segundos = Medir {
        & $iscc /Q "/DOrigen=$carpeta" "/DNombreExe=$Nombre" "/DVersion=$version" "/DFecha=$fecha" "/DIcono=$icono" @argumentosImagen "/O$salida" $iss
        if ($LASTEXITCODE -ne 0) { throw "ISCC.exe fallo (codigo $LASTEXITCODE)" }
    }
    Write-Host ("Instalador en {0:N0} s:" -f $segundos)
    Get-ChildItem $salida -File | Where-Object { $_.Name -like 'F1-05-Setup*' } |
        ForEach-Object { Write-Host ("  {0} ({1:N0} bytes)" -f $_.Name, $_.Length) }
}
