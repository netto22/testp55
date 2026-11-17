/*
 * lab02-concurrency: productor/consumidor con buffer acotado.
 *
 * Objetivo: reproducir el patron de sincronizacion mas comun en runtimes
 * concurrentes (p.ej. una cola de trabajo que alimenta hilos de compilacion
 * JIT o de marcado de GC): varios productores y consumidores comparten un
 * buffer circular de tamano fijo, coordinados con un mutex y dos variables
 * de condicion.
 *
 * Invariantes que el programa verifica al final:
 *  - Cada item producido se consume EXACTAMENTE una vez (sin perdidas ni
 *    duplicados): se comprueba con la suma total y el conteo.
 *  - Ningun consumidor lee de buffer vacio; ningun productor escribe en
 *    buffer lleno (garantizado por las variables de condicion).
 *
 * Correccion dinamica: compilar con -fsanitize=thread no debe reportar
 * ninguna condicion de carrera (make tsan).
 *
 * Parametros por linea de comandos (opcionales):
 *   ./conc [n_productores] [n_consumidores] [items_por_productor]
 */

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFFER_CAP 16   /* capacidad del buffer acotado */

typedef struct {
    long buf[BUFFER_CAP];
    int head;           /* indice de lectura */
    int tail;           /* indice de escritura */
    int count;          /* elementos actualmente en el buffer */

    pthread_mutex_t lock;
    pthread_cond_t not_full;   /* senal: hay hueco para producir */
    pthread_cond_t not_empty;  /* senal: hay dato para consumir */

    int producers_remaining;   /* productores que aun no han terminado */

    /* resultados agregados, protegidos por 'lock' */
    uint64_t total_consumed_sum;
    long total_consumed_count;
} Queue;

typedef struct {
    Queue *q;
    int id;
    long items;              /* cuantos producir */
    long base;               /* valor base para que las sumas sean distinguibles */
    uint64_t produced_sum;   /* lo que este productor metio (verificacion) */
} ProducerArg;

typedef struct {
    Queue *q;
    int id;
    uint64_t consumed_sum;   /* lo que este consumidor saco */
    long consumed_count;
} ConsumerArg;

