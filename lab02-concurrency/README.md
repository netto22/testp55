# Lab 02 — Concurrencia: productor/consumidor con buffer acotado

Práctica reproducible sobre sincronización de hilos. Reproduce el patrón más
común en runtimes concurrentes: una **cola de trabajo** de capacidad fija que
coordina varios hilos — el mismo esqueleto que usa un motor para alimentar
hilos de compilación JIT concurrente o de marcado de GC.

## Objetivo técnico

Comprender la sincronización con `pthreads`: exclusión mutua (mutex),
espera/señalización (variables de condición) y terminación limpia. Demostrar,
de forma verificable, que un buffer acotado compartido por N productores y M
consumidores **no pierde ni duplica** ningún elemento y **no tiene data races**.

## Información disponible

- `pthreads` (POSIX threads): dominio público, documentado en `man pthread_*`.
- Patrón productor/consumidor con *bounded buffer*: clásico de sistemas
  operativos (Dijkstra).
- Referencia de contraste: los *thread pools* de WebKit
  (`Source/WTF/wtf/AutomaticThread.h`, `ParallelJobs`) y el marcado paralelo
  de JSC usan exactamente esta idea con colas de trabajo y señalización.

## Herramientas necesarias

- Compilador C11 (`gcc`/`clang`), `make`, `-lpthread`.
- `ThreadSanitizer` (`-fsanitize=thread`, incluido en gcc/clang) para
  detectar condiciones de carrera.
- Opcional: `valgrind --tool=helgrind`.

## Procedimiento de análisis

```sh
make run        # 4 productores / 4 consumidores / 100k items c/u
make stress     # cargas asimetricas (1x8, 8x1, 8x8)
make tsan       # recompila con ThreadSanitizer y verifica ausencia de races
make helgrind   # alternativa con helgrind (si valgrind esta instalado)
./conc 2 3 10000    # parametros propios: n_prod n_cons items/productor
```

Diseño (ver `conc.c`):
- **Buffer circular** de capacidad `BUFFER_CAP` (16).
- **Un mutex** protege todo el estado compartido (índices, contador, agregados).
- **Dos variables de condición**: `not_full` (despierta productores) y
  `not_empty` (despierta consumidores).
- **Espera con `while`, no `if`**: imprescindible con múltiples hilos y frente
  a despertares espurios — al despertar hay que re-verificar la condición.
- **Terminación limpia**: cada productor decrementa `producers_remaining`; el
  último hace `broadcast` sobre `not_empty` para que ningún consumidor quede
  bloqueado esperando datos que ya no llegarán.

## Resultados verificables

Verificación funcional (invariante de conservación): la suma y el conteo de lo
consumido deben coincidir exactamente con lo producido, y el buffer debe
quedar vacío. Se comprueba por tres vías independientes (suma por consumidores,
agregado global, y valor esperado calculado a priori). Si algo falla, el
programa devuelve código de salida 1.

| Escenario | count esperado = consumido | buffer final |
|---|---|---|
| 4×4, 100k | 400000 ✓ | 0 ✓ |
| 1×8, 200k | 200000 ✓ | 0 ✓ |
| 8×1, 50k  | 400000 ✓ | 0 ✓ |
| 8×8, 50k  | 400000 ✓ | 0 ✓ |

Verificación de correctness concurrente: ejecución bajo **ThreadSanitizer sin
ningún reporte de data race** (código de salida 0).

## Conclusiones y limitaciones

- El mutex serializa el acceso al estado; las condvars evitan la espera activa
  (*busy-waiting*), bloqueando el hilo hasta que la condición pueda cumplirse.
- El patrón `while(condición) wait()` es la regla de oro: nunca `if`.
- La terminación es la parte sutil: hace falta un criterio explícito
  (productores vivos + buffer vacío) y un `broadcast` final.

Limitaciones frente a un *thread pool* de producción:
- **Un solo mutex** → punto de contención global. Las colas de alto rendimiento
  usan colas *lock-free* o *work-stealing* (una cola por hilo).
- Sin **afinidad**, sin prioridades, sin backpressure configurable.
- `BUFFER_CAP` fijo; un pool real dimensiona según núcleos disponibles.

## Siguientes actividades propuestas

1. **Medir contención**: cronometrar throughput variando `BUFFER_CAP` y el
   número de hilos; observar dónde satura por el mutex único.
2. **Work-stealing**: una cola por consumidor y robo de trabajo entre colas;
   comparar escalado frente a la cola única.
3. **Estudio upstream**: leer `Source/WTF/wtf/AutomaticThread.*` y el marcado
   paralelo en `Source/JavaScriptCore/heap/` para ver cómo JSC coordina sus
   hilos de GC.
4. **Cruce con Lab 01**: convertir el marcado del GC (lab01) a marcado paralelo
   usando una mark stack compartida con este mismo patrón de sincronización.
