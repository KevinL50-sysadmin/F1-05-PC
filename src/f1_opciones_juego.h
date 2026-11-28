// Formula One 05: pure helpers of the PC options inside the game's own menus (odd/tasks/opciones-pc.md T4, ported from
// F1 06's src/f1_overrides.cpp). Header only and without the runtime, so ps2xTest (ps2_f1_opciones_juego_tests.cpp)
// tests the same code the game runs. The hooks that use them are in f1_overrides.cpp.
#pragma once

#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace f1::opciones_juego
{
    // Name of the original chunk asked for by the served text (loadstring("", "=ps2x:<chunk>")).
    constexpr const char *kPrefijoOriginal = "=ps2x:";
    // os.getenv keys of the bridge.
    constexpr const char *kPrefijoClave = "ps2x:";
    constexpr size_t kLargoClave = 64u; // characters of an os.getenv key that are read

    // Lua heap (0x29B798(size, 4) destroys and recreates the active heap, *0x41BC54; realloc_lua_heap 0x131A98 calls
    // it): 512 KB at boot, 1 MB, 2 MB in the menus and 1 MB in the race (odd/tasks/opciones-pc.md L4). As in F1 06,
    // the menus' heap is raised to 4 MB for the PC pages. The heap object: buffer at +0, size at +4 (0x29C618).
    constexpr uint32_t kMontonMenus = 0x00200000u;
    constexpr uint32_t kMontonMenusPc = 0x00400000u;
    constexpr uint32_t kPunteroMonton = 0x0041BC54u;
    // F1_OPCIONES_ROTAS=3 (only to test the fallback to the size asked for, R4-003): the menus' heap is asked with a
    // size the game's malloc (guest RAM, 32 MB) can never give, so the creation gets no buffer.
    constexpr uint32_t kMontonImposible = 0x01F00000u;
    // 0x29B798's resume points (the returns of its calls to 0x29C690, 0x1010E0, the game's malloc 0x23BFB8 and
    // 0x29C618; register_functions.cpp): a call resumed there is dispatched by that address, not by 0x29B798, so the
    // heap hook is installed on them too (as lua_load's 0x29DF34/0x29DF44).
    inline constexpr uint32_t kMontonReanudar[] = {0x0029B7C8u, 0x0029B7D0u, 0x0029B7E0u, 0x0029B7F0u};

    // T5: OPTIONS (FrontEnd/Scripts/Profiles2005/EditProfile.lua, ClassTextMenu, loaded with the front end at boot) and
    // the pause's OPTIONS (PauseMenu/Profiles2005_PAUSE/EditProfile.lua, ClassPauseMenu, loaded when the race starts)
    // get one entry right after DISPLAY SETTINGS: PC OPTIONS / OPCIONES PC (by game.language() at each activation),
    // with the same outcome goDisplaySettings (the same graph node, as in F1 06) and a callback that asks for the PC
    // pages' list (ps2x:pagina 5); DISPLAY SETTINGS' own callback is wrapped to ask for the game's rows (ps2x:pagina 0).
    // Until T6 the PC OPTIONS entry opens the stock DISPLAY SETTINGS.
    // Both chunks fill five fixed tables (mMenuTextItems, mMenuOutcomeItems, mHelpText, mMenuSelectableItems,
    // mMenuCallbackItems) when they load. Their customActivate (in a race, and always in the pause) writes nil at
    // indices 5 and 6 and then calls Menu:onActivate(), whose ReActivateMenu (NavigationBars.lua) rebuilds
    // mMenuObjects from 1 to table.getn(mMenuTextItems). So the entry is never in the tables while customActivate
    // runs: the wrapped customActivate takes it out (exact inverse of the insertion) and the wrapped onActivate puts it
    // back, once, at the position after goDisplaySettings, shifting each table only up to its first hole (the 2-player
    // CHANGE CONTROLLER at index 7, behind the holes 5-6, keeps its index). onSelect calls mMenuCallbackItems[7]
    // instead of userSelect for item 7 (the 2-player entry): with the entry in, item 7 of the front end's OPTIONS is
    // PLAYER DETAILS, so for an item 7 that is not that entry (outcome "fucked") the call goes to userSelect.
    // The bridge check of T4 stays: at load it reads ps2x:vsync and sends it back ("[f1-05:opciones] puente ok
    // vsync=<v>"); each insertion logs its position (ps2x:entrada, ps2x:entrada_pausa). At most 24 locals at once in
    // the chunk's main function (F1 06), with ps2x_original: 12 here.
    constexpr const char *kExtensionOpciones = R"lua(
if Menu and Menu.mMenuOutcomeItems and Menu.mMenuTextItems and Menu.mMenuCallbackItems and Menu.customActivate
   and Menu.onActivate and os and os.getenv then
  local G = os.getenv
  G("ps2x:prueba", G("ps2x:vsync"))
  local M = Menu
  local pc = {clave = "ps2x:entrada"}
  if M.onTryCheatL1 then pc.clave = "ps2x:entrada_pausa" end
  local tablas = {"mMenuTextItems", "mMenuOutcomeItems", "mHelpText", "mMenuSelectableItems", "mMenuCallbackItems"}
  local function meter(t, p, v)
    local f = p
    while t[f] ~= nil do f = f + 1 end
    local i = f
    while i > p do t[i] = t[i - 1] i = i - 1 end
    t[p] = v
    return f
  end
  local function sacar(t, p, f)
    local i = p
    while i < f do t[i] = t[i + 1] i = i + 1 end
    t[f] = nil
  end
  local function poner()
    if pc.p then return end
    local k = nil
    for i, v in pairs(M.mMenuOutcomeItems) do
      if v == "goDisplaySettings" and type(i) == "number" then k = i end
    end
    if not k then return end
    local llamada = M.mMenuCallbackItems[k]
    if llamada == nil or llamada ~= pc.envuelta then
      pc.envuelta = function(m)
        G("ps2x:pagina", 0)
        if llamada then return llamada(m) end
      end
      M.mMenuCallbackItems[k] = pc.envuelta
    end
    local titulo = "PC OPTIONS"
    local nota = "PC settings: graphics, display and sound."
    if game and game.language and game.language() == game.spanish then
      titulo = "OPCIONES PC"
      nota = "Ajustes del PC: imagen, pantalla y sonido."
    end
    local valores = {titulo, "goDisplaySettings", nota, true, function() G("ps2x:pagina", 5) end}
    pc.p = k + 1
    pc.f = {}
    for n = 1, 5 do
      if M[tablas[n]] then pc.f[n] = meter(M[tablas[n]], pc.p, valores[n]) end
    end
    G(pc.clave, pc.p)
  end
  local function quitar()
    if not pc.p then return end
    for n = 1, 5 do
      if M[tablas[n]] and pc.f[n] then sacar(M[tablas[n]], pc.p, pc.f[n]) end
    end
    pc.p = nil
  end
  local activar = M.customActivate
  M.customActivate = function(m)
    quitar()
    return activar(m)
  end
  local activa = M.onActivate
  M.onActivate = function(m)
    poner()
    return activa(m)
  end
  if onSelect then
    local seleccionar = onSelect
    onSelect = function(a, b)
      local o = M.mMenuObjects and M.mCurrentMenuItem and M.mMenuObjects[M.mCurrentMenuItem]
      if pc.p and M.mCurrentMenuItem == 7 and o and o.outcome ~= "fucked" then
        local c = M.mMenuCallbackItems[7]
        M.mMenuCallbackItems[7] = function() M:userSelect() end
        seleccionar(a, b)
        M.mMenuCallbackItems[7] = c
        return
      end
      return seleccionar(a, b)
    end
  end
end
)lua";

    struct Extension
    {
        const char *trozo; // claveTrozo of the game's chunk
        const char *texto;
    };

    // T6 (odd/tasks/opciones-pc.md; a rewrite of F1 06's kExtensionPantalla for F1 05's DisplaySettings): DISPLAY
    // SETTINGS (FrontEnd/Scripts/Profiles2005/DisplaySettings.lua, and the pause's, PauseMenu/Profiles2005_PAUSE/) hosts
    // the PC pages with the game's own classes. Both chunks set Menu = ClassConfigMenu with fixed mMenuObjects /
    // mHelpText / mMenuTitle at load; gClassConfigMenu.onActivate (NavigationBars.lua:824) builds the screen from them
    // (title, help, mButtonAdjustFunc, rows 30 apart from gMetrics.menuStartY, the row under the cursor) and resets
    // mCurrentMenuItem if it is past the last row. Menu.onActivate is wrapped: ps2x:pagina 0 (or an unknown page) puts
    // the game's own rows, help, title and buttons back and is the stock screen; 5 is the PC OPTIONS list
    // (ClassConfigMenuButton rows, SELECT/BACK buttons) and 1-3 the value pages, whose rows are built on demand from a
    // text chunk of their own (=ps2x:graficos-pc ..., kPaginas: only in the Lua heap while the page is open). Opening
    // a value page takes the values F10/F11 changed (ps2x:abrir) and keeps them (ps2x:recordar). The cursor goes to the
    // first row when the page changes (and back to the page's entry on the list); a page rebuilt with an arrow held
    // keeps each row's mLastUpdate (the toggle's 0.19 s repeat).
    // The handlers are globals the game looks up when called (onSelect, onBack, onItemIncrease, onItemDecrease; the
    // pause's P1*/W* call them with the pad, checked against game.pausepad()):
    // - X on the list opens that page in the same node (Menu:onDeactivate, ps2x:pagina, Menu:onActivate); X on a value
    //   page saves settings.ini (ps2x:guardar) and goes back to the list.
    // - TRIANGLE on a value page discards (Menu:revertValues and ps2x:descartar: the values of when it was opened) and
    //   goes back to the list; on the list it leaves like the stock screen (page 0 again).
    // - Left/right: the game's own; a row that asks for it (DEFAULT VALUES: ps2x:por_defecto, SUPERSAMPLING: it may
    //   turn DOUBLE RESOLUTION off) has the page rebuilt.
    // PC rows only use the bridge (never game.*), so the profile is not modified and OPTIONS asks for no memory card
    // save. Texts by game.language() == game.spanish at each activation, accents as UTF-8 decimal escapes; ON/OFF are
    // the game's keys. At most 24 locals at once in a function (F1 06): the main function has 15 with ps2x_original.
    // A Lua error inside the game's handlers is not caught by anyone: the game's panic calls exit(1) (0x29F77C; seen
    // with a slider without its onInit fields, logs/pag-r2). So on a PC page the rows, the game's onActivate over them
    // and the arrows run under pcall: an error goes to the log (ps2x:error_letra, ps2x:error: lineaErrorPagina) and
    // the stock screen is shown.
    // T8b (S6): a page shown in the same node gets the game's own screen transition. Between graph nodes the engine
    // calls the chunk's onTransitionIn / onTransitionOut(t, total) each frame (total 0.5 s, t and os.time() in
    // seconds; logs/anim-r1), which call Menu:onTransition(p) (gClassConfigMenu.onTransition, NavigationBars.lua:939:
    // row k at x = (mMaxWidth + 64 k) (sin(p pi/2) - 1), off screen at 0) and draw.alphafade. A page swap in the same
    // node got neither, so its rows just appeared. Now abrir starts S.anim and Menu.onRender (the pause's
    // onCustomRender calls it too) plays both halves over the engine's duration: the old rows leave (onTransitionOut's
    // curve), the page is swapped halfway, the new rows come in (onTransitionIn's curve). X, Triangle or an arrow in
    // the first half only finish the swap; the engine's own transitions and leaving the node cancel it; a frame's
    // Lua error is logged and leaves nothing faded.
    constexpr const char *kExtensionPantalla = R"lua(
