# rendimiento.md T2a: una partida como la de T1/L3 (scripts\capturar-ventana.ps1, carrera rapida, acelerar desde 192 s)
# con el A/B dentro de la misma partida (F1_AB, src\f1_overrides.cpp): ventanas de -Ventana segundos que alternan el
# modo 0 con los de -Modo (999 = ningun codigo lo mira: A/A). Despues: python -I scripts\ab\ab_resumen.py <out.log>.
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\ab\correr-ab.ps1 -Nombre ab1 [-Modo 999] [-Ventana 4]
# Salida en logs\rend-<Nombre>\: previo.txt, capturar.txt, captura-*.out/err.log, capturas, tarjetas\ (nuevas).
# PC compartido con la sesion de F1 06: toma el cerrojo con scripts/cerrojo-pc.sh (espera si es de F1-06), espera a que
# no haya otro ps2EntryRunner o compilacion y lo suelta al acabar, tambien si falla.
param(
    [string]$Nombre = 'ab1',
    [string]$Modo = '999',
    [double]$Ventana = 4,
    [double]$Desde = 150,
    [int]$Segundos = 480,
    [string]$Pulsar = '24:abajo,25:abajo,26:abajo,27:abajo,28:abajo,40:cruz,55:cruz,70:start,80:start,90:cruz,100:cruz,108:cruz,118:cruz,124:abajo,125:abajo,126:abajo,127:abajo,130:cruz,135:abajo,136:abajo,139:izquierda,140:izquierda,141:izquierda,145:cruz,150:cruz,184:cruz,192:cruz:290',
    [int]$EsperarLibre = 90  # minutos de espera a que el PC quede libre
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $raiz
$dir = Join-Path $raiz "logs\rend-$Nombre"
if (Test-Path (Join-Path $dir 'previo.txt')) { throw "Ya existe $dir" }
[void](New-Item -ItemType Directory -Force -Path $dir)
[void](New-Item -ItemType Directory -Force -Path (Join-Path $dir 'tarjetas'))

# Cerrojo compartido con la sesion de F1-06: scripts/cerrojo-pc.sh (creacion atomica, respeta y escribe los dos
# nombres de archivo). Espera hasta $EsperarLibre minutos; codigo 3 = sigue ocupado.
$bash = 'C:\Program Files\Git\bin\bash.exe'
$cerrojoSh = Join-Path $PSScriptRoot '..\cerrojo-pc.sh'
& $bash $cerrojoSh tomar "partida A/B $Nombre" ([int][math]::Ceiling(($Segundos + 60) / 60)) $EsperarLibre
if ($LASTEXITCODE -ne 0) { throw "No se pudo tomar el cerrojo del PC (codigo $LASTEXITCODE)" }
"{0:HH:mm:ss} cerrojo puesto" -f (Get-Date)
$ocupado = 'ps2EntryRunner|^cl$|^link$|^ninja$|^cmake$|^msbuild$'
$hasta = (Get-Date).AddMinutes($EsperarLibre)
while ($true) {
    $otros = @(Get-Process | Where-Object { $_.ProcessName -match $ocupado })
    if ($otros.Count -eq 0) { break }
    $motivo = ($otros | ForEach-Object { "$($_.ProcessName)($($_.Id))" }) -join ' '
    if ((Get-Date) -gt $hasta) { & $bash $cerrojoSh soltar; throw "El PC sigue ocupado: $motivo" }
    "{0:HH:mm:ss} esperando: {1}" -f (Get-Date), $motivo
    Start-Sleep -Seconds 120
}
try {
    $obj = "\Informaci$([char]0xF3)n del procesador"
    $carga = try { ((Get-Counter -Counter "$obj(_Total)\% de utilidad del procesador" -SampleInterval 1 -MaxSamples 3).CounterSamples |
            Measure-Object -Property CookedValue -Average).Average } catch { -1 }
    @(
        ("{0:yyyy-MM-dd HH:mm:ss} {1}" -f (Get-Date), ((powercfg /getactivescheme) -join ' '))
        ("carga previa (3 s) = {0:N1} %" -f $carga)
        "F1_AB=$Ventana F1_AB_MODO=$Modo F1_AB_DESDE=$Desde Segundos=$Segundos"
        "ningun ps2EntryRunner ni compilacion abiertos al empezar"
    ) | Out-File -Encoding utf8 (Join-Path $dir 'previo.txt')

    $env:PS2X_GS = 'gpu'
    $env:PS2X_TRAZA_FOTOGRAMAS = '1'
    $env:PS2X_TARJETAS = "logs\rend-$Nombre\tarjetas"
    $env:F1_AB = "$Ventana"
    $env:F1_AB_MODO = $Modo
    $env:F1_AB_DESDE = "$Desde"
    & powershell -NoProfile -ExecutionPolicy Bypass -File scripts\capturar-ventana.ps1 -Segundos $Segundos `
        -Salida "logs\rend-$Nombre\captura.png" -Extra '190,215,300' -Pulsar $Pulsar *>&1 |
        Out-File -Encoding utf8 (Join-Path $dir 'capturar.txt')
    $texto = Get-Content (Join-Path $dir 'capturar.txt') -Raw
    if ($texto -match 'Log: (.+?)\.out\.log') {
        $base = $Matches[1].Trim()
        foreach ($ext in '.out.log', '.err.log') {
            if (Test-Path "$base$ext") { Move-Item "$base$ext" $dir }
        }
    }
    $carga = try { ((Get-Counter -Counter "$obj(_Total)\% de utilidad del procesador" -SampleInterval 1 -MaxSamples 3).CounterSamples |
            Measure-Object -Property CookedValue -Average).Average } catch { -1 }
    ("carga al acabar (3 s) = {0:N1} %" -f $carga) | Out-File -Encoding utf8 -Append (Join-Path $dir 'previo.txt')
}
finally {
    & $bash $cerrojoSh soltar
    "{0:HH:mm:ss} cerrojo quitado" -f (Get-Date)
}
Get-ChildItem $dir | Select-Object Name, Length | Format-Table -AutoSize | Out-String