static void queue_init(Queue *q, int n_producers) {
    q->head = q->tail = q->count = 0;
    q->producers_remaining = n_producers;
    q->total_consumed_sum = 0;
    q->total_consumed_count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

static void queue_destroy(Queue *q) {
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
}

static void *producer(void *arg) {
    ProducerArg *p = arg;
    Queue *q = p->q;
    for (long i = 0; i < p->items; i++) {
        long value = p->base + i;

        pthread_mutex_lock(&q->lock);
        /* Esperar mientras el buffer este lleno. 'while' (no 'if') protege
         * frente a despertares espurios y a multiples productores. */
        while (q->count == BUFFER_CAP) {
            pthread_cond_wait(&q->not_full, &q->lock);
        }
        q->buf[q->tail] = value;
        q->tail = (q->tail + 1) % BUFFER_CAP;
        q->count++;
        p->produced_sum += (uint64_t)value;
        /* Despertar a un consumidor: ahora hay dato. */
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
    }

    /* Este productor termino: anunciarlo. Si es el ultimo, despertar a TODOS
     * los consumidores para que no queden bloqueados esperando datos que no
     * llegaran. */
    pthread_mutex_lock(&q->lock);
    q->producers_remaining--;
    if (q->producers_remaining == 0) {
        pthread_cond_broadcast(&q->not_empty);
    }
    pthread_mutex_unlock(&q->lock);
    return NULL;
}

static void *consumer(void *arg) {
    ConsumerArg *c = arg;
    Queue *q = c->q;
    for (;;) {
        pthread_mutex_lock(&q->lock);
        /* Esperar mientras no haya datos Y aun queden productores vivos. */
        while (q->count == 0 && q->producers_remaining > 0) {
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
        /* Condicion de salida: buffer vacio y nadie mas va a producir. */
        if (q->count == 0 && q->producers_remaining == 0) {
            pthread_mutex_unlock(&q->lock);
            break;
        }
        long value = q->buf[q->head];
        q->head = (q->head + 1) % BUFFER_CAP;
        q->count--;

        c->consumed_sum += (uint64_t)value;
        c->consumed_count++;
        q->total_consumed_sum += (uint64_t)value;
        q->total_consumed_count++;

        /* Despertar a un productor: ahora hay hueco. */
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->lock);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int n_prod = (argc > 1) ? atoi(argv[1]) : 4;
    int n_cons = (argc > 2) ? atoi(argv[2]) : 4;
    long items = (argc > 3) ? atol(argv[3]) : 100000;

    if (n_prod < 1 || n_cons < 1 || items < 0) {
        fprintf(stderr, "uso: %s [n_prod>=1] [n_cons>=1] [items>=0]\n", argv[0]);
        return 2;
    }

    Queue q;
    queue_init(&q, n_prod);

    pthread_t *pth = calloc(n_prod, sizeof(pthread_t));
    pthread_t *cth = calloc(n_cons, sizeof(pthread_t));
    ProducerArg *pa = calloc(n_prod, sizeof(ProducerArg));
    ConsumerArg *ca = calloc(n_cons, sizeof(ConsumerArg));

    /* Suma esperada: cada productor p mete valores base..base+items-1.
     * Usamos bases muy separadas para que el total sea reproducible. */
    uint64_t expected_sum = 0;
    for (int i = 0; i < n_prod; i++) {
        pa[i] = (ProducerArg){.q = &q, .id = i, .items = items,
                              .base = (long)i * 1000000L, .produced_sum = 0};
        for (long k = 0; k < items; k++)
            expected_sum += (uint64_t)(pa[i].base + k);
    }
    long expected_count = (long)n_prod * items;

    for (int i = 0; i < n_cons; i++)
        ca[i] = (ConsumerArg){.q = &q, .id = i, .consumed_sum = 0, .consumed_count = 0};

    for (int i = 0; i < n_prod; i++)
        pthread_create(&pth[i], NULL, producer, &pa[i]);
    for (int i = 0; i < n_cons; i++)
        pthread_create(&cth[i], NULL, consumer, &ca[i]);

    for (int i = 0; i < n_prod; i++) pthread_join(pth[i], NULL);
    for (int i = 0; i < n_cons; i++) pthread_join(cth[i], NULL);

    /* --- Verificacion de invariantes --- */
    uint64_t sum_by_consumers = 0;
    long count_by_consumers = 0;
    for (int i = 0; i < n_cons; i++) {
        sum_by_consumers += ca[i].consumed_sum;
        count_by_consumers += ca[i].consumed_count;
    }

    printf("productores=%d consumidores=%d items/productor=%ld\n",
           n_prod, n_cons, items);
    printf("esperado:   count=%ld  sum=%" PRIu64 "\n", expected_count, expected_sum);
    printf("consumido:  count=%ld  sum=%" PRIu64 "\n", count_by_consumers, sum_by_consumers);
    printf("agregado q: count=%ld  sum=%" PRIu64 "\n",
           q.total_consumed_count, q.total_consumed_sum);
    printf("buffer final count=%d (debe ser 0)\n", q.count);

    int ok = (count_by_consumers == expected_count) &&
             (sum_by_consumers == expected_sum) &&
             (q.total_consumed_count == expected_count) &&
             (q.total_consumed_sum == expected_sum) &&
             (q.count == 0);

    free(pth); free(cth); free(pa); free(ca);
    queue_destroy(&q);

    if (ok) {
        printf("\nOK: cada item producido se consumio exactamente una vez.\n");
        return 0;
    } else {
        printf("\nFALLO: las invariantes no se cumplen.\n");
        return 1;
    }
}