if Menu and Menu.onActivate and Menu.onDeactivate and Menu.revertValues and Menu.mMenuObjects and onSelect and onBack
   and onItemIncrease and onItemDecrease and os and os.getenv then
  local G = os.getenv
  local M = Menu
  local S = {objetos = M.mMenuObjects, ayuda = M.mHelpText, titulo = M.mMenuTitle, botones = M.mButtonAdjustFunc,
    pausa = M.onCustomRender ~= nil, fila = 1}
  local cuerpos = {[1] = "=ps2x:graficos-pc", [2] = "=ps2x:ajustes-pc", [3] = "=ps2x:pantalla-pc",
    [5] = "=ps2x:opciones-pc"}
  local titulos = {[1] = {"GR\195\129FICOS PC", "PC GRAPHICS"}, [2] = {"AJUSTES PC", "PC SETTINGS"},
    [3] = {"PANTALLA PC", "PC DISPLAY"}, [5] = {"OPCIONES PC", "PC OPTIONS"}}
  local A = {G = G, pausa = S.pausa}
  A.T = function(es, en)
    if game and game.language and game.language() == game.spanish then return es end
    return en
  end
  A.rehacer = function() S.rehacer = true end
  -- A Lua error of a PC page (e): its message to the log, a character at a time, and then the page.
  A.avisar = function(n, e)
    e = tostring(e)
    for i = 1, math.min(string.len(e), 200) do G("ps2x:error_letra", string.byte(e, i)) end
    G("ps2x:error", n)
  end
  -- A runtime option: values {{text, number}}, read and set live through the bridge; tras after a change.
  A.fila = function(lista, clave, nombre, valores, ayuda, tras)
    local item = ClassConfigMenuToggleItem()
    item.mTitle = nombre
    item.mValueMappings = valores
    item.setGetFunction = function(v)
      if v == nil then return G("ps2x:" .. clave) end
      G("ps2x:" .. clave, v)
      if tras then tras() end
    end
    table.insert(lista, {item, ayuda})
  end
  -- 0-100 in the runtime, 0-1 on the game's slider (10 % a step). gClassConfigMenu.onActivate does not call the
  -- items' onInit (the game calls it at load): its defaults here, without its draw state call.
  A.deslizador = function(lista, clave, nombre, ayuda)
    local item = ClassSlider()
    item.mTitle = nombre
    item.mMin = 0
    item.mMax = 1
    item.mInc = function(p) return p / 10 end
    item.mGetValStr = function(v) return string.format("%d%%", math.floor(v * 100 + 0.5)) end
    item.mVolumeGroup = sfx.volumesfx
    item.setGetFunction = function(v)
      if v == nil then
        local actual = G("ps2x:" .. clave)
        if actual then return actual / 100 end
        return 0
      end
      if v < 0 then v = 0 end
      if v > 1 then v = 1 end
      G("ps2x:" .. clave, math.floor(v * 100 + 0.5))
    end
    table.insert(lista, {item, ayuda})
  end
  A.defecto = function(lista, ayuda)
    local item = ClassConfigMenuToggleItem()
    local aplicar = A.T("APLICAR", "APPLY")
    item.mTitle = A.T("VALORES POR DEFECTO", "DEFAULT VALUES")
    item.mValueMappings = {{aplicar, 1}, {aplicar, 0}, {aplicar, 2}}
    item.setGetFunction = function(v)
      if v == nil then return 0 end
      if v ~= 0 then
        G("ps2x:por_defecto")
        S.rehacer = true
      end
    end
    table.insert(lista, {item, ayuda})
  end
  -- An entry of the list: X opens page n.
  A.entrada = function(lista, n, nombre, ayuda)
    local item = {}
    if ClassConfigMenuButton then
      item = ClassConfigMenuButton()
    else
      for k, f in pairs(gClassConfigMenuButton) do item[k] = f end
    end
    item.mTitle = nombre
    table.insert(lista, {item, ayuda, n})
  end
  local function filas(n)
    local lista = {}
    local cuerpo = loadstring("", cuerpos[n])
    if cuerpo then
      setfenv(cuerpo, getfenv(1))
      cuerpo()(lista, A)
    end
    return lista
  end
  local activar = M.onActivate
  -- The stock screen (page 0).
  local function delJuego(self)
    S.filas = nil
    self.mMenuObjects = S.objetos
    self.mHelpText = S.ayuda
    self.mMenuTitle = S.titulo
    self.mButtonAdjustFunc = S.botones
    return activar(self)
  end
  M.onActivate = function(self)
    local n = G("ps2x:pagina") or 0
    local antes = nil
    if S.rehaciendo then antes = S.filas end
    S.rehacer = nil
    S.filas = nil
    if cuerpos[n] then
      if n ~= 5 and not antes then
        G("ps2x:abrir")
        G("ps2x:recordar")
      end
      local hechas, f = pcall(filas, n)
      if hechas then S.filas = f else A.avisar(n, f) end
      if S.filas and table.getn(S.filas) == 0 then S.filas = nil end
    end
    if not S.filas then n = 0 end
    if n ~= S.ultima then
      self.mCurrentMenuItem = 1
      S.ultima = n
    end
    if S.cursor then
      self.mCurrentMenuItem = S.cursor
      S.cursor = nil
    end
    if not S.filas then return delJuego(self) end
    self.mMenuObjects = {}
    self.mHelpText = {}
    for k = 1, table.getn(S.filas) do
      self.mMenuObjects[k] = S.filas[k][1]
      self.mHelpText[k] = S.filas[k][2]
    end
    self.mMenuTitle = A.T(titulos[n][1], titulos[n][2])
    self.mButtonAdjustFunc = S.botones
    if n == 5 and gNavigationButtons and gNavigationButtons.setSelectBack then
      self.mButtonAdjustFunc = gNavigationButtons.setSelectBack
    end
    local bien, r = pcall(activar, self)
    if not bien then
      A.avisar(n, r)
      G("ps2x:pagina", 0)
      S.ultima = 0
      self.mCurrentMenuItem = 1
      return delJuego(self)
    end
    if antes then
      for k = 1, table.getn(S.filas) do
        if antes[k] and antes[k][1].mLastUpdate then S.filas[k][1].mLastUpdate = antes[k][1].mLastUpdate end
      end
    end
    return r
  end
  -- T8b: the swap of a page asked by abrir (S.anim.pagina = {page, cursor}), in the same node.
  S.cambiar = function()
    local c = S.anim and S.anim.pagina
    if not c then return end
    S.anim.pagina = nil
    S.cambiando = true
    M:onDeactivate()
    G("ps2x:pagina", c[1])
    S.cursor = c[2]
    M:onActivate()
    S.cambiando = nil
  end
  -- One frame of the transition, f = the fraction of S.duracion gone (by os.time, or by frames if the clock stands):
  -- in the first half the old rows leave as DisplaySettings' onTransitionOut moves them, halfway the page is swapped,
  -- and in the second half the new rows come in as its onTransitionIn moves them, with the same draw.alphafade.
  S.paso = function()
    local a = S.anim
    a.cuadros = a.cuadros + 1
    local f = math.max((os.time() - a.inicio) / S.duracion, a.cuadros / (60 * S.duracion))
    if f >= 0.5 then S.cambiar() end
    if f >= 1 then
      S.anim = nil
      M:onTransition(1)
      draw.alphafade(1)
    elseif a.pagina then
      M:onTransition(math.max(0, (0.5 - f) * 2))
      draw.alphafade(1 - f)
    else
      M:onTransition(math.max(0, (f - 0.5) * 2))
      draw.alphafade(f)
    end
  end
  S.duracion = 0.5
  S.dibujar = M.onRender
  M.onRender = function(self)
    if S.anim then
      local bien, e = pcall(S.paso)
      if not bien then
        S.anim = nil
        A.avisar(S.ultima or 0, e)
        pcall(draw.alphafade, 1)
        if self.mMenuItemCount == nil then
          G("ps2x:pagina", 0)
          pcall(self.onActivate, self)
        end
      end
    end
    return S.dibujar(self)
  end
  S.desactivar = M.onDeactivate
  M.onDeactivate = function(self)
    if not S.cambiando and not S.rehaciendo then
      S.anim = nil
    end
    return S.desactivar(self)
  end
  S.entrar, S.salir = onTransitionIn, onTransitionOut
  if S.entrar then
    onTransitionIn = function(t, total)
      if total and total > 0 then S.duracion = total end
      S.anim = nil
      return S.entrar(t, total)
    end
  end
  if S.salir then
    onTransitionOut = function(t, total)
      S.anim = nil
      return S.salir(t, total)
    end
  end
  -- Page n in the same node, with the transition (page 0, after an error: at once); cursor: the row to start on.
  local function abrir(n, cursor)
    S.anim = {inicio = os.time(), cuadros = 0, pagina = {n, cursor}}
    if n == 0 then
      S.cambiar()
      S.anim = nil
    end
  end
  local function mio(a, b)
    return a == 1 and S.filas and M.mOutcomeSent == false and (not S.pausa or b == nil or game.pausepad() == b)
  end
  local seleccionar = onSelect
  onSelect = function(a, b)
    if not mio(a, b) then return seleccionar(a, b) end
    if S.anim and S.anim.pagina then return S.cambiar() end
    if S.ultima == 5 then
      local f = S.filas[M.mCurrentMenuItem]
      if f and f[3] then
        sfx.play(sfx.accept)
        S.fila = M.mCurrentMenuItem
        abrir(f[3])
      end
      return
    end
    G("ps2x:guardar")
    sfx.play(sfx.accept)
    abrir(5, S.fila)
  end
  local volver = onBack
  onBack = function(a, b)
    if mio(a, b) then
      if S.anim and S.anim.pagina then return S.cambiar() end
    end
    if mio(a, b) and S.ultima ~= 5 then
      M:revertValues()
      G("ps2x:descartar")
      sfx.play(sfx.cancel)
      return abrir(5, S.fila)
    end
    if mio(a, b) then G("ps2x:pagina", 0) end
    return volver(a, b)
  end
  local function trasCambiar(original)
    return function(a, b)
      if not S.filas then return original(a, b) end
      if S.anim and S.anim.pagina then return S.cambiar() end
      local bien, r = pcall(original, a, b)
      if not bien then
        A.avisar(S.ultima, r)
        return abrir(0)
      end
      if S.rehacer and S.filas then
        S.rehaciendo = true
        M:onDeactivate()
        M:onActivate()
        S.rehaciendo = nil
      end
      return r
    end
  end
  onItemIncrease = trasCambiar(onItemIncrease)
  onItemDecrease = trasCambiar(onItemDecrease)
