# Instancias

Una instancia es la configuración de un juego: sus mods, descargas, perfiles,
Overwrite y ajustes. **Gestionar instancias…**, en la barra de comandos, las
lista, cambia de una a otra y crea instancias nuevas.

Al crear una instancia eliges dónde vive:

| Tipo | Dónde | Cuándo usarla |
| --- | --- | --- |
| **Global (recomendada)** | `%LOCALAPPDATA%\MO2 Revamped\Instances\<nombre>` | Casi siempre. Se guarda en tu carpeta de usuario y la gestiona este MO2. |
| **Portable** | `<carpeta de MO2>\PortableInstances\<nombre>` | Todo junto al programa, por ejemplo en otra unidad. |
| **Copia aislada** | `<carpeta de MO2>\IsolatedInstances\<nombre>` | Un juego totalmente aparte. Por ahora, Elden Ring y Nightreign. |

Después eliges el juego, su edición (como Steam o GOG) y un nombre.

Cada instancia guarda sus ajustes en su `ModOrganizer.ini`. Los ajustes del
propio MO2, como el tema y el idioma, están en `MO2-Revamped-Global.ini`, en
`%LOCALAPPDATA%\MO2 Revamped`, o junto al programa cuando MO2 es portable.

## Instancias globales

Las instancias globales están en `%LOCALAPPDATA%\MO2 Revamped\Instances`. MO2
Revamped nunca lee la carpeta de instancias globales del MO2 original
(`%LOCALAPPDATA%\ModOrganizer`), así que ninguno cambia las instancias del
otro. Para traer una configuración del MO2 original, usa **Copiar datos** en el
instalador (ver [Instalar y actualizar](instalar-y-actualizar.md#copiar-datos)).

Cuando el propio MO2 Revamped está instalado como copia portable, sus
instancias globales van a `<carpeta de MO2>\GlobalInstances`.

## Instancias portables

Una instancia portable guarda todo en su carpeta, junto al programa. Puedes
mover la carpeta de MO2 entera con ella.

## Instancias aisladas

Una instancia aislada es para un solo juego y guarda sus perfiles, mods,
descargas y ajustes dentro de su propia carpeta. La instalación aislada de
Elden Ring y Nightreign del instalador solo muestra su propia instancia, sin las
globales.

## Instancias enlazadas de Nolvus

[Nolvus](https://www.nolvus.net) instala una lista grande de Skyrim SE con su
propio MO2. MO2 Revamped puede abrir una instalación de Nolvus sin cambiarla.

MO2 Revamped busca Nolvus en `C:\Nolvus` y en las carpetas que añadas. Cuando
encuentra una, ofrece abrirla en MO2 Revamped; **Crear la instancia** crea una
instancia enlazada.

Una instancia **enlazada**:

- es una instancia portable que usa directamente los mods, el Overwrite y la
  carpeta del juego de Nolvus, con copias de los perfiles de Nolvus;
- tiene sus propios ajustes, descargas y caché web;
- nunca escribe en la carpeta de Nolvus, salvo cuando cambias un mod;
- muestra los archivos de Nolvus en la pestaña Descargas como solo lectura;
- no recibe los enlaces `nxm://` ni actualiza la lista: para eso sigue usando
  el Nolvus Dashboard.

Después de una actualización de Nolvus, MO2 Revamped puede volver a copiar los
perfiles; la copia anterior queda en `profiles-backup-<hora>`.

Para convertirla en una instancia normal, **transfiérela**:

- **Mover** (solo en la misma unidad): las carpetas de la lista pasan a la
  instancia en segundos, pero el Nolvus Dashboard ya no puede actualizar esa
  instalación.
- **Copiar** (lo predeterminado): se copian las carpetas; Nolvus sigue
  funcionando y sus actualizaciones solo llegan a su copia. Necesita el espacio
  libre que se indica.
- **Dejar donde está**: la instancia usa y hace suyas las carpetas donde están.
  Sus perfiles son los de Nolvus, así que el MO2 y el Dashboard de Nolvus ven lo
  que cambies.

Cada paso de una transferencia se escribe antes en `nolvus-transfer.jsonl`, en
la carpeta de la instancia, así que una transferencia fallida o interrumpida se
puede deshacer, y un traslado terminado también, más adelante.

## Perfiles

Cada instancia tiene perfiles, y cada perfil guarda su propia lista de mods y
orden de plugins. **Perfiles** (Ctrl+P) crea, duplica, renombra y borra
perfiles; **Usar este perfil** cambia a uno, igual que la barra superior.

- **Partidas propias del perfil** y **Archivos INI del juego propios del
  perfil** separan las partidas y la configuración del juego de cada perfil.
  **Transferir partidas...** copia partidas entre perfiles.
- Ajustes > General > **Valores predeterminados del perfil** elige las opciones
  con las que empieza un perfil nuevo.
- Un perfil no se puede renombrar ni borrar mientras es el actual o mientras
  el juego está en marcha.
