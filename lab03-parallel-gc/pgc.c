/*
 * lab03-parallel-gc: marcado PARALELO de un recolector mark-and-sweep.
 *
 * Cruce de los labs 01 (GC mark-and-sweep) y 02 (concurrencia con pthreads):
 * la fase de marcado se reparte entre N hilos que drenan una mark stack
 * compartida. Es el nucleo de un GC concurrente real (p.ej. el marcado
 * paralelo de JavaScriptCore / V8).
 *
 * Dos problemas que este lab resuelve de forma verificable:
 *
 *  1) Marcar cada objeto EXACTAMENTE una vez pese a que varios hilos lo
 *     descubran a la vez. Solucion: el bit de marca es atomico; "gana" el
 *     hilo cuyo atomic_exchange lo pasa de 0 a 1, y solo ese hilo empuja el
 *     objeto a la pila. Asi cada objeto se empuja y se escanea una sola vez.
 *
 *  2) Deteccion de terminacion. Un hilo que ve la pila vacia no puede salir:
 *     otro hilo activo podria estar a punto de empujar hijos. Solucion: se
 *     cuenta cuantos hilos estan ociosos; SOLO cuando los N estan ociosos y
 *     la pila esta vacia no queda trabajo en vuelo -> done + broadcast.
 *     Un hilo que tiene un objeto "en la mano" (lo saco pero aun no lo
 *     escaneo) nunca esta contado como ocioso, de modo que su trabajo
 *     pendiente impide declarar el fin prematuramente.
 *
 * Verificacion:
 *  - El conjunto de objetos marcados en paralelo debe ser IDENTICO al de un
 *    marcado secuencial de referencia (DFS), para 1, 2, 4 y 8 hilos.
 *  - Nº de objetos escaneados == nº de alcanzables (cada uno una vez).
 *  - Bajo ThreadSanitizer no debe haber ninguna condicion de carrera.
 *
 * Grafo reproducible (RNG con semilla fija). Los objetos de la region alta
 * no son referenciados por nadie ni son raices => inalcanzables garantizados,
 * para comprobar que el barrido los libera.
 */

#define _POSIX_C_SOURCE 200809L   /* para clock_gettime / CLOCK_MONOTONIC */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXC 6   /* maximo de hijos por objeto */

typedef struct Object {
    int id;
    _Atomic unsigned char marked;
    int nchildren;
    struct Object *children[MAXC];
} Object;

static Object **g_all;   /* heap: todos los objetos */
static int g_n;          /* nº total de objetos */

/* ------------------------- mark stack compartida ------------------------- */
typedef struct {
    Object **data;
    long size;
    long cap;                 /* pre-dimensionada a g_n: nunca hay que crecer */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int idle;                 /* hilos actualmente ociosos */
    int nworkers;
    bool done;
    _Atomic long scanned;     /* objetos sacados y escaneados (instrumentacion) */
} MarkStack;

/* push/pop: el llamante tiene el lock; la capacidad esta garantizada. */
static void ms_push(MarkStack *ms, Object *o) { ms->data[ms->size++] = o; }
static Object *ms_pop(MarkStack *ms) { return ms->data[--ms->size]; }

static void *worker(void *arg) {
    MarkStack *ms = arg;
    pthread_mutex_lock(&ms->lock);
    for (;;) {
        if (ms->size > 0) {
            Object *o = ms_pop(ms);
            /* Tenemos un objeto "en la mano": NO estamos ociosos.
             * Escaneamos sus hijos FUERA del lock para no serializar. */
            pthread_mutex_unlock(&ms->lock);
            atomic_fetch_add(&ms->scanned, 1);

            Object *winners[MAXC];
            int nw = 0;
            for (int i = 0; i < o->nchildren; i++) {
                Object *c = o->children[i];
                /* Gana quien pase la marca de 0 a 1: ese empuja el hijo. */
                unsigned char prev = atomic_exchange(&c->marked, (unsigned char)1);
                if (prev == 0) winners[nw++] = c;
            }

            pthread_mutex_lock(&ms->lock);
            for (int i = 0; i < nw; i++) ms_push(ms, winners[i]);
            if (nw > 0) pthread_cond_broadcast(&ms->cond); /* hay trabajo nuevo */
            continue;
        }

        /* Pila vacia: nos declaramos ociosos. */
        ms->idle++;
        if (ms->idle == ms->nworkers) {
            /* Todos ociosos y pila vacia => no hay trabajo en vuelo. Fin. */
            ms->done = true;
            pthread_cond_broadcast(&ms->cond);
            pthread_mutex_unlock(&ms->lock);
            return NULL;
        }
        while (ms->size == 0 && !ms->done) {
            pthread_cond_wait(&ms->cond, &ms->lock);
        }
        if (ms->done) {               /* si done, la pila esta vacia: salir */
            pthread_mutex_unlock(&ms->lock);
            return NULL;
        }
        ms->idle--;                   /* nos despertaron con trabajo */
        continue;
    }
}

/* Ejecuta un marcado paralelo completo. Devuelve nº de objetos escaneados. */
static long parallel_mark(int nworkers, MarkStack *ms, Object **roots, int nroots) {
    for (int i = 0; i < g_n; i++)
        atomic_store(&g_all[i]->marked, (unsigned char)0);
    ms->size = 0;
    ms->idle = 0;
    ms->done = false;
    ms->nworkers = nworkers;
    atomic_store(&ms->scanned, 0);

    /* Raices: marcar y empujar (un solo hilo, sin carrera). */
    for (int i = 0; i < nroots; i++)
        if (atomic_exchange(&roots[i]->marked, (unsigned char)1) == 0)
            ms_push(ms, roots[i]);

    pthread_t *th = calloc((size_t)nworkers, sizeof(pthread_t));
    for (int i = 0; i < nworkers; i++)
        pthread_create(&th[i], NULL, worker, ms);
    for (int i = 0; i < nworkers; i++)
        pthread_join(th[i], NULL);
    free(th);
    return atomic_load(&ms->scanned);
}