end
)lua";

    // T7 (S4, "Primero las del port"): the pages, with only the options F1 05's runtime applies itself (applyLive and
    // the display/aspect/GS code; patch 0230 leaves the PC extras out). fov since T10 (patch 0245 keeps it out of the
    // extras; f1_overrides.cpp widens the race cameras with it, f1_campo_vision.h). mostrar_fps since T9: the runtime's
    // counter draws only with a game letter it knows (ps2_game_font.h), and F1 05's Frontend/Fonts/Futurah*Px.bft are
    // captured as the game reads them (src/f1_letra.h, odd/tasks/opciones-pc.md T9). Each body is a chunk of its own, served by
    // name (=ps2x:<name>) when kExtensionPantalla opens the page; it returns a function(lista, A) that adds the rows
    // with A's helpers (fila, deslizador, defecto, entrada, T, G, rehacer, pausa).
    constexpr const char *kPaginaOpcionesPc = R"lua(
return function(lista, A)
  local T = A.T
  A.entrada(lista, 1, T("GR\195\129FICOS PC", "PC GRAPHICS"),
    T("Filtro de imagen, supersampling, doble resoluci\195\179n, sincronizaci\195\179n vertical, contador de FPS y modo de pantalla.",
      "Image filter, supersampling, double resolution, vertical sync, FPS counter and screen mode."))
  A.entrada(lista, 3, T("PANTALLA PC", "PC DISPLAY"),
    T("Forma de la imagen, tama\195\177o de la ventana y dibujo de los gr\195\161ficos (al reiniciar).",
      "Picture shape, window size and graphics renderer (on restart)."))
  A.entrada(lista, 2, T("AJUSTES PC", "PC SETTINGS"),
    T("Volumen, latencia del sonido, sonido mono, vibraci\195\179n y campo de visi\195\179n.",
      "Volume, audio latency, mono sound, vibration and field of view."))
