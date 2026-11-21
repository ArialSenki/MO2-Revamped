# Solución de problemas e informes de errores

## Dónde mirar primero

- **La campana de notificaciones** de la barra de comandos lista los problemas
  que han encontrado los plugins, como un extensor de scripts que falta o un mod
  que necesita otro. Un punto en la campana indica que hay algo que leer;
  **Arreglarlo** soluciona un problema cuando puede.
- **El panel de registro:** **…** > **Panel de registro** muestra el registro de
  MO2 en la parte de abajo de la ventana.
- **La sección del juego** en [Juegos](juegos.md) lista sus problemas
  habituales.

## Problemas habituales

**El juego arranca sin mods.** Inícialo desde MO2 (**Ejecutar**), no desde
Steam, GOG ni su lanzador. Si es un programa que añadiste tú, revísalo en
**Ejecutables**.

**Un mod parece no hacer nada.** Puede que otro mod más abajo en la lista gane
el conflicto: el resumen **Con conflictos** y los colores de conflicto del mod lo
muestran. En los juegos con plugins, comprueba que su plugin está marcado en la
pestaña **Plugins**.

**Faltan archivos que crea el juego o una herramienta.** Van a **Overwrite**;
el resumen **Overwrite** lo abre. Muévelos a un mod para tenerlos aparte.

**Las descargas de Nexus no llegan a MO2.** Conecta tu cuenta en Ajustes > Nexus
y activa **Registrar MO2 para los enlaces de descarga de Nexus**. Solo un MO2
puede recibir los enlaces.

**Nexus dice que se ha llegado al límite de solicitudes.** MO2 espera a que el
límite se reinicie; las colecciones y las listas siguen donde se quedaron.

**MO2 no se abre.** Solo puede haber un MO2 abierto a la vez, contando también
el Mod Organizer 2 original: abrir otro trae al frente la ventana que ya está
abierta. Cierra el otro primero.

**Un plugin cierra MO2 al arrancar.** El siguiente inicio ofrece saltarlo o
ponerlo en la lista negra. Ajustes > Plugins > **Lista negra de plugins** lista
los que están en ella.

**La ventana se ve mal después de cambiar de monitor.** Ajustes > Soluciones >
**Restablecer la geometría de las ventanas**.

## Después de un cierre inesperado

Cuando MO2 se cierra de golpe, guarda un volcado de error. En el siguiente
inicio, un aviso lo dice, con estos botones:

- **Crear informe de errores** prepara el informe que se explica abajo;
- **Abrir la carpeta** abre la carpeta de volcados;
- **Cerrar**.

Los volcados están en la carpeta `crashDumps` de la instancia, como
`ModOrganizer-<versión>-<fecha>.dmp`. Ajustes > Diagnóstico fija cuánto detalle
tienen y cuántos se guardan.

## Crear un informe de errores

**…** > **Crear informe de errores** muestra un informe con los registros y las
versiones, sin tus datos privados. Lo crea el plugin **MO2 Revamped
Information**; si está desactivado, actívalo en Ajustes > Plugins y reinicia
MO2.

La vista previa, **Esto es exactamente lo que compartirá el informe**, lista
cada archivo y muestra su contenido. **Guardar ZIP…** lo guarda y **Copiar
resumen** copia el texto principal.

**Qué contiene**

- `report.txt`: la versión y el canal de MO2 Revamped, Qt, Python y el sistema,
  el juego y su versión, la instancia, el perfil con sus mods, los plugins y los
  volcados de error encontrados (solo sus nombres).
- El final del registro de MO2, de los registros más nuevos del sistema de
  archivos virtual y de los diagnósticos de memoria.
- En Elden Ring y Nightreign, los registros del puente y el estado de las
  herramientas.
- `included-files.txt`, la lista de archivos.

**Lo que nunca contiene:** mods, descargas, partidas ni los propios volcados de
error.

**Qué quita del texto**

| Dato privado | Se cambia por |
| --- | --- |
| Tu nombre de usuario de Windows, también en forma corta y de URL | `%USERNAME%` o `\Users\<user>` |
| El nombre de tu PC y su dominio | `<pc>` |
| Direcciones de correo | `<email>` |
| Claves API, tokens, contraseñas, cookies y claves de Nexus | `<removed>` |
| IDs de cuenta de Steam | `<steamid>` |

## Informar de un problema

Abre una [incidencia en GitHub](https://github.com/ArialSenki/MO2-Revamped/issues)
(también la abre **…** > **Guía de MO2 Revamped** > **Obtener ayuda** >
**Abrir el gestor de incidencias**) e incluye:

- la versión de MO2 Revamped y el juego;
- qué hiciste, qué esperabas y qué pasó;
- el ZIP del informe de errores;
- después de un cierre inesperado, el archivo `.dmp` de `crashDumps`, si quieres
  compartirlo. Un volcado guarda parte de la memoria de MO2, por eso no se añade
  solo al informe.

No adjuntes partidas, datos de tu cuenta ni archivos personales que no tengan
que ver.

## Registros y archivos

| Qué | Dónde |
| --- | --- |
| Registro de MO2 | `<instancia>\logs\mo_interface.log` (se sustituye en cada inicio) |
| Registros del sistema de archivos virtual | `<instancia>\logs\usvfs-*.log` (se guardan los cinco últimos) |
| Volcados de error | `<instancia>\crashDumps` |
| Diagnósticos de inicio y de memoria | `<carpeta de MO2>\logs`, o `%TEMP%` |
| Ajustes de la instancia | `<instancia>\ModOrganizer.ini` |
| Ajustes del propio MO2 | `%LOCALAPPDATA%\MO2 Revamped\MO2-Revamped-Global.ini` |
| Registros del instalador | `%ProgramFiles%\MO2 Revamped Setup\Installer Logs`, o `%TEMP%\MO2 Revamped\Installer Logs` |

En Windows, los diagnósticos detallados de inicio están desactivados de forma
predeterminada; define la variable de entorno `MO2_REVAMPED_DIAGNOSTICS=1`
antes de iniciar MO2 para activarlos. [DIAGNOSTICS.md](../../DIAGNOSTICS.md)
(en inglés) tiene los detalles.
