# Progress Tracker (mod de Geode para GD 2.2081)

Guarda solo, en cada intento, el porcentaje donde empezaste y donde moriste (por ejemplo 20-30)
de los niveles que actives desde el menu de pausa.

## Como usarlo en el juego
1. Entra a un nivel y pausa. Arriba a la izquierda aparece el boton **Progreso**.
2. Dentro hay una lista de niveles; marca el check del que quieras seguir.
3. Juega normal. Cada muerte o victoria se guarda sola en `progress.json`.
   El boton **Abrir carpeta** abre donde esta ese archivo.

## Como compilarlo sin instalar nada (GitHub)
1. Crea una cuenta en github.com y un repositorio nuevo (privado o publico).
2. Sube TODO el contenido de esta carpeta (incluida la carpeta oculta `.github`).
   Si la web no te deja subir `.github`, usa "Add file > Create new file", escribe como nombre
   `.github/workflows/build.yml` y pega el contenido de ese archivo.
3. Ve a la pestana **Actions**, abre "Build Geode Mod" y pulsa **Run workflow**.
4. Cuando termine (unos minutos), entra a la ejecucion y descarga **Build Output**.
   Dentro esta el archivo `.geode`.
5. Copia ese `.geode` a la carpeta `geode/mods` de tu Geometry Dash y abre el juego.

Requiere Geode 5.11.2 o mas nuevo. El id del mod esta en `mod.json` (`usuario.progress-tracker`);
puedes cambiar "usuario" y "developer" por tu nombre.