end
)lua";

    constexpr const char *kPaginaGraficosPc = R"lua(
return function(lista, A)
  local T = A.T
  local onoff = {{"OFF", 0}, {"ON", 1}}
  A.fila(lista, "filtro", T("FILTRO DE IMAGEN", "IMAGE FILTER"),
    {{T("CERCANO", "NEAREST"), 0}, {T("BILINEAL", "BILINEAR"), 1}, {T("N\195\141TIDO", "SHARP"), 2}, {"FSR 1", 3}, {"CRT", 4}},
    T("C\195\179mo se ampl\195\173a la imagen: p\195\173xeles, suave, n\195\173tida, FSR 1 (ampl\195\173a y enfoca) o CRT (televisi\195\179n antigua).",
      "How the picture is scaled: pixels, smooth, sharp, FSR 1 (upscales and sharpens) or CRT (old TV look)."))
  A.fila(lista, "supersampling", "SUPERSAMPLING", {{"X1", 1}, {"X2", 2}, {"X4", 4}, {"X8", 8}, {"X16", 16}},
    T("Bordes m\195\161s suaves (dibujo por GPU). Cuanto m\195\161s alto, m\195\161s trabajo para la tarjeta gr\195\161fica.",
      "Smoother edges (GPU renderer). The higher, the more work for the graphics card."), A.rehacer)
  A.fila(lista, "alta_resolucion", T("DOBLE RESOLUCI\195\147N", "DOUBLE RESOLUTION"), onoff,
    T("La imagen con el doble de ancho y de alto. Necesita supersampling X4 o m\195\161s.",
      "The picture at twice the width and height. Needs supersampling X4 or more."))
  A.fila(lista, "vsync", T("SINCRONIZACI\195\147N VERTICAL", "VERTICAL SYNC"), onoff,
    T("La imagen cambia al ritmo del monitor, sin cortes. El juego va a la misma velocidad.",
      "The picture changes with the monitor, without tearing. The game runs at the same speed."))
  A.fila(lista, "mostrar_fps", T("CONTADOR DE FPS", "FPS COUNTER"), onoff,
    T("Las im\195\161genes por segundo del juego (por ejemplo 50 FPS), arriba a la derecha, con la letra del juego.",
      "The game's frames per second (for example 50 FPS), top right, in the game's own letter."))
  A.fila(lista, "pantalla", T("MODO DE PANTALLA", "SCREEN MODE"),
    {{T("VENTANA", "WINDOWED"), 0}, {T("SIN BORDES", "BORDERLESS"), 1}, {T("COMPLETA", "FULL SCREEN"), 2}},
    T("Ventana, ventana sin bordes del tama\195\177o del monitor o pantalla completa. F11 la cambia jugando.",
      "A window, a borderless window the size of the monitor or full screen. F11 changes it while playing."))
  A.defecto(lista, T("Izquierda o derecha: las filas de esta p\195\161gina vuelven a sus valores por defecto. X los guarda.",
    "Left or right: the rows of this page go back to their default values. X saves them."))
