# rendimiento.md T2 (diagnostico): una partida como en T1 (scripts\capturar-ventana.ps1, salida con la parrilla entera)
# con scripts\medir-hilos.ps1 y dos perfiles por muestreo del GameThread (scripts\perfil\perfil.cpp): la salida y un
# tramo tranquilo. Solo mide: no toca el runtime ni el juego.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\perfil\correr.ps1 -Nombre p1
# Salida en logs\rend-<Nombre>\: previo.txt, capturar.txt, captura-*.out/err.log, hilos.csv/txt, capturas,
# perfil-salida.txt / perfil-calma.txt (+ -serie.txt, -crudo.txt), jitmapa.txt.
param(
    [string]$Nombre = 'p1',
    [int]$Segundos = 320,
    [double]$SalidaDesde = 188, [double]$SalidaDura = 27,
    [double]$CalmaDesde = 262, [double]$CalmaDura = 38,
    [int]$Intervalo = 4,
    [string]$Pulsar = '24:abajo,25:abajo,26:abajo,27:abajo,28:abajo,40:cruz,55:cruz,70:start,80:start,90:cruz,100:cruz,108:cruz,118:cruz,124:abajo,125:abajo,126:abajo,127:abajo,130:cruz,135:abajo,136:abajo,139:izquierda,140:izquierda,141:izquierda,145:cruz,150:cruz,184:cruz,192:cruz:120',
    [int]$EsperarLibre = 90  # minutos de espera a que no haya otro ps2EntryRunner ni compilacion
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $raiz
$dir = Join-Path $raiz "logs\rend-$Nombre"
$perfilExe = Join-Path $raiz 'logs\rend-perfil\bin\perfil.exe'
if (-not (Test-Path $perfilExe)) { throw "Falta $perfilExe (compilar scripts\perfil\perfil.cpp)" }
[void](New-Item -ItemType Directory -Force -Path $dir)
[void](New-Item -ItemType Directory -Force -Path (Join-Path $dir 'tarjetas'))

$ocupado = 'ps2EntryRunner|^F1-0|^cl$|^link$|^ninja$|^cmake$|^msbuild$|^clang|^lld'
$hasta = (Get-Date).AddMinutes($EsperarLibre)
while ($true) {
    $otros = @(Get-Process | Where-Object { $_.ProcessName -match $ocupado })
    if ($otros.Count -eq 0) { break }
    if ((Get-Date) -gt $hasta) { throw 'El PC sigue ocupado (otro juego o compilacion)' }
    "{0:HH:mm:ss} esperando: {1}" -f (Get-Date), (($otros | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ' ')
    Start-Sleep -Seconds 30
}
$obj = "\Informaci$([char]0xF3)n del procesador"
$carga = try { ((Get-Counter -Counter "$obj(_Total)\% de utilidad del procesador" -SampleInterval 1 -MaxSamples 3).CounterSamples |
        Measure-Object -Property CookedValue -Average).Average } catch { -1 }
@(
    ("{0:yyyy-MM-dd HH:mm:ss} {1}" -f (Get-Date), ((powercfg /getactivescheme) -join ' '))
    ("carga previa (3 s) = {0:N1} %" -f $carga)
    "ningun ps2EntryRunner ni compilacion abiertos al empezar"
) | Out-File -Encoding utf8 (Join-Path $dir 'previo.txt')

$env:PS2X_GS = 'gpu'
$env:PS2X_TRAZA_FOTOGRAMAS = '1'
$env:PS2X_TARJETAS = "logs\rend-$Nombre\tarjetas"
$env:PS2X_GS_GIF_ESPERAS = '1'   # "[gs:gif]" cada 10 s: esperas del EE al hilo GS GIF por motivo
$env:PS2X_JIT_MAPA = Join-Path $dir 'jitmapa.txt'

$antes = Get-Date
$cap = Start-Process -FilePath powershell -PassThru -WindowStyle Hidden -WorkingDirectory $raiz `
    -RedirectStandardOutput (Join-Path $dir 'capturar.txt') -RedirectStandardError (Join-Path $dir 'capturar.err.txt') `
    -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts\capturar-ventana.ps1', '-Segundos', "$Segundos",
        '-Salida', "logs\rend-$Nombre\captura.png", '-Extra', '190,215,280', '-Pulsar', $Pulsar)
$hil = Start-Process -FilePath powershell -PassThru -WindowStyle Hidden -WorkingDirectory $raiz `
    -RedirectStandardOutput (Join-Path $dir 'hilos.out.txt') -RedirectStandardError (Join-Path $dir 'hilos.err.txt') `
    -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'scripts\medir-hilos.ps1', '-Salida', "logs\rend-$Nombre\hilos.csv")

$juego = $null
$limite = (Get-Date).AddSeconds(60)
while (-not $juego -and (Get-Date) -lt $limite) {
    $juego = Get-Process -Name ps2EntryRunner -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -ge $antes } | Select-Object -First 1
    if (-not $juego) { Start-Sleep -Milliseconds 300 }
}
if (-not $juego) { throw 'No arranca el juego' }
"juego pid $($juego.Id) arrancado {0:HH:mm:ss.fff}" -f $juego.StartTime

$perfiles = @()
foreach ($v in @(@('salida', $SalidaDesde, $SalidaDura), @('calma', $CalmaDesde, $CalmaDura))) {
    $env:PERFIL_SERIE = Join-Path $dir "perfil-$($v[0])-serie.txt"
    $env:PERFIL_CRUDO = Join-Path $dir "perfil-$($v[0])-crudo.txt"
    $perfiles += Start-Process -FilePath $perfilExe -PassThru -WindowStyle Hidden -WorkingDirectory $raiz `
        -RedirectStandardOutput (Join-Path $dir "perfil-$($v[0]).txt") -RedirectStandardError (Join-Path $dir "perfil-$($v[0]).err.txt") `
        -ArgumentList @("$($juego.Id)", "$($v[1])", "$($v[2])", "$Intervalo", 'GameThread')
}
Remove-Item Env:\PERFIL_SERIE, Env:\PERFIL_CRUDO

$cap.WaitForExit()
foreach ($p in $perfiles) { [void]$p.WaitForExit(600000) }
[void]$hil.WaitForExit(60000)
# Los logs del juego los deja capturar-ventana en logs\: se traen a la carpeta de la partida.
$texto = Get-Content (Join-Path $dir 'capturar.txt') -Raw
if ($texto -match 'Log: (.+?)\.out\.log') {
    $base = $Matches[1].Trim()
    foreach ($ext in '.out.log', '.err.log') {
        if (Test-Path "$base$ext") { Move-Item "$base$ext" $dir }
    }
}
"fin {0:HH:mm:ss}" -f (Get-Date)
Get-ChildItem $dir | Select-Object Name, Length | Format-Table -AutoSize | Out-String
