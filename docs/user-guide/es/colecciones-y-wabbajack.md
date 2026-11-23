# Colecciones de Nexus y listas de Wabbajack

Las dos instalan una configuración con mods entera de una vez: descargan,
instalan y ordenan todo por ti. Para los archivos de Nexus hace falta una cuenta
de Nexus; con Premium, MO2 los descarga solo.

## Colecciones de Nexus

### Buscar una colección

- Herramientas > **Colecciones de Nexus** > **Explorar colecciones…** (también
  directamente en el menú del botón **Nexus**) busca entre las colecciones de Nexus, con filtro por juego,
  orden (**Más recomendadas**, **Más descargadas**, **Actualizadas
  recientemente**) y **Contenido adulto**. **Abrir la colección** abre la
  elegida.
- **Descargar una colección de Nexus…** acepta el enlace de una colección.
- Un enlace de colección desde el navegador también la abre.

En la galería, Ctrl+F va a la búsqueda y F5 recarga.

### La ventana de la colección

Muestra **Lo que necesita**, las notas del autor y los mods.

- **Instalar en:** **Perfil actual**, **Nuevo perfil** o **Nueva instancia**.
  Una instancia nueva toma el tema y las categorías de esta, así que la
  instalación no se para con las preguntas del primer inicio.
- **Separadores por categoría** pone los mods bajo separadores de categoría.
- **Automático (Premium)** o **Página a página en Nexus** elige cómo se
  descargan los archivos.

Después, tres pasos: descargar, instalar, y ordenar y terminar.

**Comprobaciones antes de instalar.** En el primer **Iniciar**, MO2 busca lo
que puede estorbar: otra versión del juego, un extensor de scripts o un DLC que
falta, mods retirados de Nexus, mods ya activos en el perfil o muchos archivos
que pulsar con una cuenta gratuita. Puedes empezar igualmente y arreglarlo
después.

### Gratis y Premium

- **Premium** descarga solo, hasta tres archivos a la vez.
- **Gratis:** MO2 abre la página de cada archivo en Nexus (**Abrir el siguiente
  en Nexus**) y tú pulsas **Slow download** allí. **Abrir la siguiente página
  automáticamente** te ahorra un clic por archivo.

### Qué se aplica

- Las opciones de FOMOD que eligió el autor. Los mods en los que no se pudieron
  aplicar todas se listan al final, en las opciones del instalador elegidas a
  mano.
- Los parches binarios, comprobando antes el archivo original.
- Las copias exactas de la instalación del autor, las carpetas Root y los
  archivos que sustituyen a otros.
- Los ajustes de INI, después de preguntarte.
- Las reglas de orden de mods y de plugins. Cuando la colección no fija un orden
  de plugins, al final se ofrece **Ordenar los plugins con LOOT**.
- Los archivos de otras webs, o de la página de otro juego en Nexus, quedan para
  que los instales tú; la ventana dice cuáles.

### Pausar, continuar y actualizar

- **Pausar**, **Continuar** y **Reintentar fallidos** funcionan en cualquier
  momento. Para cada archivo, **Marcar como hecho** y **Omitir** ayudan cuando
  uno no se puede descargar.
- El progreso se guarda en la carpeta `collections` de la instancia. Una
  colección sin terminar se ofrece de nuevo en el siguiente inicio:
  **Continuar**, **Ahora no** o **No volver a preguntar**.
- Los avisos esperan mientras se instala una colección.
- Cuando hay una revisión nueva, **Actualizar a la revisión <N>** la actualiza.
  MO2 ofrece mover a la papelera de reciclaje los mods que ya no están en la
  colección.

## Listas de Wabbajack

Las listas de [Wabbajack](https://www.wabbajack.org) son configuraciones
completas con mods. MO2 Revamped las instala él mismo, sin la aplicación de
Wabbajack, cada una en su propia instancia.

### Buscar una lista

- Herramientas > **Listas de Wabbajack** > **Explorar listas de Wabbajack…**
  muestra la galería de Wabbajack por juego. **Abrir la lista** descarga el
  archivo `.wabbajack` y lo abre.
- **Abrir una lista de Wabbajack…** acepta un archivo `.wabbajack` que ya
  tengas.

En la galería, Ctrl+F va a la búsqueda y F5 recarga.

### Descargar los archivos

La ventana de la lista muestra **Lo que necesita** y cada archivo con su estado.

- Elige la **Carpeta de descargas**. Puedes reutilizar una carpeta de descargas
  de Wabbajack: los archivos se reconocen por su hash.
- **Descargar** baja los archivos; las descargas detenidas siguen donde se
  quedaron.
- **Comprobar por hash** verifica los archivos que ya están en la carpeta.
- MO2 descarga de Nexus, de enlaces directos y de los servidores de Wabbajack.
  Sin Premium, cada archivo de Nexus es un clic en su página (**Abrir el
  siguiente en Nexus**).
- Los archivos de Google Drive, MEGA, MediaFire, ModDB, foros o Bethesda.net se
  marcan **A mano**: haz doble clic para abrirlos en el navegador. Las descargas
  del navegador terminadas se recogen de la carpeta de descargas. Clic derecho
  en un archivo > **Elegir el archivo descargado…** si se guardó en otro sitio.
- Los archivos del juego que necesita la lista se toman de tu carpeta del juego.

### Instalar la lista

**Instalar** pone la lista en una **instancia nueva**: dale un **Nombre de la
instancia** y una **Carpeta de instalación**. La carpeta debe estar vacía y no
puede ser la del juego, la de descargas ni la raíz de una unidad. Al terminar,
**Reiniciar en la instancia nueva** cambia a ella.

- El perfil, el orden de carga y los programas de la lista vienen con ella.
- La instancia nueva toma el tema de esta instancia y las categorías de la lista
  (o las de esta instancia, si la lista no trae), así que abre sin las
  preguntas del primer inicio.
- Las fusiones de zEdit de la lista se rehacen con el parche de la lista, como
  hace Wabbajack: no hace falta zEdit.
- Los plugins de MO2 que trae una lista se apartan y se ofrecen; no se instalan
  solos.
- Una instalación que se detuvo se puede continuar más tarde.

### Pasos manuales

Algunas listas dejan archivos para la carpeta del juego. **Pasos manuales de
esta lista…**, en el mismo menú, los copia, hace copia de lo que sustituyen y
ejecuta sus parcheadores cuando se lo pides.

### Problemas habituales

- Una lista necesita su juego instalado y mucho espacio libre; MO2 dice cuánto
  antes de empezar.
- Cuando se llega al límite de solicitudes de Nexus, MO2 espera o continúas más
  tarde: no se pierde nada.
- Un archivo que ya no está en su servidor hay que buscarlo a mano.
- Un paso que MO2 Revamped aún no sabe hacer se indica como «MO2 Revamped aún no
  hace: …». Informa de la lista y del paso en una
  [incidencia](https://github.com/ArialSenki/MO2-Revamped/issues).