end
)lua";

    constexpr const char *kPaginaAjustesPc = R"lua(
return function(lista, A)
  local T = A.T
  A.deslizador(lista, "volumen", T("VOLUMEN GENERAL", "MASTER VOLUME"),
    T("El volumen de todo el sonido del juego.", "The volume of all the game's sound."))
  A.fila(lista, "latencia_audio", T("LATENCIA DEL SONIDO", "AUDIO LATENCY"),
    {{T("AUTOM\195\129TICA", "AUTOMATIC"), 0}, {T("BAJA", "LOW"), 1}, {T("ALTA", "HIGH"), 2}},
    T("Autom\195\161tica: de 43 a 128 ms seg\195\186n el PC. Baja: 43 ms. Alta: 128 ms, para un PC lento.",
      "Automatic: 43 to 128 ms as the PC needs. Low: 43 ms. High: 128 ms, for a slow PC."))
  A.fila(lista, "audio_mono", T("SALIDA DE SONIDO", "SOUND OUTPUT"), {{T("EST\195\137REO", "STEREO"), 0}, {"MONO", 1}},
    T("Mono: los dos canales juntos, para un solo altavoz o un solo o\195\173do.",
      "Mono: both channels together, for a single speaker or one ear."))
  A.deslizador(lista, "vibracion", T("VIBRACI\195\147N", "VIBRATION"),
    T("La fuerza de la vibraci\195\179n del mando y del volante. Al m\195\173nimo, sin vibraci\195\179n.",
      "How strong the pad and wheel vibration is. At the minimum, no vibration."))
  A.fila(lista, "fov", T("CAMPO DE VISI\195\147N", "FIELD OF VIEW"),
    {{"NORMAL", 0}, {"+5 %", 5}, {"+10 %", 10}, {"+15 %", 15}, {"+20 %", 20}, {"+25 %", 25}, {"+30 %", 30}},
    T("Las c\195\161maras de la carrera m\195\161s abiertas: se ve m\195\161s a los lados. El HUD y los men\195\186s no cambian.",
      "Wider race cameras: you see more to the sides. The HUD and the menus do not change."))
  A.defecto(lista, T("Izquierda o derecha: las filas de esta p\195\161gina vuelven a sus valores por defecto. X los guarda.",
    "Left or right: the rows of this page go back to their default values. X saves them."))
