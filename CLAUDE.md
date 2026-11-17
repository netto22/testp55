# Proyecto — Laboratorios de sistemas (gestión de memoria y concurrencia)

Experimentos reproducibles en C que construyen, paso a paso, conceptos de
gestión de memoria y concurrencia (al estilo del núcleo de un motor como
JavaScriptCore), con código propio y verificación estricta.

## Alcance
- Estudio de ingeniería sobre componentes de código abierto y herramientas
  educativas con código propio. Nada depende de binarios cerrados ni de
  dispositivos concretos; todo compila y se verifica en Linux con `gcc`/`clang`.
- NO se trabaja el diff de firmware de una consola concreta entre versiones
  (p.ej. localizar cambios de seguridad entre releases): eso es reconocimiento
  de exploits y queda fuera. El estudio de WebKit/JSC se hace sobre el upstream
  open source.

## Estructura
```
labs.html                  # documento interactivo que explica TODOS los labs
labNN-<tema>/              # un directorio por laboratorio
  <fuente>.c  Makefile  README.md
```
Cada lab tiene su `README.md` con esta estructura fija: objetivo técnico ·
información disponible · herramientas · procedimiento · resultados verificables ·
conclusiones y limitaciones.

## Estándar de verificación (obligatorio por lab)
Un lab solo se considera correcto cuando pasa DOS niveles:
1. **Funcional**: `assert`, comparación contra una referencia, o invariante de
   conservación comprobada en el propio programa.
2. **Memoria/concurrencia**: AddressSanitizer+UBSan (`make asan`) para labs de
   memoria; ThreadSanitizer (`make tsan`) para labs con hilos.
Los Makefile deben compilar con `-Wall -Wextra -Wpedantic` sin warnings.

## labs.html — mantener sincronizado (REGLA)
`labs.html` es el documento interactivo de referencia del proyecto. **Debe
actualizarse cada vez que se cree un laboratorio nuevo, o cuando cambien
resultados ya documentados.** Al añadir o modificar un lab:
- Añadir/actualizar su pestaña y su sección (objetivo, cómo funciona, extractos
  de código REALES del lab, tablas de resultados).
- Incluir una visualización interactiva cuando el concepto lo permita (como las
  existentes: simulador mark-and-sweep, buffer acotado animado, carrera atómica).
- Poner al día las **tablas de resultados con los números realmente medidos**
  (no inventados); si un resultado es incómodo (p.ej. no escala), documentarlo
  tal cual.
- Mantenerlo **autocontenido y sin dependencias externas** (funciona offline;
  CSS/JS inline; nada de CDN).
- Antes de commitear, **verificar con Chromium headless** que renderiza sin
  errores de consola y que las interacciones del lab nuevo funcionan.
Nota: esto lo hace Claude al generar el contenido; no es un hook de shell.

## Git
- Rama de desarrollo: `ccr-4310c914-tcu6he`. Commit + push al terminar cambios.
- Binarios compilados van en `.gitignore` (se regeneran con `make`): el repo
  contiene solo fuente, `Makefile`, documentación y `labs.html`.
- No crear pull requests salvo que se pida explícitamente.

## Progresión de labs (hasta ahora)
- `lab01-gc` — mark-and-sweep secuencial (recolecta ciclos; ASan limpio).
- `lab02-concurrency` — productor/consumidor con buffer acotado (TSan limpio).
- `lab03-parallel-gc` — marcado paralelo (correcto vs. referencia; TSan limpio;
  NO escala por contención del mutex único → motiva work-stealing).
- Siguiente natural: `lab04` work-stealing, o lectura del upstream de JSC.
