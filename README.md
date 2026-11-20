# F1 05 PC: a native Windows port of Formula One 05 (PS2) by static recompilation

> **Coming soon. Work in progress: there is no download yet.** Watch this repository (Watch → Custom → Releases) to
> be told when the first release is out.

> **Fan project. Not affiliated with, endorsed by or connected to Sony Interactive Entertainment, SCE Studio Liverpool,
> Formula One Group, the FIA or any other rights holder.** No game data is or will be distributed: you will need your
> own copy of the game. [Español más abajo](#español).

**F1 05 PC** turns the European PlayStation 2 release of *Formula One 05* (PAL, SCES-53033) into a native Windows
program. It is not an emulator running the disc: the game's MIPS code is translated ahead of time into C++ with
[PS2Recomp](https://github.com/ran-j/PS2Recomp) and compiled into a normal `.exe`, together with a runtime that stands
in for the PS2 hardware and draws the graphics on your graphics card through
[paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) (Vulkan).

It is the sibling of [F1 06 PC](https://github.com/KevinL50-sysadmin/F1-06-PC), by the same author and on the same
foundations. The goal for the first release is the PS2 game **as it was**: faithful, complete and playable with a
controller.

<p align="center">
  <img src="docs/img/race-cockpit.png" alt="F1 05 PC: cockpit view in a race at Catalunya" width="49%">
  <img src="docs/img/quick-race-menu.png" alt="F1 05 PC: the quick race menu" width="49%">
</p>

## Status

- Boots, goes through the menus and plays a quick race to the results, with sound and a controller.
- Being worked on: performance in some cameras and circuits, the memory card (profile load), remaining bugs.
- Then: packaging, the first release here and on [RecompLauncher](https://recomplauncher.com).

The source code will be published here with the first release, under the GPL-3.0 license.

## Español

**F1 05 PC** convierte la versión europea de PlayStation 2 de *Formula One 05* (PAL, SCES-53033) en un programa nativo
de Windows mediante recompilación estática, como [F1 06 PC](https://github.com/KevinL50-sysadmin/F1-06-PC).
**Próximamente: todavía no hay descarga.** Pulsa Watch → Custom → Releases para enterarte cuando salga.

Proyecto de aficionado, sin relación con Sony Interactive Entertainment, SCE Studio Liverpool, Formula One Group, la FIA
ni ningún otro titular de derechos. No se distribuye ningún dato del juego: hará falta tu propia copia.

Estado: arranca, recorre los menús y juega una carrera rápida hasta los resultados, con sonido y mando. Se trabaja en el
rendimiento, la tarjeta de memoria y los fallos que quedan; después, el paquete, la primera versión y la ficha en
RecompLauncher. El código fuente se publicará aquí con la primera versión, bajo licencia GPL-3.0.