end
)lua";

    // The window sizes come from the host (ps2x:ventanas, ps2x:ventana_ancho/_alto, i; the row's value is the position
    // in that list) and the renderer not in use says "(ON RESTART)" (ps2x:gs_en_uso).
    constexpr const char *kPaginaPantallaPc = R"lua(
return function(lista, A)
  local T, G = A.T, A.G
  A.fila(lista, "aspecto", T("FORMA DE LA IMAGEN", "PICTURE SHAPE"),
    {{T("AUTOM\195\129TICA", "AUTOMATIC"), 0}, {"4:3", 1}, {"16:9", 2}, {T("P\195\141XELES CUADRADOS", "SQUARE PIXELS"), 3}},
    T("Autom\195\161tica: la de SCREEN SIZE en OPCIONES DE PANTALLA (4:3 o panor\195\161mica). P\195\173xeles cuadrados: sin ajustar. F10 la cambia jugando.",
      "Automatic: the one of SCREEN SIZE in DISPLAY SETTINGS (4:3 or widescreen). Square pixels: not adjusted. F10 changes it while playing."))
  local tamanos = {}
  for i = 0, (G("ps2x:ventanas") or 0) - 1 do
    table.insert(tamanos, {G("ps2x:ventana_ancho", i) .. " x " .. G("ps2x:ventana_alto", i), i})
  end
  if table.getn(tamanos) > 0 then
    A.fila(lista, "tamano_ventana", T("TAMA\195\145O DE LA VENTANA", "WINDOW SIZE"), tamanos,
      T("En modo ventana (MODO DE PANTALLA, en GR\195\129FICOS PC). Si no cabe en el escritorio, se ajusta a \195\169l.",
        "In windowed mode (SCREEN MODE, in PC GRAPHICS). If it does not fit on the desktop, it is fitted to it."))
  end
  local reiniciar = T(" (AL REINICIAR)", " (ON RESTART)")
  local cpu, gpu = "CPU", "GPU"
  if G("ps2x:gs_en_uso") == 1 then cpu = cpu .. reiniciar else gpu = gpu .. reiniciar end
  A.fila(lista, "gs", T("DIBUJO DE LOS GR\195\129FICOS", "GRAPHICS RENDERER"), {{cpu, 0}, {gpu, 1}},
    T("GPU: en la tarjeta gr\195\161fica (Vulkan), lo normal. CPU: modo Software, m\195\161s lento. Se aplica al reiniciar el juego.",
      "GPU: on the graphics card (Vulkan), the usual. CPU: Software mode, slower. It takes effect when the game restarts."))
  A.defecto(lista, T("Izquierda o derecha: las filas de esta p\195\161gina vuelven a sus valores por defecto. X los guarda.",
    "Left or right: the rows of this page go back to their default values. X saves them."))
