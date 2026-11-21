# Instalar y actualizar

MO2 Revamped viene en un solo instalador, `MO2-Revamped-Setup-<versión>.exe`,
en las [versiones de GitHub](https://github.com/ArialSenki/MO2-Revamped/releases)
o en [Nexus Mods](https://www.nexusmods.com/site/mods/2612). Cada versión de
GitHub trae también un `SHA256SUMS.txt` para comprobar la descarga.

El instalador está en español, inglés, alemán, francés, italiano y portugués de
Brasil. El idioma que elijas es también el que usará MO2 al arrancar.

## Qué puede hacer el instalador

Vuelve a abrir el mismo instalador para cualquiera de estas opciones:

| Opción | Qué hace |
| --- | --- |
| **Instalar MO2 Revamped** | Una instalación nueva. La carpeta predeterminada es `C:\MO2 Revamped`. |
| **Actualizar MO2 Revamped** | Cambia los archivos del programa de una instalación por los de esta versión. |
| **Copiar datos** | Copia los datos de un MO2 que ya tienes en una copia portable nueva. |
| **Restaurar copia anterior** | Recupera la versión que guardó la última actualización. |
| **Desinstalar MO2 Revamped** | Quita una instalación. |

También está **Instalar Elden Ring y Nightreign aislado**, una copia aparte de
MO2 Revamped solo para esos dos juegos, de forma predeterminada en
`C:\MO2 Revamped\Isolated\Elden Ring`. Guarda sus perfiles, mods, descargas y
ajustes en su propia carpeta. Ver [Instancias](instancias.md#instancias-aisladas).

### Dónde instalarlo

Usa una carpeta en la que puedas escribir, como la predeterminada,
`C:\MO2 Revamped`. El instalador avisa si eliges Archivos de programa, donde
Windows protege los archivos y MO2 puede tener problemas de permisos.

### Opciones al terminar

- **Abrir MO2 al terminar** (activada de forma predeterminada).
- Acceso en el menú Inicio (activado) y en el escritorio (desactivado).
- **Abrir los enlaces de Nexus (nxm://) con este MO2**: activada, salvo que
  otro programa (otro MO2, Vortex) ya reciba los enlaces; entonces el
  instalador dice cuál y deja la casilla sin marcar. Esos enlaces son los
  botones **Mod manager download** de Nexus. MO2 también puede registrarse más
  tarde en Ajustes > Nexus.
- **Forzar el cierre de MO2 en esta carpeta si sigue abierto.** Sin ella, el
  instalador dice qué programas siguen usando los archivos y espera.

### Runtime de Visual C++

MO2 necesita el runtime de Visual C++ 2015-2022 de Microsoft. Cuando falta o es
anterior a la 14.44, el instalador descarga el instalador firmado de Microsoft y
lo ejecuta (x64, y x86 para los juegos de 32 bits). Después Windows puede pedir
un reinicio.

## Actualizar

Elige **Actualizar MO2 Revamped** y la carpeta de la instalación.

- Antes se hace una copia de seguridad de los archivos que se cambian.
- Se conservan los perfiles, los mods, las descargas y los plugins adicionales.
- Se conservan los accesos directos; las casillas añaden los que falten.

La copia de seguridad está en `%LOCALAPPDATA%\MO2 Revamped Installer\backups`.
Solo se guarda la más reciente.

Las instalaciones aisladas de Elden Ring, también las de la 1.0.1, se
actualizan igual.

## Restaurar la versión anterior

**Restaurar copia anterior** recupera los archivos del programa que guardó la
última actualización. Se conservan tus perfiles, mods, descargas y los archivos
que hayas cambiado desde entonces. Solo está en la ventana del instalador, no
en modo silencioso.

## Copiar datos

**Copiar datos** copia una instalación que ya tienes en una carpeta portable
nueva, de forma predeterminada `C:\MO2 Revamped\Portable`. Eliges qué copiar:
perfiles y partidas, mods instalados, descargas, Overwrite, preferencias de MO2,
y categorías y mapa de Nexus. La carpeta de origen no cambia.

## Desinstalar

- **Conservar mis mods y datos** (lo predeterminado) quita el programa y deja
  tus datos.
- **Quitar todo, también mis mods, datos y la carpeta** borra la carpeta
  entera, y la copia de la última actualización si la hay.
- **Quitar también mis instancias, mods, descargas y ajustes** borra además
  `%LOCALAPPDATA%\MO2 Revamped`, donde viven las instancias globales. No se
  ofrece mientras haya otra instalación estándar registrada.

Los archivos de la carpeta que no son de MO2 se conservan y se listan al final.
Al desinstalar también se quitan los accesos directos y el registro de los
enlaces de Nexus.

## Instalación silenciosa

El instalador tiene opciones para instalaciones con scripts:

```text
MO2-Revamped-Setup-<versión>.exe /S /dir="C:\MO2 Revamped" [/isolated] [/nodesktop] [/nostartmenu] [/nonxm] [/launch] [/forceclose]
MO2-Revamped-Setup-<versión>.exe /S /uninstall /dir="C:\MO2 Revamped" [/keepdata | /removeall [/removeappdata]] [/forceclose]
```

- `/S` también se puede escribir `/quiet`, `/silent` o `/verysilent`; `/D=`
  equivale a `/dir=`.
- Una ejecución silenciosa sobre una instalación existente la actualiza, y
  nunca instala una versión más antigua sobre una más nueva.
- `/keepdata` es lo predeterminado al desinstalar. En una instalación estándar,
  `/removeall` necesita también `/removeappdata`.
- `unins000.exe /S`, en la carpeta de la instalación, desinstala esa carpeta.
- Códigos de salida: 0 hecho, 1 error, 2 línea de comandos incorrecta (no se ha
  cambiado nada).

Los registros del instalador están en
`%ProgramFiles%\MO2 Revamped Setup\Installer Logs`, o en
`%TEMP%\MO2 Revamped\Installer Logs` cuando se ejecuta sin permisos de
administrador. Se guardan los diez últimos.

## El aviso de versión nueva

MO2 Revamped pregunta a GitHub por versiones nuevas como mucho una vez al día.
Cuando hay una más nueva, una ventana **Ya está disponible MO2 Revamped
<versión>** lista cada versión nueva con sus notas, y estos botones:

- **Abrir en GitHub** y **Abrir en Nexus** abren la página de descarga. MO2 no
  descarga ni instala nada: actualizas con el instalador.
- **Recordármelo más tarde** vuelve a avisar en otro inicio.
- **Omitir esta versión** deja de mostrar la ventana para esa versión; desde
  entonces solo se ve en la barra de estado.

Ajustes > General > **Actualizaciones**:

- **Avisarme cuando salga una nueva versión de MO2 Revamped** activa o
  desactiva la comprobación (activada de forma predeterminada).
- **Versiones de las que avisar**: **Versiones estables**, **Estables y beta** o
  **Estables, beta y de desarrollo**. De forma predeterminada, una versión beta
  avisa de las betas y una estable solo de las estables.

En el modo sin conexión (Ajustes > Soluciones) no se comprueba.

## MO2 Revamped y el MO2 original

MO2 Revamped guarda sus instancias globales en `%LOCALAPPDATA%\MO2 Revamped` y
nunca lee ni cambia las instancias globales del MO2 original. Puedes tener los
dos instalados, pero solo uno abierto a la vez: si abres uno mientras el otro
está abierto, se trae al frente la ventana que ya estaba abierta.
