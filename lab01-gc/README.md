# Lab 01 — Gestión de memoria: recolector mark-and-sweep

Práctica reproducible del programa de investigación sobre arquitectura de
motores. Reproduce, con código propio y mínimo, el mecanismo central de
gestión de memoria de motores como JavaScriptCore.

## Objetivo técnico

Comprender cómo un motor gestiona memoria automática: heap de objetos,
conjunto de raíces, alcanzabilidad por grafo, marcado y barrido. Mostrar de
forma verificable **por qué mark-and-sweep recolecta ciclos** que un simple
conteo de referencias (reference counting) no puede liberar.

## Información disponible

- Concepto general de mark-and-sweep (dominio público, cualquier texto de
  compiladores/runtimes).
- Diseño real de referencia para contrastar: el GC **Riptide** de
  JavaScriptCore (concurrente, generacional), descrito en los posts de
  diseño de webkit.org y en `Source/JavaScriptCore/heap/` del repo de WebKit.
  Nuestro lab es una versión didáctica *stop-the-world*, no concurrente.

## Herramientas necesarias

- Compilador C11 (`gcc` o `clang`), `make`.
- `AddressSanitizer` (incluido en gcc/clang) para verificación dinámica.
- Opcional: `valgrind`.

## Procedimiento de análisis

```sh
make run        # compila y ejecuta los 3 escenarios con asserts
make asan       # recompila con ASan+UBSan y vuelve a ejecutar
make valgrind   # alternativa con valgrind (si está instalado)
```

El diseño (ver `gc.c`):
- **Modelo de objeto**: entero o par cons (`head`, `tail`) → permite construir
  listas, árboles y ciclos arbitrarios.
- **Raíces**: pila explícita que simula el stack de una VM.
- **Marcado**: recorrido recursivo del grafo; el bit `marked` corta ciclos.
- **Barrido**: recorre la lista global de objetos y libera los no marcados.
- **Disparo**: por umbral de población viva, con umbral adaptativo
  (`num_objects * 2`) tras cada ciclo — como escalan los GC reales.

## Resultados verificables

Cada escenario termina con `assert`, así que el programa **falla ruidosamente**
si una invariante se rompe:

1. **Inalcanzables se recolectan** — objetos sin referencia se liberan en el
   siguiente ciclo (`total_freed == 2`).
2. **Raíces sobreviven** — un objeto en raíces persiste a través de 101
   asignaciones y ~95 ciclos de GC; al final `num_objects == 1`.
3. **Ciclos huérfanos se recolectan** — dos pares que se apuntan mutuamente,
   sin raíz que los alcance, se liberan por completo (`num_objects == 0`).
   Esto es exactamente lo que el conteo de referencias **no** logra.

Verificación de corrección de memoria: la ejecución bajo AddressSanitizer +
UndefinedBehaviorSanitizer termina **sin errores ni fugas**.

## Conclusiones y limitaciones

- Mark-and-sweep resuelve el problema de los ciclos por construcción, al
  razonar sobre alcanzabilidad y no sobre cuentas locales.
- El coste del barrido es O(objetos vivos + muertos); de ahí el umbral
  adaptativo para amortizarlo.

Limitaciones frente a un GC de producción (JSC/V8):
- Es *stop-the-world*; Riptide es **concurrente** (marca mientras la VM corre).
- No es **generacional** (JSC separa objetos jóvenes/viejos para escanear menos).
- Marcado **recursivo** → riesgo de desbordar la pila con grafos profundos;
  los GC reales usan una pila de marcado explícita (mark stack).
- No hay **compactación** ni manejo de fragmentación.

## Siguientes actividades propuestas

1. **Mark stack explícita**: sustituir el marcado recursivo por una pila
   iterativa y medir el límite de profundidad que ya no desborda.
2. **GC generacional**: añadir una generación "joven" con *write barrier* y
   comparar número de objetos escaneados por ciclo.
3. **Estudio upstream**: clonar `github.com/WebKit/WebKit` y leer
   `Source/JavaScriptCore/heap/` (p.ej. `MarkingConstraint`, `SlotVisitor`)
   para contrastar nuestra versión con Riptide.
4. **Concurrencia** (siguiente lab): un productor/consumidor en C con
   `pthreads` y variables de condición, para estudiar sincronización — el otro
   eje que pediste.