end
)lua";

    // The pages' bodies by the name kExtensionPantalla asks for (loadstring("", "=ps2x:<name>")).
    inline constexpr Extension kPaginas[] = {
        {"opciones-pc", kPaginaOpcionesPc},
        {"graficos-pc", kPaginaGraficosPc},
        {"ajustes-pc", kPaginaAjustesPc},
        {"pantalla-pc", kPaginaPantallaPc},
    };

    // The body of a page by its key (claveTrozo of the =ps2x: name), or nullptr.
    inline const char *cuerpoPagina(const std::string &clave)
    {
        for (const Extension &e : kPaginas)
            if (clave == e.trozo)
                return e.texto;
        return nullptr;
    }

    // The runtime options of each PC page (ps2x:pagina), for ps2x:recordar, ps2x:descartar and ps2x:por_defecto: the
    // rows of kPaginas. F1 05's own list (the runtime's ps2x::gameOptionsPageKeys is F1 06's pages, with the volume
    // on PC GRAPHICS). 0 (the game's rows), 5 (the list) and any other page: none.
    inline const std::vector<std::string> &clavesPagina(int pagina)
    {
        static const std::vector<std::string> ninguna;
        static const std::vector<std::string> graficos = {"filtro", "supersampling", "alta_resolucion", "vsync",
                                                          "mostrar_fps", "pantalla"};
        static const std::vector<std::string> ajustes = {"volumen", "latencia_audio", "audio_mono", "vibracion",
                                                         "fov"};
        static const std::vector<std::string> pantalla = {"aspecto", "tamano_ventana", "gs"};
        switch (pagina)
        {
        case 1:
            return graficos;
        case 2:
            return ajustes;
        case 3:
            return pantalla;
        default:
            return ninguna;
        }
    }

    // The game's chunks with a PC extension.
    inline constexpr Extension kExtensiones[] = {
        {"frontend/scripts/profiles2005/editprofile.lua", kExtensionOpciones},
        {"frontend/scripts/pausemenu/profiles2005_pause/editprofile.lua", kExtensionOpciones},
        {"frontend/scripts/profiles2005/displaysettings.lua", kExtensionPantalla},
        {"frontend/scripts/pausemenu/profiles2005_pause/displaysettings.lua", kExtensionPantalla},
    };

    // F1_OPCIONES_ROTAS (only to test the fallbacks of odd/tasks/opciones-pc.md T5 in the real game): "1" serves the
    // extensions with a syntax error at the end (the game's chunk must load from its original), "2" does not keep the
    // originals (the =ps2x: load must fail), "3" asks the menus' Lua heap with kMontonImposible (the creation must fall
    // back to the size asked for). Anything else, or not set: 0, nothing broken.
    inline int pruebaRota(const char *valor)
    {
        if (valor == nullptr)
            return 0;
        if (std::strcmp(valor, "1") == 0)
            return 1;
        if (std::strcmp(valor, "2") == 0)
            return 2;
        if (std::strcmp(valor, "3") == 0)
            return 3;
        return 0;
    }

    // Appended to a served text by F1_OPCIONES_ROTAS=1: a syntax error.
    constexpr const char *kTextoRoto = "\n) -- F1_OPCIONES_ROTAS=1\n";

    // What a =ps2x: load gets when the original was not kept: a text that does not compile (an unexpected symbol in
    // the first statement), so loadstring returns nil and the error instead of an empty function.
    inline std::string textoSinOriginal(const std::string &nombre)
    {
        std::string comentario;
        for (const char c : nombre)
            comentario.push_back(c == '\n' || c == '\r' ? ' ' : c);
        return "-- F1 05 (PC): no se guardo el original de " + comentario + "\n@ps2x_sin_original\n";
    }

    // The log lines of a chunk that does not load (lua_load returned non-zero).
    inline std::string lineaExtensionNoCarga(const std::string &nombre, const std::string &mensaje)
    {
        return "[f1-05:opciones] extension de " + nombre + " no carga: " + mensaje + "; se usa el original";
    }

    inline std::string lineaNoCarga(const std::string &nombre, const std::string &mensaje)
    {
        return "[f1-05:opciones] " + nombre + " no carga: " + mensaje;
    }

    // The log line of a Lua error on a PC page (kExtensionPantalla's A.avisar: ps2x:error_letra, then ps2x:error).
    inline std::string lineaErrorPagina(int pagina, const std::string &mensaje)
    {
        return "[f1-05:opciones] pagina " + std::to_string(pagina) + ": error de Lua (" +
               (mensaje.empty() ? std::string("sin mensaje") : mensaje) + "); se muestra la pantalla del juego";
    }

    constexpr size_t kLargoMensaje = 240u; // characters of a Lua error message that are read

    // F1_OPCIONES_JUEGO: on when not set or empty; "0" (any value atoi reads as 0) turns everything of T4 off.
    inline bool activas(const char *valor)
    {
        return valor == nullptr || *valor == '\0' || std::atoi(valor) != 0;
    }

    // The key of a chunk name: '\' as '/', no leading '@' or '/', lower case (F1 05 mixes FrontEnd/, Frontend/ and
    // frontend/ in its names).
    inline std::string claveTrozo(std::string nombre)
    {
        std::replace(nombre.begin(), nombre.end(), '\\', '/');
        size_t inicio = 0;
        while (inicio < nombre.size() && (nombre[inicio] == '/' || nombre[inicio] == '@'))
            ++inicio;
        nombre.erase(0, inicio);
        std::transform(nombre.begin(), nombre.end(), nombre.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return nombre;
    }

    // True if `nombre` is "=ps2x:<chunk>" (exact prefix, chunk not empty): the served text asks for the original.
    inline bool nombreOriginal(const std::string &nombre, std::string &clave)
    {
        const size_t largo = std::strlen(kPrefijoOriginal);
        if (nombre.size() <= largo || nombre.compare(0, largo, kPrefijoOriginal) != 0)
            return false;
        clave = claveTrozo(nombre.substr(largo));
        return true;
    }

    // The extension of a chunk key, or nullptr.
    inline const char *extension(const std::string &clave)
    {
        for (const Extension &e : kExtensiones)
            if (clave == e.trozo)
                return e.texto;
        return nullptr;
    }

    // The text served in place of the chunk: the original (by "=ps2x:<name>", in the chunk's own environment) and then
    // the extension. The served text's main function only has the local ps2x_original (F1 06: at most 24 locals).
    inline std::string textoConExtension(const std::string &nombre, const char *extension)
    {
        std::string literal;
        for (const char c : nombre)
        {
            if (c == '\\' || c == '"')
                literal.push_back('\\');
            literal.push_back(c);
        }
        return std::string("-- F1 05 (PC): ") + nombre + " del juego y las opciones del PC\n" +
               "local ps2x_original = loadstring(\"\", \"" + kPrefijoOriginal + literal + "\")\n" +
               "if ps2x_original then setfenv(ps2x_original, getfenv(1)) ps2x_original() end\n" +
               "ps2x_original = nil\n" + (extension != nullptr ? extension : "");
    }

    inline uint32_t leer32(const uint8_t *rdram, uint32_t direccion)
    {
        uint32_t valor = 0;
        std::memcpy(&valor, rdram + (direccion & PS2_RAM_MASK & ~3u), sizeof(valor));
        return valor;
    }

    inline void escribir32(uint8_t *rdram, uint32_t direccion, uint32_t valor)
    {
        std::memcpy(rdram + (direccion & PS2_RAM_MASK & ~3u), &valor, sizeof(valor));
    }

    // The result as lua_pushnumber (0x29D4A0) writes it: TObject at L->top (type 3 and the float; type 0 = nil when
    // there is none) and L->top + 8. The hook then returns 1 result.
    inline void escribirResultado(uint8_t *rdram, uint32_t L, bool hay, float valor)
    {
        const uint32_t top = leer32(rdram, L + 8u);
        escribir32(rdram, top, hay ? 3u : 0u);
        std::memcpy(rdram + ((top + 4u) & PS2_RAM_MASK & ~3u), &valor, sizeof(valor));
        escribir32(rdram, L + 8u, top + 8u);
    }

    // The size the heap is created with: the menus' 2 MB becomes 4 MB with the options on (kMontonImposible with
    // F1_OPCIONES_ROTAS=3, to test the fallback); anything else as asked.
    inline uint32_t tamanoMonton(uint32_t pedido, bool activas, int rota = 0)
    {
        if (!activas || pedido != kMontonMenus)
            return pedido;
        return rota == 3 ? kMontonImposible : kMontonMenusPc;
    }

    struct Monton
    {
        uint32_t objeto = 0u;
        uint32_t bufer = 0u;
        uint32_t tamano = 0u;
    };

    // The active heap after 0x29B798: object *0x41BC54, buffer at +0 (0 if the game's malloc failed), size at +4.
    inline Monton leerMonton(const uint8_t *rdram)
    {
        Monton m;
        m.objeto = leer32(rdram, kPunteroMonton);
        if (m.objeto != 0u)
        {
            m.bufer = leer32(rdram, m.objeto);
            m.tamano = leer32(rdram, m.objeto + 4u);
        }
        return m;
    }

    // After a lua_load that failed, its error object is at L->top - 8 (TObject {type, value}): true if it is a string
    // (type 4; TString text at +16), with up to kLargoMensaje characters of it (control characters as spaces).
    inline bool mensajeDeError(const uint8_t *rdram, uint32_t L, std::string &mensaje)
    {
        const uint32_t base = leer32(rdram, L + 12u);
        const uint32_t top = leer32(rdram, L + 8u);
        if (top < base + 8u || leer32(rdram, top - 8u) != 4u)
            return false;
        const uint32_t texto = leer32(rdram, top - 4u) + 16u;
        mensaje.clear();
        for (size_t i = 0; i < kLargoMensaje; ++i)
        {
            const char c = static_cast<char>(rdram[(texto + i) & PS2_RAM_MASK]);
            if (c == '\0')
                break;
            mensaje.push_back(static_cast<unsigned char>(c) < 0x20u ? ' ' : c);
        }
        return true;
    }

    // lua_pop(L, 1) of that error object (L->top - 8), so a load tried again leaves the stack as one load does.
    // Nothing when the stack above L->base is empty.
    inline void quitarCima(uint8_t *rdram, uint32_t L)
    {
        const uint32_t base = leer32(rdram, L + 12u);
        const uint32_t top = leer32(rdram, L + 8u);
        if (top >= base + 8u)
            escribir32(rdram, L + 8u, top - 8u);
    }

    struct Argumentos
    {
        std::string clave; // without "ps2x:"
        bool conValor = false;
        float valor = 0.0f;
    };

    // os.getenv (0x2AE880, a C function: a0 = lua_State, arguments from L->base (+12) to L->top (+8), 8-byte TObjects
    // {type, value}): true if the first argument is a string (type 4; TString text at +16) starting with "ps2x:".
    // Then the key without it and, if the second argument is a number (type 3, float), that value.
    inline bool leerArgumentos(const uint8_t *rdram, uint32_t L, Argumentos &argumentos)
    {
        const uint32_t base = leer32(rdram, L + 12u);
        const uint32_t top = leer32(rdram, L + 8u);
        if (top < base + 8u || leer32(rdram, base) != 4u)
            return false;
        const uint32_t texto = leer32(rdram, base + 4u) + 16u;
        std::string clave;
        for (size_t i = 0; i < kLargoClave; ++i)
        {
            const char c = static_cast<char>(rdram[(texto + i) & PS2_RAM_MASK]);
            if (c == '\0')
                break;
            clave.push_back(c);
        }
        const size_t largo = std::strlen(kPrefijoClave);
        if (clave.compare(0, largo, kPrefijoClave) != 0)
            return false;
        argumentos.clave = clave.substr(largo);
        argumentos.conValor = top >= base + 16u && leer32(rdram, base + 8u) == 3u;
        argumentos.valor = 0.0f;
        if (argumentos.conValor)
            std::memcpy(&argumentos.valor, rdram + ((base + 12u) & PS2_RAM_MASK & ~3u), sizeof(argumentos.valor));
        return true;
    }
}
