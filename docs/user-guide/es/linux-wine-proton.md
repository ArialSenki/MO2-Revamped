# Linux, Wine y Proton

MO2 Revamped es un programa de Windows. En Linux y en Steam Deck funciona con
Wine o Proton, en el mismo prefijo que el juego o en uno propio.

## Qué hace MO2 Revamped con Wine

- Reconoce Wine al arrancar y añade la versión de Wine a sus informes.
- El instalador pone las DLL del runtime de Visual C++ junto a
  `ModOrganizer.exe`. Se actualizan, se restauran y se desinstalan con el resto
  de los archivos. También valen las DLL de Microsoft instaladas en el prefijo
  con winetricks o protontricks.
- MO2 no se cierra al arrancar cuando Wine no informa de ninguna pantalla.
- La transparencia de las ventanas (Mica) y las esquinas redondeadas están
  desactivadas.
- El navegador integrado funciona sin su sandbox, que Wine no admite.
- Los diagnósticos de inicio y de memoria están activados de forma
  predeterminada, así que un inicio fallido deja un registro (ver abajo).
- Los enlaces `nxm://` de Nexus se aceptan aunque Wine no pueda decir qué
  programa los envió.
- Los accesos directos que Wine no puede crear solo dan un aviso, y **Abrir MO2
  al terminar** inicia MO2 directamente.

## Instalar

Ejecuta el instalador en el prefijo, por ejemplo con `protontricks-launch` o
`wine MO2-Revamped-Setup-<versión>.exe`, e instala en una carpeta de la unidad
`C:` del prefijo, como `C:\MO2 Revamped`. Después crea una instancia como en
Windows.

## Cuando no arranca

1. Busca `logs\startup_diagnostics.log` junto a `ModOrganizer.exe` (o
   `%TEMP%\ModOrganizer-startup_diagnostics.log` cuando esa carpeta no admite
   escritura). Las últimas líneas muestran
   hasta dónde llegó.
2. Si no hay ningún registro de inicio, una DLL no se cargó antes de que MO2
   pudiera escribir nada, normalmente el runtime de Visual C++. Ejecuta el juego
   o MO2 con `PROTON_LOG=1` y busca líneas `err:module` en el registro de Proton.
3. Si MO2 se cerró de golpe, guarda un archivo `ModOrganizer-*.dmp` en la
   carpeta `crashDumps` de la instancia. Adjúntalo a la incidencia con un
   informe de errores (ver [Solución de problemas](problemas.md)).

`MO2_REVAMPED_DIAGNOSTICS=0` desactiva los diagnósticos.

## Límites conocidos

- Los arreglos para Wine y Proton solo se han comprobado en Windows por ahora.
  Informa de los problemas con tu distribución, tu versión de Wine o Proton y
  los registros de arriba.
