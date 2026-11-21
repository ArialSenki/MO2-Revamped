# Guía de usuario de MO2 Revamped

MO2 Revamped es una edición de la comunidad de Mod Organizer 2, basada en MO2
2.5.2. Mantiene la forma de trabajar de MO2 y añade una interfaz nueva, un
instalador guiado, colecciones de Nexus, listas de Wabbajack y soporte propio
para varios juegos.

Esta guía explica lo que MO2 Revamped hace de otra manera. Para todo lo que
conserva de Mod Organizer 2 sirve la
[documentación de Mod Organizer 2](https://github.com/ModOrganizer2/modorganizer/wiki)
(en inglés).

[English version](../README.md)

## Páginas

- [Instalar y actualizar](instalar-y-actualizar.md): el instalador, las
  actualizaciones, restaurar, copiar datos, desinstalar y el aviso de versión
  nueva.
- [Instancias](instancias.md): instancias globales, portables y aisladas, e
  instancias enlazadas de Nolvus.
- [Juegos](juegos.md): lo que MO2 Revamped hace en Cyberpunk 2077, Baldur's
  Gate 3, The Witcher 3, Stardew Valley, Valheim, los juegos de Bethesda y
  Elden Ring y Nightreign, y sus problemas habituales.
- [Colecciones de Nexus y listas de Wabbajack](colecciones-y-wabbajack.md).
- [Ajustes](ajustes.md): todas las páginas, también Apariencia, los niveles de
  animación, Qt Quick, Negro OLED y los plugins.
- [Linux, Wine y Proton](linux-wine-proton.md).
- [Solución de problemas e informes de errores](problemas.md).

## Primeros pasos

1. **Crea una instancia.** Una instancia es la configuración de un juego: sus
   mods, descargas y perfiles. **Gestionar instancias…**, en la barra de
   comandos, crea una: elige **Global (recomendada)**, el juego, su edición y
   un nombre.
2. **Conecta tu cuenta de Nexus.** Ajustes > Nexus > **Conectarse a Nexus**
   enlaza tu cuenta en el navegador; **Introducir la clave API manualmente**
   también sirve. Después, los botones **Mod manager download** de Nexus envían
   los archivos a MO2.
3. **Instala mods.** Haz doble clic en un archivo de **Descargas** para
   instalarlo, o usa **Instalar** (Ctrl+M) para uno de tu PC. Marca un mod en la
   lista para activarlo; el mod más abajo en la lista gana un conflicto.
4. **O una colección o lista entera.** Herramientas > **Colecciones de Nexus**
   instala una colección de Nexus, y Herramientas > **Listas de Wabbajack** una
   lista de Wabbajack en una instancia propia.
5. **Inicia el juego.** Elige el juego en la barra superior y pulsa
   **Ejecutar**. Inícialo siempre desde MO2: desde Steam o su lanzador no ve los
   mods.
6. **Si algo va mal.** La campana de la barra de comandos lista los problemas
   que MO2 ha encontrado. **…** > **Crear informe de errores** prepara un
   informe que puedes compartir sin tus datos privados.

Los mismos pasos están en el programa: **…** > **Guía de MO2 Revamped**. Una
instancia nueva sin mods los ofrece una vez. Cuando el juego actual tiene su
página en la guía, **…** > **Ayuda para <juego>** la abre, y el botón **Ayuda**
de Ajustes abre la página de la guía sobre Apariencia, Plugins o Desarrollador,
y el resumen de Ajustes en las demás páginas.

## La ventana principal

- **Barra superior:** el juego, el perfil y el programa que se inicia.
  **Ejecutar** lo inicia con el perfil activo; la última línea dice cuándo
  guardó el juego por última vez.
- **Resumen:** **Mods activos**, **Con conflictos**, **Overwrite** y
  **Actualizaciones**. Sus botones filtran la lista de mods o abren Overwrite.
- **Lista de mods:** los filtros **Todo**, **Activos** y **Conflictos** la
  filtran; arrastra un mod para cambiar su prioridad.
- **Pestañas de la derecha:** **Plugins**, **Archivos**, **Datos**,
  **Partidas** y **Descargas**. Los juegos sin plugins o sin archivos no
  muestran esas pestañas.
- **Barra de comandos:** Instancias, **Instalar**, **Nexus**, **Perfiles**,
  **Actualizar**, **Ejecutables**, **Herramientas**, **Ajustes** y la campana
  de notificaciones. **…** tiene los enlaces, las actualizaciones y la ayuda.
- **Teclado:** Ctrl+M instalar un mod, Ctrl+P perfiles, Ctrl+E ejecutables,
  Ctrl+I herramientas, Ctrl+S ajustes, Ctrl+N Nexus, Ctrl+H ayuda, F5
  actualizar, Ctrl+F buscar en la lista, Esc borrar la búsqueda.
- **Ayuda sobre la interfaz:** **…** > **Ayuda sobre la interfaz** y haz clic
  en cualquier parte de la ventana para leer qué hace.
