---
description: Requisitos, compilación, tests y traducciones.
---

# Compilar y probar

## Requisitos

* CMake ≥ 3.16
* Qt 6 (Core, Gui, Widgets, Network, Test) — probado con Qt 6.4 y 6.7
* Compilador C++20: GCC 12+, Clang 15+ o MSVC 2022
* Opcional: Qt LinguistTools (`lrelease`) para incrustar la traducción al inglés; sin él la app se compila sólo
  en español
* Opcional (Linux): QtDBus para capturas y atajo global en Wayland; `libx11-dev` y `libxcb1-dev` para el atajo
  global en X11
* Opcional: zlib (`QAFLOW_HAS_ZLIB`) para leer adjuntos `.docx`/`.odt` comprimidos

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/qaflow
```

## Tests

Un ejecutable por clase, etiquetado por capa. Los de interfaz usan la plataforma `offscreen`.

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -L core             # modelos y funciones puras
ctest --test-dir build -L application      # stores y servicios sobre repositorios en memoria
ctest --test-dir build -L infrastructure   # JSON en disco, QSettings, clientes REST contra un servidor HTTP falso
ctest --test-dir build -L presentation     # ventana principal completa (menú, atajos, toasts)
```

`test_gesreq_client` tiene una prueba contra un GESREQ real que sólo se ejecuta con `QAFLOW_GESREQ_URL`,
`QAFLOW_GESREQ_USER` y `QAFLOW_GESREQ_PASSWORD` definidas.

## Traducciones

El código fuente está en español; `resources/i18n/qaflow_en.ts` es la traducción al inglés. Para recoger las
cadenas nuevas:

```bash
cmake --build build --target qaflow_lupdate
```

Después, traduce las entradas `unfinished` del `.ts` (con Qt Linguist o a mano).

## Capturas de la documentación

`DevSnapshot` (en `presentation/`) renderiza cada pantalla a PNG con datos de ejemplo; son las imágenes de
`docs/screenshots/`.
