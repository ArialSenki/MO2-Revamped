# Juegos

MO2 Revamped es compatible con todos los juegos que admite Mod Organizer 2. Para
los juegos de esta página tiene su propio plugin de juego, escrito para MO2
Revamped, con comprobaciones de los problemas más habituales.

En todos los juegos: **inícialo desde MO2.** Elige el programa en la barra
superior y pulsa **Ejecutar**. Desde Steam, GOG o su propio lanzador, el juego no
ve los mods.

Un plugin hecho para un juego, como las herramientas de Elden Ring, no se carga
en las instancias de otros juegos (ver [Ajustes > Plugins](ajustes.md#plugins)).
Los problemas que encuentra un plugin salen en la campana de notificaciones;
**Arreglarlo** los soluciona cuando puede.

- [Cyberpunk 2077](#cyberpunk-2077)
- [Baldur's Gate 3](#baldurs-gate-3)
- [The Witcher 3](#the-witcher-3)
- [Stardew Valley](#stardew-valley)
- [Valheim](#valheim)
- [Juegos de Bethesda](#juegos-de-bethesda)
- [Elden Ring y Nightreign](#elden-ring-y-nightreign)

Los nombres de las opciones de los plugins y algunas herramientas de juegos
(como **BG3 Tools**) están en inglés en el programa, y así aparecen aquí.

## Cyberpunk 2077

**Qué hace MO2 Revamped**

- Coloca los mods como los lee el juego: un mod necesita `archive`, `bin`,
  `engine`, `r6`, `red4ext`, `mods` o `root` arriba del todo. Otras formas se
  recolocan al instalar; las dudosas se dejan al instalador manual.
- Cyber Engine Tweaks y RED4ext se cargan con el sistema de archivos virtual de
  MO2; no se copia nada a la carpeta del juego.
- **REDmod:** los REDmods que cambian se despliegan antes de iniciar el juego.
  - **Cyberpunk 2077 - skip REDmod deploy** lo inicia sin desplegar.
  - **Manually deploy REDmod** solo despliega.
  - Los REDmods necesitan el DLC gratuito REDmod.
- Los archivos `r6/cache` del juego se copian a Overwrite y se renuevan después
  de una actualización del juego.
- **Orden de los archives:** el juego carga los archivos `.archive` por su
  nombre. Para usar el orden de los mods, activa **enforce_archive_load_order**
  y **reverse_archive_load_order** del plugin de Cyberpunk 2077 en Ajustes >
  Plugins: así gana el mod más abajo en la lista.
- Herramientas > **Conflictos de archives** muestra qué `.archive` gana donde
  dos mods cambian los mismos archivos.

Programas: **Cyberpunk 2077**, **Cyberpunk 2077 - skip REDmod deploy**,
**Manually deploy REDmod** y **REDprelauncher**.

Opciones del plugin (Ajustes > Plugins > Cyberpunk 2077 Support Plugin):

| Opción | Predeterminado | Qué hace |
| --- | --- | --- |
| `skipStartScreen` | no | Se salta la pantalla de inicio. |
| `enforce_archive_load_order` | no | Carga los `.archive` en el orden de los mods. |
| `reverse_archive_load_order` | no | Con la anterior, gana el mod más abajo en la lista. |
| `enforce_redmod_load_order` | sí | Despliega los REDmods en el orden de los mods. |
| `reverse_redmod_load_order` | no | Invierte ese orden. |
| `auto_deploy_redmod` | sí | Despliega los REDmods que cambian antes de iniciar el juego. |
| `clear_cache_after_game_update` | sí | Renueva la copia de la caché tras una actualización del juego. |

**Problemas habituales**

- **Falta un framework.** Casi todos los mods necesitan RED4ext, Cyber Engine
  Tweaks, redscript, ArchiveXL, TweakXL o Codeware; también se comprueban Input
  Loader y Audioware. Una notificación dice cuál falta.
- **Scripts que no compilan.** Cuando redscript no puede compilar los scripts de
  los mods, la notificación dice qué mods son. El registro completo es
  `r6/logs/redscript_rCURRENT.log` en Overwrite. Actualiza o desactiva esos mods.
- **Falló el despliegue de REDmod.** MO2 dice dónde está su registro y pregunta
  si iniciar el juego sin los últimos cambios de los REDmods.
- **Mods REDmod sin el DLC REDmod** y **archivos de REDmod desplegados fuera de
  MO2** tienen su notificación.

Wiki de la comunidad: <https://wiki.redmodding.org/cyberpunk-2077-modding>

## Baldur's Gate 3

**Qué hace MO2 Revamped**

- Los mods `.pak` van a la carpeta `Mods` del juego en tus documentos, los
  archivos del Script Extender a `Script Extender`, y el `modsettings.lsx` de
  cada perfil a `PlayerProfiles/Public`, todo con el sistema de archivos virtual.
- Acepta las formas que usan las listas de Wabbajack (`Root/`, `PAK_FILES/`,
  `SE_CONFIG/`) y carpetas sueltas como `Public`, `Mods`, `Generated` o
  `Localization`.
- Los mods `.pak` entran en el orden de carga de la pestaña **Plugins**;
  `modsettings.lsx` se escribe al iniciar el juego.
- Detecta el Script Extender (`bin/DWrite.dll`, también en una carpeta `Root`),
  y las DLL de los mods se cargan solo en el juego, no en el Larian Launcher.
- Herramientas > **BG3 Tools**:
  - **Load Order** lista los módulos con sus problemas; **Move needed mods
    up** pone cada mod después de los que necesita.
  - **Check For LsLib Updates**, **Reparse Pak Metadata** y **Convert JSONS to
    YAML**.

Programas: **Baldur's Gate 3: DX11**, **Baldur's Gate 3: Vulkan** y, si está
instalado, **Larian Launcher**.

**LSLib y .NET.** Para leer los `.pak` hace falta LSLib, que se descarga de
GitHub si aceptas, y .NET 8 Runtime (x64). **Check For LsLib Updates** vuelve a
descargar LSLib.

**Problemas habituales**

Antes de iniciar el juego, MO2 avisa cuando:

- hay mods que necesitan mods que no están activos, o una versión más nueva de
  un mod;
- hay mods que cargan antes que los mods que necesitan;
- hay mods que necesitan el Script Extender;
- un mod está instalado dos veces, o hay dos mods incompatibles activos;
- el juego desactivó mods en su último inicio.

Wiki de la comunidad: <https://wiki.bg3.community>

## The Witcher 3

**Qué hace MO2 Revamped**

- Los mods van a la carpeta `Mods` del juego. Al instalar, MO2 corrige los
  errores de forma habituales: añade el prefijo `mod` que falte, quita una
  carpeta `Mods\` de más alrededor del mod y manda las carpetas `dlc` y `bin` a
  la carpeta root del mod.
- **Carpeta root:** con la opción del plugin `root_folder_to_game_folder`
  (activada de forma predeterminada), la carpeta `root` de cada mod se muestra
  en la carpeta del juego. Con Root Builder activado, se encarga Root Builder.
- Si no hay versión DX11, encuentra la DX12 (`bin\x64_dx12`).
- Las partidas salen con su imagen en la pestaña **Partidas**.

Programas: **The Witcher 3: Wild Hunt**, **The Witcher 3: Wild Hunt (DX12)** y,
si se encuentra, **Script Merger**.

**Problemas habituales**

- **Varios mods cambian los mismos scripts.** Ejecuta Script Merger para crear
  `mod0000_MergedFiles`.
- **Menús que la versión next-gen no carga.** Faltan los XML de menú de un mod
  en `dx11filelist.txt` o `dx12filelist.txt`.

## Stardew Valley

**Qué hace MO2 Revamped**

- Los mods van a la carpeta `Mods` del juego. Un mod es una carpeta de SMAPI con
  un `manifest.json`, que se busca hasta cuatro niveles de profundidad, así que
  los paquetes con varios mods se instalan tal como vienen.
- Las partidas se leen de `%APPDATA%\StardewValley\Saves`, con los datos de la
  granja.

Programas: **SMAPI** (`StardewModdingAPI.exe`) y **Stardew Valley**. Inicia el
juego con SMAPI para que cargue los mods.

**Problemas habituales**

- **SMAPI no está instalado.** Instala SMAPI con su propio instalador en la
  carpeta del juego, no como mod.
- **Mods que necesitan otros mods que no están**, **versiones más nuevas de
  otros mods** o **un SMAPI más nuevo**.
- **El mismo mod instalado dos veces** (el mismo UniqueID en dos mods).

## Valheim

**Qué hace MO2 Revamped**

- Los mods van a la carpeta del juego. Reconoce los plugins de BepInEx, los mods
  de InSlimVML (`*_vml.dll`), los archivos `.vbuild` de Advanced Builder y los
  `.assets`.
- El cargador de BepInEx (`winhttp.dll`) se carga desde su mod cuando está
  instalado.
- Con la opción del plugin `sync_overwrite` (activada de forma predeterminada),
  los archivos `.cfg` de BepInEx que caen en Overwrite pasan al mod del plugin
  al que pertenecen, al iniciar MO2 y al cerrar el juego.

**Problemas habituales**

- **BepInEx no está instalado.** Instala BepInExPack_Valheim como mod.
- **Plugins que necesitan otros plugins que no están**, como Jotunn.
- **Mods de InSlimVML sin su cargador.**

## Juegos de Bethesda

Skyrim (Special Edition, el original y VR), Fallout 4 (y VR), Fallout 3, New
Vegas, Tale of Two Wastelands, Oblivion, Morrowind, Starfield, Enderal y Nehrim
usan los plugins de Bethesda de MO2, con los cambios de MO2 Revamped.

**Qué hace MO2 Revamped**

- **Orden de los plugins:** la pestaña **Plugins** fija el orden de los archivos
  `.esp`, `.esm` y `.esl`. **Ordenar** usa LOOT. En New Vegas y TTW está
  desactivado salvo que actives `enable_loot_sorting`, porque la mayoría de sus
  guías lo desaconsejan.
- **Extensores de scripts** (SKSE, F4SE, xNVSE/NVSE, FOSE, OBSE, SFSE y las
  versiones VR): al instalar uno, MO2 ofrece **Instalarlo en la carpeta del
  juego**, copiar los archivos tú o **Instalarlo como un mod normal**, y después
  inicia el juego con él. Si su versión no coincide con la del juego, MO2 avisa
  antes de iniciar.
- **Carpetas Root sin Root Builder:** los cargadores de extensores de scripts y
  los archivos de ENB, ReShade y DXVK de la carpeta `Root` de un mod se cargan
  desde el mod cuando eso es fiable; si no, se copian para la partida, se hace
  copia de los archivos del juego que sustituyen, y se quitan otra vez al cerrar
  el juego o en el siguiente inicio de MO2. Los archivos que crea un programa
  ahí van a `Overwrite/Root`.
- **Skyrim SE y Fallout 4:** MO2 comprueba que el extensor de scripts coincide
  con la versión del juego y que Address Library está instalado (opción
  `script_extender_checks`, activada de forma predeterminada).
- **Archivos sueltos y BSA:** **Invalidación automática de archivos**, en cada
  perfil, hace que el juego cargue los archivos sueltos por encima de los de sus
  archivos BSA.
- **DLC y Creation Club** aparecen en la lista de mods como "Creation Club:
  nombre"; ordena sus plugins en la pestaña Plugins.
- **Ventana sin bordes**, en la flecha de **Ejecutar**, para Skyrim, Skyrim SE,
  Enderal, Enderal SE, Fallout 3, New Vegas, TTW, Fallout 4, Oblivion y
  Nehrim.
- Partidas en hora local, co-saves de Fallout 3, y detección de GOG y de
  cualquier biblioteca de Steam.

**Problemas habituales**

- **Plugins del extensor de scripts que no cargan.** Una notificación dice
  cuáles y por qué, como una actualización de Address Library o una DLL que
  falta.
- **Plugins hechos para el Skyrim original** en Skyrim SE (form 43): se avisa.
- **Starfield** avisa de plugins ESP activos y de líneas `sTestFile` en
  `StarfieldCustom.ini`.
- Añade el extensor de scripts, LOOT, xEdit y otras herramientas como programas
  en **Ejecutables**, para que también vean los mods.

Probados con mods hasta ahora: Skyrim SE, Fallout 4, Fallout 3, New Vegas y
Nolvus. Oblivion, el Skyrim original, Morrowind y Starfield aún no se han jugado
con mods, y TTW, Enderal y las versiones VR no se han probado.

## Elden Ring y Nightreign

Elden Ring y Elden Ring Nightreign tienen herramientas propias de MO2 Revamped.
Los mods van a la carpeta `Game` del juego. El programa es **ELDEN RING (MO2)**,
o **ELDEN RING NIGHTREIGN (MO2)** en Nightreign, y también **ELDEN RING
Reforged (online)** cuando ese mod está activado. La guía
completa está en el programa: **…** > **Guía de Elden Ring** (o **Guía de
Nightreign**).

**Instalador de archivos.** Al instalar, elige **Organizar para MO2
(recomendado)** o **Instalar tal como viene en el archivo**. **Manual...** abre
**Colocar los archivos a mano**. Reconoce ELDEN RING Reforged, Convergence,
Seamless Co-op y FRZN Merge, y avisa cuando un Seamless Co-op es del otro juego.

**DLL nativas.** Herramientas > **Perfil de DLL nativas** lista las DLL de los
mods activados y su orden de carga, para cada perfil. Se cargan con el puente
nativo de MO2 Revamped, también con Seamless Co-op y ELDEN RING Reforged.

**Partidas.** Herramientas > **Aislamiento de partidas** elige dónde van las
partidas:

- **Solo este perfil (recomendado)**;
- **Compartidas entre los perfiles de esta instancia**;
- **Las de Steam (sin aislar)**.

Los perfiles de partidas con nombre (**Nuevo perfil de partidas…**,
**Renombrar…**, **Eliminar…**) separan las partidas. Antes de cada inicio se hace
copia de las partidas si cambiaron (se guardan cinco de forma predeterminada,
opción `save_backups_kept`); **Restaurar la copia seleccionada** recupera una.

Mientras el juego se ejecuta desde un perfil aislado, las carpetas de partidas
de Steam se apartan y vuelven sin cambios al cerrarlo, con cualquier lanzador.

Algunos mods guardan con su propio nombre, para que sus personajes no se mezclen
con los del juego sin mods: ELDEN RING Reforged `ER0000.err`, Seamless Co-op
`ER0000.co2`, The Convergence `ER0000.cnv`, y The Convergence con Seamless Co-op
`ER0000.cnvco2`. Con The Convergence, Seamless Co-op arranca desde una copia de
su carpeta en la instancia, con tus ajustes; el mod no se toca. Si jugaste a
The Convergence desde MO2 antes de la beta 16, ese personaje está en
`ER0000.sl2` (o en `ER0000.co2` con Seamless): cópialo como `ER0000.cnv` (o
`ER0000.cnvco2`) en la misma carpeta para seguir con él.

**Opciones de inicio.** Herramientas > **Opciones de inicio**: **Omitir las
pantallas de logos iniciales**, **Inicio del juego más rápido (BootBoost)**,
**Iniciar Elden Ring minimizado**, **Usar un fondo negro al iniciar**, la
prioridad del proceso, la limpieza de Overwrite y los registros del puente.

**Juego online.** MO2 no cambia Easy Anti-Cheat ni el modo online. Usa los
archivos con mods y sus partidas solo para jugar sin conexión.

**Problemas habituales**

- **El juego se cierra nada más iniciarse, o funciona sin el puente de MO2.**
  Comprueba que **Forzar carga de bibliotecas** está activado para su programa y
  lee **Abrir último registro del puente** en Opciones de inicio.
- **MO2 no reconoce esta versión de Elden Ring.** Una actualización del juego
  puede llegar antes de que MO2 Revamped la admita; espera a una actualización de
  MO2 Revamped.
- **Varios mods activos traen su propio regulation.bin.** Solo uno puede ganar;
  deja el que los demás esperan.
- **Seamless Co-op necesita una contraseña de sesión.**
- **Las partidas de Steam siguen ocultas** o hace falta recuperar las partidas
  tras un cierre inesperado: MO2 las devuelve en su siguiente inicio; sigue el
  aviso.
- Algunos mods DLL necesitan una API de extensión que MO2 Revamped no ofrece. me3
  y Mod Engine 2 no se usan como lanzadores; sus paquetes `mod/` se siguen
  instalando.
- Nightreign no tiene lanzador online de Reforged.