/* ------------------------- referencia secuencial ------------------------- */
/* DFS iterativo sobre el grafo; 'seen[id]' = alcanzable. Devuelve el conteo. */
static long reference_reachable(bool *seen, Object **roots, int nroots) {
    memset(seen, 0, (size_t)g_n);
    int *stk = malloc((size_t)g_n * sizeof(int));
    long sp = 0, cnt = 0;
    for (int i = 0; i < nroots; i++) {
        int id = roots[i]->id;
        if (!seen[id]) { seen[id] = true; stk[sp++] = id; }
    }
    while (sp > 0) {
        int id = stk[--sp];
        cnt++;
        Object *o = g_all[id];
        for (int i = 0; i < o->nchildren; i++) {
            int cid = o->children[i]->id;
            if (!seen[cid]) { seen[cid] = true; stk[sp++] = cid; }
        }
    }
    free(stk);
    return cnt;
}

/* El marcado paralelo debe coincidir objeto a objeto con la referencia. */
static bool verify(const bool *seen) {
    for (int i = 0; i < g_n; i++) {
        bool m = atomic_load(&g_all[i]->marked) != 0;
        if (m != seen[i]) return false;
    }
    return true;
}

/* --------------------------- grafo reproducible -------------------------- */
static uint64_t rng = 0x9E3779B97F4A7C15ULL;
static uint64_t xr(void) {           /* xorshift64, determinista */
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* Objetos [0,live) pueden ser destino de aristas y raices.
 * Objetos [live,n) no son referenciados por nadie ni raices => inalcanzables. */
static void build_graph(int n, int live) {
    g_n = n;
    g_all = calloc((size_t)n, sizeof(Object *));
    for (int i = 0; i < n; i++) {
        Object *o = calloc(1, sizeof(Object));
        o->id = i;
        atomic_store(&o->marked, (unsigned char)0);
        g_all[i] = o;
    }
    for (int i = 0; i < n; i++) {
        int nc = (int)(xr() % (MAXC + 1));
        g_all[i]->nchildren = nc;
        for (int j = 0; j < nc; j++)
            g_all[i]->children[j] = g_all[(int)(xr() % (unsigned)live)];
    }
}

int main(int argc, char **argv) {
    int n    = (argc > 1) ? atoi(argv[1]) : 200000;
    int live = (argc > 2) ? atoi(argv[2]) : 160000;
    if (n < 2 || live < 1 || live >= n) {
        fprintf(stderr, "uso: %s [n>=2] [live<n]\n", argv[0]);
        return 2;
    }
    build_graph(n, live);

    int nroots = 16;
    Object *roots[16];
    for (int i = 0; i < nroots; i++)
        roots[i] = g_all[(int)(xr() % (unsigned)live)];

    bool *seen = malloc((size_t)n);
    long reachable = reference_reachable(seen, roots, nroots);

    printf("grafo: %d objetos (%d alcanzable-elegibles, %d inalcanzables garantizados)\n",
           n, live, n - live);
    printf("alcanzables desde %d raices (referencia secuencial DFS): %ld\n\n",
           nroots, reachable);

    MarkStack ms;
    ms.cap = n;
    ms.data = malloc((size_t)n * sizeof(Object *));
    pthread_mutex_init(&ms.lock, NULL);
    pthread_cond_init(&ms.cond, NULL);

    int thread_counts[] = {1, 2, 4, 8};
    bool all_ok = true;
    for (size_t k = 0; k < sizeof(thread_counts) / sizeof(int); k++) {
        int nw = thread_counts[k];
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        long scanned = parallel_mark(nw, &ms, roots, nroots);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double el = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                    (t1.tv_nsec - t0.tv_nsec) / 1e6;
        bool ok = (scanned == reachable) && verify(seen);
        all_ok = all_ok && ok;
        printf("hilos=%d  escaneados=%ld  %s  tiempo=%6.2f ms\n",
               nw, scanned, ok ? "OK" : "FALLO", el);
    }

    /* Barrido sobre el ultimo marcado (8 hilos): libera lo inalcanzable. */
    long survivors = 0, freed = 0;
    for (int i = 0; i < n; i++) {
        if (atomic_load(&g_all[i]->marked)) {
            survivors++;
        } else {
            free(g_all[i]);
            g_all[i] = NULL;
            freed++;
        }
    }
    bool sweep_ok = (survivors == reachable);
    printf("\nbarrido tras marcado paralelo: supervivientes=%ld, liberados=%ld %s\n",
           survivors, freed, sweep_ok ? "OK" : "FALLO");

    for (int i = 0; i < n; i++)
        if (g_all[i]) free(g_all[i]);
    free(g_all);
    free(seen);
    free(ms.data);
    pthread_mutex_destroy(&ms.lock);
    pthread_cond_destroy(&ms.cond);

    if (all_ok && sweep_ok) {
        printf("\nOK: el marcado paralelo coincide con la referencia "
               "secuencial para 1, 2, 4 y 8 hilos.\n");
        return 0;
    }
    printf("\nFALLO: el resultado paralelo no coincide con la referencia.\n");
    return 1;
}
