# Mide la CPU de cada hilo de un proceso mientras corre (solo lectura: no toca el proceso).
# Uso: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\medir-hilos.ps1 [-Proceso ps2EntryRunner]
#        [-Intervalo 5] [-Salida logs\hilos.csv] [-Top 8] [-Espera 60]
# Espera hasta -Espera s a que arranque el proceso y muestrea cada -Intervalo s hasta que se cierra. Por muestra y hilo
# escribe en -Salida (CSV): t (s desde el arranque del proceso, como el t= de [ps2x:fotogramas]), tid, nombre, cpu
# (% de un nucleo en el intervalo). El nombre es la descripcion del hilo (SetThreadDescription) o, si no tiene,
# modulo+desplazamiento de su direccion de inicio. Junto a -Salida deja un .txt con los -Top hilos de cada muestra.
param(
    [string]$Proceso = 'ps2EntryRunner',
    [double]$Intervalo = 5,
    [string]$Salida = 'logs\hilos.csv',
    [int]$Top = 8,
    [int]$Espera = 60
)
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($Salida)) { $Salida = Join-Path $raiz $Salida }
[void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Salida))
$resumen = [System.IO.Path]::ChangeExtension($Salida, '.txt')

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HilosMedida {
    [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr OpenThread(uint acceso, bool heredar, uint tid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr h, out IntPtr texto);
    [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr p);
    [DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(IntPtr h, int clase, out IntPtr info, int largo, IntPtr devuelto);
    const uint THREAD_QUERY_LIMITED_INFORMATION = 0x0800, THREAD_QUERY_INFORMATION = 0x0040;
    public static string Descripcion(uint tid) {
        IntPtr h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, false, tid);
        if (h == IntPtr.Zero) return "";
        try {
            IntPtr p;
            if (GetThreadDescription(h, out p) < 0 || p == IntPtr.Zero) return "";
            string s = Marshal.PtrToStringUni(p); LocalFree(p); return s ?? "";
        } finally { CloseHandle(h); }
    }
    public static long Inicio(uint tid) {
        IntPtr h = OpenThread(THREAD_QUERY_INFORMATION, false, tid);
        if (h == IntPtr.Zero) return 0;
        try {
            IntPtr dir;  // 9 = ThreadQuerySetWin32StartAddress
            return NtQueryInformationThread(h, 9, out dir, IntPtr.Size, IntPtr.Zero) == 0 ? dir.ToInt64() : 0;
        } finally { CloseHandle(h); }
    }
}
'@

$limite = (Get-Date).AddSeconds($Espera)
$proc = $null
while (-not $proc -and (Get-Date) -lt $limite) {
    $proc = Get-Process -Name $Proceso -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { Start-Sleep -Milliseconds 500 }
}
if (-not $proc) { throw "No arranca $Proceso en $Espera s" }
$arranque = $proc.StartTime
$modulos = @()
function NombreInicio([long]$dir) {
    if ($dir -eq 0) { return '?' }
    if ($script:modulos.Count -eq 0) {
        try { $script:modulos = @($proc.Modules | ForEach-Object { [pscustomobject]@{ N = $_.ModuleName; B = $_.BaseAddress.ToInt64(); L = [long]$_.ModuleMemorySize } }) } catch {}
    }
    foreach ($m in $script:modulos) { if ($dir -ge $m.B -and $dir -lt $m.B + $m.L) { return ('{0}+0x{1:x}' -f $m.N, ($dir - $m.B)) } }
    '0x{0:x}' -f $dir
}
$nombres = @{}
function Nombre([int]$tid) {
    if (-not $script:nombres.ContainsKey($tid) -or $script:nombres[$tid] -like '@*') {
        $d = [HilosMedida]::Descripcion([uint32]$tid)
        $script:nombres[$tid] = if ($d) { $d } else { '@' + (NombreInicio ([HilosMedida]::Inicio([uint32]$tid))) }
    }
    $script:nombres[$tid]
}

't,tid,nombre,cpu' | Out-File -Encoding utf8 $Salida
"Proceso $Proceso (PID $($proc.Id)) arrancado $($arranque.ToString('HH:mm:ss')); intervalo $Intervalo s; cpu = % de un nucleo" |
    Out-File -Encoding utf8 $resumen
$cultura = [System.Globalization.CultureInfo]::InvariantCulture
$antes = @{}; $tAntes = $null
while (-not $proc.HasExited) {
    $ahora = Get-Date
    $actual = @{}
    try { $proc.Refresh(); foreach ($h in $proc.Threads) { try { $actual[$h.Id] = $h.TotalProcessorTime.TotalMilliseconds } catch {} } } catch { break }
    if ($tAntes) {
        $ms = ($ahora - $tAntes).TotalMilliseconds
        $t = ($ahora - $arranque).TotalSeconds
        $filas = foreach ($tid in $actual.Keys) {
            $previo = if ($antes.ContainsKey($tid)) { $antes[$tid] } else { 0 }
            [pscustomobject]@{ Tid = $tid; Nombre = (Nombre $tid); Cpu = 100.0 * ($actual[$tid] - $previo) / $ms }
        }
        $filas = @($filas | Sort-Object Cpu -Descending)
        $lineas = foreach ($f in $filas) {
            [string]::Format($cultura, '{0:F1},{1},"{2}",{3:F1}', $t, $f.Tid, ($f.Nombre -replace '"', "'"), $f.Cpu)
        }
        $lineas | Out-File -Encoding utf8 -Append $Salida
        $total = ($filas | Measure-Object Cpu -Sum).Sum
        $cabeza = [string]::Format($cultura, 't={0:F0} total={1:F0}% hilos={2}:', $t, $total, $filas.Count)
        # (no llamar $top a la lista: PowerShell no distingue mayusculas y pisaria el parametro [int]$Top)
        $lista = ($filas | Select-Object -First $Top | ForEach-Object { [string]::Format($cultura, ' {0}[{1}]={2:F0}', $_.Nombre, $_.Tid, $_.Cpu) }) -join ''
        ($cabeza + $lista) | Out-File -Encoding utf8 -Append $resumen
    }
    $antes = $actual; $tAntes = $ahora
    Start-Sleep -Milliseconds ([int]($Intervalo * 1000))
}
"Fin: $Proceso cerrado" | Out-File -Encoding utf8 -Append $resumen
