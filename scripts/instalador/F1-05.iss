; Instalador de F1 05 recompilado (Inno Setup 6), adaptado del de F1 06 (fase 5 T4, odd/tasks/paquete.md). No se
; compila a mano: lo hace scripts\empaquetar.ps1 -Instalador, que pasa la carpeta portable ya hecha (Origen), el nombre
; del .exe, la version (scripts\paquete\version.txt), la fecha, el icono (Icono: build\icono\F1-05.ico), las imagenes
; del asistente si estan (ImagenAsistente, ImagenPequena), SinDatos con -SinDatos y la carpeta de salida.
; Instala el .exe, las DLL, README.txt (ingles), LEEME.txt (espanol), recomplauncher.json (RecompLauncher) y licenses\
; (y data\ sin SinDatos); sin portable.txt, asi que el juego guarda lo del usuario en Documentos\My Games\F1-05
; (config, saves) y los registros y la cache en AppData\Local\F1-05.
; Sin los extras de PC (fase 4, odd/tasks/sin-extras.md): un solo acceso, al juego; ni juego en red ni sus textos.
; Con /DSinDatos (empaquetar.ps1 -SinDatos, la version publica) no lleva data\: un solo Setup.exe con la pagina "Elige
; tu copia del juego (ISO)", que al instalar ejecuta {#NombreExe} --extraer-iso <iso> --ventana (su barra de progreso,
; cancelable; en silencio, sin --ventana, como F1 06 fallos-con-log T9). En blanco, el juego pide la ISO la primera
; vez que se abre.

#ifndef Origen
  #error Falta /DOrigen=<carpeta portable>: compila con scripts\empaquetar.ps1 -Instalador
#endif
#ifndef NombreExe
  #define NombreExe "F1-05.exe"
#endif
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Fecha
  #define Fecha ""
#endif
; La publica es otra aplicacion para Windows que la de uso personal (con datos): su AppId, su carpeta, sus accesos y su
; nombre en Aplicaciones. Instalar o desinstalar una nunca toca la otra. AppId propios de F1 05 (no los de F1 06).
#ifdef SinDatos
  #define IdApp "{{E828217A-37A8-4404-A9A8-FB6C5D4AAE06}"
  #define NombreApp "F1 05 PC"
  #define CarpetaApp "F1-05-PC"
#else
  #define IdApp "{{BC9FEF81-2958-46C1-AA74-86B870B0A90C}"
  #define NombreApp "F1 05"
  #define CarpetaApp "F1-05"
#endif

