# Ajustes

**Ajustes** (Ctrl+S) tiene estas páginas. El botón **Ayuda** (o F1) abre la guía
del programa: su página sobre Apariencia, Plugins o Desarrollador, y el resumen de
Ajustes en las demás páginas.

- [General](#general)
- [Apariencia](#apariencia)
- [Lista de mods](#lista-de-mods)
- [Rutas](#rutas)
- [Nexus](#nexus)
- [Plugins](#plugins)
- [Soluciones](#soluciones)
- [Diagnóstico](#diagnóstico)
- [Desarrollador](#modo-de-desarrollador) (solo en modo de desarrollador)

Las opciones que no sirven para el juego actual no se muestran.

## General

- **Idioma.**
- **Avisos: solo sonido, sin la tarjeta** reproduce el sonido de notificación de
  Windows en lugar de mostrar las tarjetas pequeñas de la parte de abajo. Los
  avisos siguen en el registro de MO2, y las preguntas y los errores se siguen
  mostrando.
- **Lista de descargas:** mostrar la información de Nexus, lista compacta y
  ocultar las descargas ya instaladas.
- **Actualizaciones:** **Avisarme cuando salga una nueva versión de MO2
  Revamped** y **Versiones de las que avisar**. Ver
  [el aviso de versión nueva](instalar-y-actualizar.md#el-aviso-de-versión-nueva).
- **Valores predeterminados del perfil:** INI locales, **Guardados locales** e
  **Invalidación automática de archivos** para los perfiles nuevos.
- **Ventanas y diálogos:** centrar siempre los diálogos, **Preguntar antes de
  cambiar de instancia**, abrir las vistas previas con doble clic y
  **Restablecer opciones de diálogo**.

## Apariencia

### Tema

**Tema de la interfaz** ofrece:

- **Claro** y **Oscuro**;
- **Negro OLED**: Oscuro con fondo negro puro, para pantallas OLED;
- tus propios temas, con su base, como "Ember (sobre Oscuro)".

**Nuevo tema...** pide un nombre, una base (Claro u Oscuro) y seis colores, y la
ventana los muestra según los eliges. **Guardar** los conserva y cambia al tema
nuevo; **Cancelar** vuelve atrás. **Editar tema...** cambia uno de los tuyos.

Los temas son archivos `.qss` de la carpeta `stylesheets` (**Abrir carpeta de
temas**), y MO2 recarga uno cuando se guarda su archivo. No edites `Light.qss`
ni `dark.qss`: las actualizaciones los sustituyen. Los temas antiguos de MO2 se
cargan sobre Oscuro o Claro; [THEMES.md](../../THEMES.md) (en inglés) explica
cómo escribir uno.

### Pantalla

- **Tamaño de la interfaz:** 80 %, 90 %, 100 % (normal), 110 % o 125 %. Escala
  todas las ventanas.
- **Suavizar el texto en gris (pantallas OLED)** dibuja las letras sin bordes de
  color, que en las pantallas OLED se ven con flecos.
- **Usar la nueva interfaz (Qt Quick) donde esté disponible** dibuja con Qt
  Quick la ventana de Instancias, el resumen del panel principal y la pregunta
  del tema de una instancia nueva. Funcionan igual y usan algo más de memoria. Si
  alguna no carga, MO2 usa la versión clásica.

Las tres se aplican al volver a iniciar MO2.

### Animaciones

| Nivel | Qué hace |
| --- | --- |
| **Desactivada** | Cada cambio se muestra al instante. |
| **Ahorro (30 Hz)** | Pestañas, interruptores, menús, el panel de registro y el botón Ejecutar se animan a 30 imágenes por segundo. |
| **Estándar (60 Hz)** | Lo mismo a 60 imágenes por segundo. Lo predeterminado. |
| **Monitor (N Hz)** | Sigue la frecuencia de tu pantalla, hasta 240 Hz, con algo más de CPU mientras algo se mueve. |

El nivel de animación se aplica al pulsar **Aceptar**.

### Barra de herramientas y paneles

**Tamaño de los iconos de la barra de herramientas** (Pequeños, Medianos,
Grandes), **Estilo de los botones de la barra de herramientas** (Solo iconos,
Solo texto, Iconos y texto), **Mostrar barra de estado** y **Mostrar panel de
registro**.

### Colores de resaltado

**Colores de resaltado de mods y plugins** fija los colores de los conflictos;
**Restablecer colores predeterminados** los recupera.

## Lista de mods

Cómo muestra la lista los mods, los separadores y los conflictos, y cómo se
comportan la instalación y el arrastre: colores de los separadores (en la barra
de desplazamiento y el de los separadores nuevos), **Mostrar mods instalados
fuera de MO2**, **Recordar filtros tras reiniciar**, **Buscar actualizaciones
tras instalar**, **Contraer elementos automáticamente al arrastrar**, los
separadores plegables y sus iconos de estado, **Guardar por perfil el estado
plegado de los separadores** y **Gestionar categorías**.

## Rutas

Dónde guarda esta instancia sus descargas, mods, cachés, perfiles y Overwrite,
y la carpeta del juego. Usa `%BASE_DIR%` para referirte a la carpeta base. Todas
las carpetas deben admitir escritura.

## Nexus

- **Conectarse a Nexus** enlaza tu cuenta en el navegador; **Introducir la clave
  API manualmente** también sirve; desconectarse la quita.
- La cuenta enlazada y su **Uso de solicitudes de API** (diario y por hora).
- **Registrar MO2 para los enlaces de descarga de Nexus** hace que los botones
  **Mod manager download** de Nexus envíen los archivos a este MO2.
- Las acciones de recomendar y seguir mods, la correspondencia de categorías de
  Nexus, el contador de solicitudes y **Vaciar la caché de Nexus**.
- **Prioridad de servidores de descarga:** los servidores que prefieres, primero
  el de arriba.

## Plugins

Ajustes > **Plugins** lista los plugins de MO2 por tipo: juegos, instaladores,
herramientas, vistas previas y comprobaciones. Elige uno para ver su autor,
versión y descripción.

- **Activar plugin:** desmárcalo para desactivar un plugin. MO2 pregunta antes
  cuando otros lo necesitan; algunos plugins no se pueden desactivar.
- **Opciones específicas del plugin** lista sus ajustes; haz doble clic en un
  valor para cambiarlo. Las secciones de [Juegos](juegos.md) dicen cuáles son
  útiles.
- **Lista negra de plugins:** los archivos de plugin de la lista negra se saltan
  al iniciar MO2. Selecciona una entrada y pulsa Supr para quitarla de la lista.
- Si un plugin se cerró de golpe al cargar, el siguiente inicio ofrece saltarlo,
  ponerlo en la lista negra o cargarlo.

**Plugins de otros juegos.** Un plugin hecho para un juego, como las
herramientas de FNIS y OMOD o las de Elden Ring, no se carga en las instancias
de otros juegos, así los menús son cortos y MO2 arranca más rápido. Es
automático; no hay nada que ajustar. El modo de desarrollador los carga todos.

Cuando el plugin de un juego indica una wiki de modding, **…** > **Wiki de
ayuda del juego** la abre.

## Soluciones

Ajustes para casos con problemas:

- **Mantener activos los archivos principales del juego**, **Comprobar
  conflictos dentro de los archivos comprimidos (experimental)** y **Mantener
  MO2 bloqueado mientras se ejecuta un programa**.
- Ajustes de inicio de Steam (usuario y App ID) para juegos que necesitan Steam
  para arrancar.
- **Usar modo sin conexión**, **Usar el proxy HTTP del sistema** y **Usar
  navegador personalizado**.
- **Restablecer la geometría de las ventanas** y, en Skyrim, **Retrasar la fecha
  de los archivos BSA**.
- **Exclusiones del sistema de archivos virtual:** programas, extensiones de
  archivo y carpetas que el sistema de archivos virtual deja en paz.

## Diagnóstico

- **Detalle del registro de MO2:** Debug, Info (recomendado), Warning o Error.
- **Detalle de los volcados de error:** ninguno, Mini (recomendado), Data o
  Full, y **Máximo de volcados a conservar**.
- **Detalle del registro de LOOT**, en los juegos que ordena LOOT.
- Enlaces a las carpetas de registros y de volcados de error.

En [Solución de problemas](problemas.md) está qué enviar cuando algo va mal.

## Modo de desarrollador

El modo de desarrollador muestra funciones que aún están en pruebas. Ninguna
hace falta para jugar.

- **Activarlo:** en **…** > **Acerca de MO2 Revamped** > **Componentes**, pulsa
  siete veces la fila **Base de MO2**, como con el número de compilación de un
  móvil.
- Aparece una página **Desarrollador** al final de Ajustes con las funciones en
  pruebas.
- **Desactivarlo:** desmarca **Modo de desarrollador** en esa página. La página
  desaparece la próxima vez que abras Ajustes.

El modo de desarrollador también carga todos los plugins, incluidos los de otros
juegos.
