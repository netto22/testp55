# Lab 03 — Marcado paralelo del GC (cruce Lab01 × Lab02)

Reparte la fase de **marcado** de un recolector mark-and-sweep (Lab 01) entre
N hilos coordinados con el patrón mutex + variable de condición (Lab 02). Es
el núcleo de un GC concurrente real (marcado paralelo de JavaScriptCore / V8).

## Objetivo técnico

Resolver, de forma verificable, los dos problemas que aparecen al paralelizar
el marcado:

1. **Marcar cada objeto exactamente una vez** aunque varios hilos lo
   descubran a la vez.
2. **Detectar la terminación** cuando el trabajo está repartido y nadie sabe,
   localmente, si el resto ha acabado.

## Información disponible

- Labs 01 y 02 de este proyecto (modelo de objeto + patrón de sincronización).
- Referencia de contraste: marcado paralelo real en
  `Source/JavaScriptCore/heap/` de WebKit (`SlotVisitor`, mark stacks por hilo,
  *draining* y *work-stealing*).

## Herramientas necesarias

- C11, `pthreads`, `-lpthread`.
- C11 atomics (`<stdatomic.h>`): el bit de marca es `_Atomic`.
- `ThreadSanitizer` (`-fsanitize=thread`) para detectar data races.

## Procedimiento de análisis

```sh
make run       # grafo de 200k objetos; marca con 1,2,4,8 hilos y verifica
make tsan      # recompila con ThreadSanitizer (grafo menor) y verifica races
make helgrind  # alternativa con helgrind (muy lento)
./pgc 500000 400000   # n objetos, region alcanzable-elegible
```

### Soluciones implementadas (ver `pgc.c`)

**(1) Marcado exactamente-una-vez.** El bit `marked` es `_Atomic`. Al visitar
un hijo se hace `atomic_exchange(&c->marked, 1)`: el hilo cuyo intercambio
devuelve el valor previo `0` es el que "gana", y solo ese empuja el hijo a la
pila. Invariante: un objeto se marca antes de empujarse ⇒ se empuja y se
escanea una sola vez. El escaneo de hijos se hace **fuera del lock** (solo el
`push` lo toma) para no serializar el trabajo.

**(2) Detección de terminación.** Se cuenta `idle` (hilos ociosos). Un hilo que
sacó un objeto de la pila lo tiene "en la mano" y **no** está contado como
ocioso, así que su trabajo pendiente impide declarar el fin. Solo cuando
`idle == nworkers` con la pila vacía no hay trabajo en vuelo: se marca `done` y
se hace `broadcast`. Se usa `while (...) cond_wait` (nunca `if`).

## Resultados verificables

Corrección (lo esencial): el conjunto marcado en paralelo es **idéntico** al
del DFS secuencial de referencia para 1, 2, 4 y 8 hilos; `escaneados ==
alcanzables`; el barrido libera exactamente los inalcanzables. Bajo
**ThreadSanitizer: 0 data races**.

Ejemplo (200k objetos, 150 436 alcanzables; máquina de 8 vCPU del contenedor):

| hilos | escaneados | tiempo |
|------:|-----------:|-------:|
| 1 | 150436 ✓ | 13.4 ms |
| 2 | 150436 ✓ | 58.5 ms |
| 4 | 150436 ✓ | 66.2 ms |
| 8 | 150436 ✓ | 64.4 ms |

## Conclusiones y limitaciones

- **La corrección es sólida**: atomicidad del bit de marca + protocolo de
  terminación bastan para un marcado paralelo correcto y sin carreras.
- **Pero no escala — al contrario, empeora con más hilos.** Esto es un
  resultado medido, no un fallo de corrección, y es el hallazgo central del
  lab. La causa:
  - **Un único mutex global** serializa todos los `push`/`pop`: es el cuello de
    botella (el mismo efecto de contención que midió el Lab 02).
  - **El trabajo por objeto es minúsculo** (escanear ≤ 6 punteros) frente al
    coste de tomar el lock y despertar hilos. La relación trabajo/sincronización
    es desfavorable, así que la coordinación cuesta más de lo que se gana.
  - El `broadcast` en cada `push` genera despertares innecesarios
    (*thundering herd*).

  Es exactamente por esto que los GC de producción **no** usan una pila global
  con un lock: usan **una mark stack por hilo** y **work-stealing** (robo de
  trabajo entre colas), minimizando la sincronización al caso de robo.

## Siguientes actividades propuestas

1. **Work-stealing**: una deque por hilo; cada hilo trabaja en su extremo sin
   lock y solo sincroniza al robar del extremo opuesto de otra. Medir si ahora
   sí escala con el mismo grafo.
2. **Reducir sincronización**: empujar hijos en lotes (menos `broadcast`);
   usar `signal` en vez de `broadcast`; aumentar el trabajo por objeto
   (grafos con más fan-out) para ver dónde el paralelismo empieza a compensar.
3. **Estudio upstream**: leer `SlotVisitor::drainFromShared` y las mark stacks
   de JSC en `Source/JavaScriptCore/heap/` para contrastar con nuestro diseño.
4. **Marcado concurrente (no solo paralelo)**: marcar mientras un "mutator"
   modifica el grafo, introduciendo un *write barrier* — el salto conceptual de
   "paralelo" (varios marcadores) a "concurrente" (marcar a la vez que se usa).
