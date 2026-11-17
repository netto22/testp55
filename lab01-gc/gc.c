/*
 * lab01-gc: un recolector de basura mark-and-sweep didactico.
 *
 * Objetivo: reproducir, con codigo propio y minimo, el mecanismo central
 * de gestion de memoria que usan motores como JavaScriptCore: un heap de
 * objetos, un conjunto de raices (roots), una fase de marcado que recorre
 * el grafo de alcanzabilidad y una fase de barrido que libera lo inalcanzable.
 *
 * No depende de nada externo. Compila con: make
 *
 * Diseno deliberadamente simple para que cada paso sea verificable:
 *  - Modelo de objeto: un valor entero O una celda cons (par de punteros),
 *    suficiente para construir grafos arbitrarios (listas, ciclos, arboles).
 *  - Pila de raices explicita (simula el stack de maquina virtual).
 *  - Marcado recursivo con deteccion de ciclos via bit de marca.
 *  - Barrido sobre una lista enlazada de todos los objetos asignados.
 *  - Disparo del GC por umbral de objetos vivos (politica tipo "allocation
 *    trigger"), igual que los GC reales escalan su umbral tras cada ciclo.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { OBJ_INT, OBJ_PAIR } ObjType;

typedef struct Object {
    ObjType type;
    unsigned char marked;      /* bit de marca para mark-and-sweep */
    struct Object *next;       /* enlace en la lista global de objetos */
    union {
        long value;            /* OBJ_INT */
        struct {               /* OBJ_PAIR */
            struct Object *head;
            struct Object *tail;
        };
    };
} Object;

#define MAX_ROOTS 256

typedef struct {
    Object *roots[MAX_ROOTS];  /* raices: lo que la "VM" mantiene vivo */
    int root_count;

    Object *first;             /* cabeza de la lista de todos los objetos */
    int num_objects;           /* objetos vivos actuales */
    int threshold;             /* umbral que dispara el proximo GC */

    /* contadores de instrumentacion, para poder verificar resultados */
    long total_allocated;
    long total_freed;
    int gc_runs;
} VM;

static VM *vm_new(void) {
    VM *vm = calloc(1, sizeof(VM));
    assert(vm != NULL);
    vm->threshold = 8;         /* umbral inicial bajo para forzar ciclos */
    return vm;
}

static void gc(VM *vm);        /* fwd */

static Object *alloc(VM *vm, ObjType type) {
    if (vm->num_objects >= vm->threshold) {
        gc(vm);
    }
    Object *obj = calloc(1, sizeof(Object));
    assert(obj != NULL);
    obj->type = type;
    obj->next = vm->first;
    vm->first = obj;
    vm->num_objects++;
    vm->total_allocated++;
    return obj;
}

static void push_root(VM *vm, Object *obj) {
    assert(vm->root_count < MAX_ROOTS);
    vm->roots[vm->root_count++] = obj;
}

static Object *pop_root(VM *vm) {
    assert(vm->root_count > 0);
    return vm->roots[--vm->root_count];
}

static Object *new_int(VM *vm, long value) {
    Object *obj = alloc(vm, OBJ_INT);
    obj->value = value;
    return obj;
}

static Object *new_pair(VM *vm, Object *head, Object *tail) {
    Object *obj = alloc(vm, OBJ_PAIR);
    obj->head = head;
    obj->tail = tail;
    return obj;
}

/* --- Fase de marcado --- */
static void mark(Object *obj) {
    if (obj == NULL) return;
    if (obj->marked) return;   /* ya visitado: corta ciclos */
    obj->marked = 1;
    if (obj->type == OBJ_PAIR) {
        mark(obj->head);
        mark(obj->tail);
    }
}

static void mark_all(VM *vm) {
    for (int i = 0; i < vm->root_count; i++) {
        mark(vm->roots[i]);
    }
}

/* --- Fase de barrido --- */
static void sweep(VM *vm) {
    Object **obj = &vm->first;
    while (*obj) {
        if (!(*obj)->marked) {
            Object *unreached = *obj;
            *obj = unreached->next;
            free(unreached);
            vm->num_objects--;
            vm->total_freed++;
        } else {
            (*obj)->marked = 0;   /* limpia la marca para el proximo ciclo */
            obj = &(*obj)->next;
        }
    }
}

static void gc(VM *vm) {
    int before = vm->num_objects;
    mark_all(vm);
    sweep(vm);
    vm->gc_runs++;
    /* Politica adaptativa: el umbral crece con la poblacion viva, como en
     * los GC reales, para amortizar el coste del barrido. */
    vm->threshold = (vm->num_objects == 0) ? 8 : vm->num_objects * 2;
    fprintf(stderr, "[gc] run #%d: %d -> %d objetos (umbral=%d)\n",
            vm->gc_runs, before, vm->num_objects, vm->threshold);
}

static void vm_free(VM *vm) {
    vm->root_count = 0;   /* nada es raiz: el barrido libera todo */
    gc(vm);
    free(vm);
}

/* ------------------------------------------------------------------ */
/* Programa de demostracion con asserts: cada escenario es verificable. */
/* ------------------------------------------------------------------ */

int main(void) {
    /* Escenario 1: objetos no alcanzables se recolectan. */
    {
        VM *vm = vm_new();
        new_int(vm, 1);          /* basura inmediata, nadie la referencia */
        new_int(vm, 2);
        assert(vm->num_objects == 2);
        vm->threshold = 0;       /* forzar GC en el proximo alloc */
        new_int(vm, 3);          /* esto dispara gc(): limpia 1 y 2, deja 3 */
        /* 3 tampoco es raiz, pero se asigno despues del barrido */
        assert(vm->total_freed == 2);
        printf("Escenario 1 OK: inalcanzables recolectados (freed=%ld)\n",
               vm->total_freed);
        vm_free(vm);
    }

    /* Escenario 2: objetos alcanzables desde raices sobreviven. */
    {
        VM *vm = vm_new();
        Object *a = new_int(vm, 10);
        push_root(vm, a);
        for (int i = 0; i < 100; i++) new_int(vm, i);  /* mucha basura */
        gc(vm);
        assert(vm->num_objects == 1);  /* solo 'a' sobrevive */
        assert(pop_root(vm) == a);
        printf("Escenario 2 OK: raiz preservada entre %ld asignaciones\n",
               vm->total_allocated);
        vm_free(vm);
    }

    /* Escenario 3: ciclos. Dos pares que se apuntan mutuamente y que
     * NO son alcanzables deben recolectarse (mark-and-sweep maneja ciclos;
     * un simple contador de referencias no podria). */
    {
        VM *vm = vm_new();
        Object *p1 = new_pair(vm, NULL, NULL);
        push_root(vm, p1);
        Object *p2 = new_pair(vm, NULL, NULL);
        push_root(vm, p2);
        p1->tail = p2;           /* p1 -> p2 */
        p2->tail = p1;           /* p2 -> p1  (ciclo) */
        pop_root(vm);            /* quita p2 de raices */
        pop_root(vm);            /* quita p1 de raices: el ciclo queda huerfano */
        long freed_before = vm->total_freed;
        gc(vm);
        assert(vm->num_objects == 0);          /* el ciclo entero se libera */
        assert(vm->total_freed - freed_before == 2);
        printf("Escenario 3 OK: ciclo huerfano recolectado (algo que el "
               "conteo de referencias no logra)\n");
        vm_free(vm);
    }

    printf("\nTodos los escenarios verificados.\n");
    return 0;
}