[Setup]
AppId={#IdApp}
AppName={#NombreApp}
AppVersion={#Version} ({#Fecha})
AppVerName={#NombreApp} {#Version}
VersionInfoVersion={#Version}
VersionInfoDescription=Instalador de {#NombreApp}
DefaultDirName={autopf}\{#CarpetaApp}
DisableProgramGroupPage=yes
; Por defecto pide administrador (Archivos de programa); el dialogo deja instalar solo para el usuario actual
; sin permisos (y /CURRENTUSER en la linea de ordenes).
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=F1-05-Setup-{#Version}
#ifdef SinDatos
Compression=lzma2/ultra64
SolidCompression=yes
; Lo que ocupara data\ al copiar la ISO (unos 3,5 GB; Inno avisa si no cabe).
ExtraDiskSpaceRequired=3700000000
#else
; Los datos rondan 3,5 GB: Setup.exe + archivos .bin (DiskSpanning) del tamano maximo. Compresion lzma2/fast en
; varios hilos: la mayor parte es audio y video ya comprimido.
Compression=lzma2/fast
SolidCompression=no
LZMANumBlockThreads=8
LZMAUseSeparateProcess=yes
DiskSpanning=yes
DiskSliceSize=max
#endif
; El .exe lleva el icono dentro: es el que sale en Configuracion > Aplicaciones.
UninstallDisplayIcon={app}\{#NombreExe}
#ifdef Icono
SetupIconFile={#Icono}
#endif
UninstallDisplayName={#NombreApp}
WizardStyle=modern
; Imagenes del asistente (build\instalador-imagen, si estan), una por escala.
#ifdef ImagenAsistente
WizardImageFile={#ImagenAsistente}
; La pagina de bienvenida (Inno Setup 6 la quita por defecto) es la que ensena la imagen al entrar.
DisableWelcomePage=no
#endif
#ifdef ImagenPequena
WizardSmallImageFile={#ImagenPequena}
#endif
ShowLanguageDialog=no

[Languages]
; El idioma sale del de Windows (sin preguntar): espanol, o ingles en cualquier otro.
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
es.BorrarPartidas=¿Borrar también tus partidas y opciones (Documentos\My Games\F1-05) y los registros y la caché (%LOCALAPPDATA%\F1-05)?
en.BorrarPartidas=Also delete your saves and options (Documents\My Games\F1-05) and the logs and cache (%LOCALAPPDATA%\F1-05)?
es.IsoTitulo=Elige tu copia del juego (ISO)
en.IsoTitulo=Choose your copy of the game (ISO)
es.IsoDescripcion=El juego no trae nada del disco: hace falta tu Formula One 05 de PS2 (europeo PAL, SCES-53033).
en.IsoDescripcion=The game includes nothing from the disc: your Formula One 05 for PS2 (European PAL, SCES-53033) is needed.
es.IsoTexto=Elige la imagen .iso de tu disco. Al instalar se comprueba y sus archivos se copian en la carpeta data del juego (unos 3,5 GB, unos minutos, con una barra de progreso); la ISO solo se lee.%n%nSi lo dejas en blanco, el juego te la pedirá la primera vez que lo abras.
en.IsoTexto=Choose the .iso image of your disc. During the installation it is checked and its files are copied to the game's data folder (about 3.5 GB, a few minutes, with a progress bar); the ISO is only read.%n%nIf you leave it blank, the game asks for it the first time you open it.
es.IsoCampo=Imagen del disco (.iso):
en.IsoCampo=Disc image (.iso):
es.IsoFiltro=Imagen de disco de PS2 (*.iso)|*.iso|Todos los archivos|*.*
en.IsoFiltro=PS2 disc image (*.iso)|*.iso|All files|*.*
es.IsoNoExiste=Ese archivo no existe. Elige tu .iso o deja el campo en blanco.
en.IsoNoExiste=That file does not exist. Choose your .iso or leave the field blank.
es.IsoCopiando=Copiando los archivos de tu disco...
en.IsoCopiando=Copying the files of your disc...
es.IsoSinCopiar=No se han copiado los archivos del disco. El juego te pedirá tu copia la primera vez que lo abras.
en.IsoSinCopiar=The disc files were not copied. The game will ask for your copy the first time you open it.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Origen}\{#NombreExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Origen}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Origen}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Origen}\LEEME.txt"; DestDir: "{app}"; Flags: ignoreversion
; RecompLauncher: junto al .exe; sin portable.txt usa las rutas de Documentos y AppData
Source: "{#Origen}\recomplauncher.json"; DestDir: "{app}"; Flags: ignoreversion
; Licencias de paraLLEl-GS (LGPL) y sus dependencias, con su codigo fuente (lo genera empaquetar.ps1)
Source: "{#Origen}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifndef SinDatos
Source: "{#Origen}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
; Solo el juego (S6: sin accesos de los extras de PC ni del juego en red).
Name: "{autoprograms}\{#NombreApp}"; Filename: "{app}\{#NombreExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#NombreApp}"; Filename: "{app}\{#NombreExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#NombreExe}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#NombreApp}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Solo lo que el juego crea dentro de la carpeta del programa (la copia del disco), y la carpeta si queda vacia. Nunca
; la carpeta entera: si el jugador instalo en una suya con mas cosas, esas no se tocan.
Type: filesandordirs; Name: "{app}\data"
Type: filesandordirs; Name: "{app}\data.extrayendo"
Type: filesandordirs; Name: "{app}\data.anterior*"
Type: dirifempty; Name: "{app}"

[Code]
#ifdef SinDatos
var
  PaginaIso: TInputFileWizardPage;

function ContieneTexto(const Texto, Parte: String): Boolean;
begin
  Result := Pos(Lowercase(Parte), Lowercase(Texto)) > 0;
end;

// Una .iso de la carpeta (sin subcarpetas) con "formula" o "f1" y ademas "05" en el nombre, o ''. Sin "05" no se
// propone ninguna: la de otro Formula One (p. ej. el 06, que puede estar en Descargas) se usaria sin preguntar en una
// instalacion silenciosa y fallaria con el codigo 2. El .exe la comprueba de verdad al copiarla.
function BuscarIso(const Carpeta: String): String;
var
  Busqueda: TFindRec;
begin
  Result := '';
  if (Carpeta = '') or not DirExists(Carpeta) then
    Exit;
  if FindFirst(AddBackslash(Carpeta) + '*.iso', Busqueda) then
  try
    repeat
      if ((Busqueda.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0) and
          (ContieneTexto(Busqueda.Name, 'formula') or ContieneTexto(Busqueda.Name, 'f1')) then
      begin
        if ContieneTexto(Busqueda.Name, '05') then
        begin
          Result := AddBackslash(Carpeta) + Busqueda.Name;
          Exit;
        end;
      end;
    until not FindNext(Busqueda);
  finally
    FindClose(Busqueda);
  end;
end;

// Descargas puede estar en otro disco: Shell Folders tiene la ruta ya expandida.
function CarpetaDescargas: String;
begin
  if not RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders',
      '{374DE290-123F-4565-9164-39C4925E467B}', Result) then
    Result := ExpandConstant('{%USERPROFILE}\Downloads');
end;

procedure InitializeWizard;
var
  Encontrada: String;
begin
  PaginaIso := CreateInputFilePage(wpSelectTasks, CustomMessage('IsoTitulo'), CustomMessage('IsoDescripcion'),
    CustomMessage('IsoTexto'));
  PaginaIso.Add(CustomMessage('IsoCampo'), CustomMessage('IsoFiltro'), '.iso');
  // /ISO="<ruta>" en la linea de ordenes manda (instalaciones silenciosas y pruebas); si no, Descargas y el Escritorio.
  Encontrada := ExpandConstant('{param:ISO|}');
  if Encontrada = '' then
    Encontrada := BuscarIso(CarpetaDescargas);
  if Encontrada = '' then
    Encontrada := BuscarIso(ExpandConstant('{userdesktop}'));
  PaginaIso.Values[0] := Encontrada;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = PaginaIso.ID) and (Trim(PaginaIso.Values[0]) <> '') and
      not FileExists(Trim(PaginaIso.Values[0])) then
  begin
    SuppressibleMsgBox(CustomMessage('IsoNoExiste'), mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

// Las lineas de un flujo del .exe en el registro de /LOG, como mucho 60. Las "progress <0-1> <texto>" (para
// RecompLauncher, una por centesima) no van: solo la ultima.
procedure RegistrarSalida(const Flujo: String; const Lineas: TArrayOfString);
var
  I, Escritas: Integer;
  UltimoProgreso: String;
begin
  Escritas := 0;
  UltimoProgreso := '';
  for I := 0 to GetArrayLength(Lineas) - 1 do
    if Copy(Lineas[I], 1, 9) = 'progress ' then
      UltimoProgreso := Lineas[I]
    else if Escritas < 60 then
    begin
      Log(Flujo + ': ' + Lineas[I]);
      Escritas := Escritas + 1;
    end;
  if UltimoProgreso <> '' then
    Log(Flujo + ' (ultima progress): ' + UltimoProgreso);
  if Escritas = 60 then
    Log(Flujo + ': ... (se cortan las demas lineas)');
end;

// Tras copiar los archivos: el .exe comprueba la ISO y la copia en data con su ventana de progreso (y sus mensajes de
// error). Si no se copia, el juego la pide al abrirse.
// En silencio (/SILENT, /VERYSILENT) va sin --ventana: una ventana del .exe (error o progreso, salen visibles aunque
// se oculte el proceso) dejaria la instalacion esperando sin limite y sin rastro (F1 06 fallos-con-log T9).
// En los dos casos la orden, el codigo de salida (0 hecho, 1 uso, 2 no es Formula One 05 PAL, 3 lectura/escritura/
// espacio, 4 cancelado) y las lineas [disco] del .exe quedan en el registro de /LOG.
procedure CopiarIso;
var
  Iso, Parametros: String;
  Codigo: Integer;
  Salida: TExecOutput;
  Hecho: Boolean;
begin
  Iso := Trim(PaginaIso.Values[0]);
  if Iso = '' then
    Exit;
  WizardForm.StatusLabel.Caption := CustomMessage('IsoCopiando');
  Parametros := '--extraer-iso "' + Iso + '"';
  if not WizardSilent then
    Parametros := Parametros + ' --ventana';
  Log('Copia de la ISO: ' + ExpandConstant('{app}\{#NombreExe}') + ' ' + Parametros);
  try
    Hecho := ExecAndCaptureOutput(ExpandConstant('{app}\{#NombreExe}'), Parametros, ExpandConstant('{app}'),
      SW_SHOWNORMAL, ewWaitUntilTerminated, Codigo, Salida);
  except
    Hecho := False;
    Codigo := -1;
    Log('Copia de la ISO: ' + GetExceptionMessage);
  end;
  if Hecho then
  begin
    RegistrarSalida('salida', Salida.StdOut);
    RegistrarSalida('errores', Salida.StdErr);
    Log('Copia de la ISO: codigo de salida ' + IntToStr(Codigo));
  end
  else if Codigo <> -1 then
    Log('Copia de la ISO: no se pudo ejecutar (' + SysErrorMessage(Codigo) + ')');
  if not Hecho or (Codigo <> 0) then
    SuppressibleMsgBox(CustomMessage('IsoSinCopiar'), mbInformation, MB_OK, IDOK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    CopiarIso;
end;

// La copia del disco en Carpeta: data, su copia a medias (data.extrayendo) y las incompletas que aparto el juego
// (data.anterior, data.anterior-2...).
procedure BorrarCopiasDelDisco(const Carpeta: String);
var
  Busqueda: TFindRec;
begin
  DelTree(Carpeta + '\data', True, True, True);
  DelTree(Carpeta + '\data.extrayendo', True, True, True);
  if FindFirst(Carpeta + '\data.anterior*', Busqueda) then
  try
    repeat
      if ((Busqueda.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and
          ((CompareText(Busqueda.Name, 'data.anterior') = 0) or
           (CompareText(Copy(Busqueda.Name, 1, 14), 'data.anterior-') = 0)) then
        DelTree(Carpeta + '\' + Busqueda.Name, True, True, True);
    until not FindNext(Busqueda);
  finally
    FindClose(Busqueda);
  end;
end;
#endif

// Al desinstalar: la copia del disco que el juego hizo en %LOCALAPPDATA%\F1-05 (instalado en Archivos de programa sin
// elegir la ISO al instalar) se borra siempre, como la de {app}\data: se rehace con la ISO. Solo en la publica: la de
// uso personal lleva data\ en {app} y nunca escribe ahi. Las partidas y opciones del usuario (Documentos\My Games\F1-05)
// y los registros y la cache (%LOCALAPPDATA%\F1-05) solo se borran si dice que si (por defecto, y en silencio, no).
// Nunca se tocan las carpetas de F1 06.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Usuario, Registros: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  Usuario := ExpandConstant('{userdocs}\My Games\F1-05');
  Registros := ExpandConstant('{localappdata}\F1-05');
#ifdef SinDatos
  BorrarCopiasDelDisco(Registros);
#endif
  // /SILENT no calla SuppressibleMsgBox (solo /SUPPRESSMSGBOXES): sin esto la pregunta pararia la desinstalacion
  // silenciosa.
  if UninstallSilent or (not DirExists(Usuario) and not DirExists(Registros)) then
    Exit;
  if SuppressibleMsgBox(CustomMessage('BorrarPartidas'),
      mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
  begin
    DelTree(Usuario, True, True, True);
    DelTree(Registros, True, True, True);
  end;
end;
