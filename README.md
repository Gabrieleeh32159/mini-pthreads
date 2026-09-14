# mini-pthreads

Simulación en C del artículo **Frank Mueller, "A Library Implementation of
POSIX Threads under UNIX", Winter USENIX 1993** (Proyecto 1, Sistemas
Operativos CS3015, UTEC 2026-2).

## Qué se simula

Una librería de hilos a nivel de usuario que reproduce, en pequeño, la
arquitectura del paper:

| Idea del artículo | Dónde está |
|---|---|
| Kernel de la librería como monitor monolítico: *kernel flag* y *dispatcher flag* | `src/thread.c` (`mpt_enter_kernel`, `mpt_leave_kernel`, `mpt_dispatch`) |
| Manejador universal de señales; una señal recibida dentro del kernel se registra y se atiende al salir | `src/signal.c` |
| Cambio de contexto (en el paper: register windows de SPARC; aquí `ucontext`) | `src/thread.c` |
| Bloque de control por hilo, colas de listos por prioridad, planificación apropiativa por prioridad y por temporizador (`setitimer`/`SIGALRM`) | `src/queue.c`, `src/thread.c`, `src/signal.c` |
| Mutex con entrega al de mayor prioridad; herencia de prioridad (búsqueda lineal) y techo de prioridad vía SRP (pila) | `src/mutex.c` |
| Variables de condición | `src/cond.c` |
| *Perverted scheduling* (mutex switch, random switch) para depurar | `src/perverted.c` |

## Demostraciones

- `bin/demo1_monitor`: el mismo programa con el monitor monolítico activado y
  desactivado; sin él, la señal del temporizador que cae en medio de una
  operación interna corrompe la cola de listos.
- `bin/demo2_inversion`: escenario de la Figura 5 (P1 baja, P2 media, P3 alta)
  sin protocolo, con herencia y con techo de prioridad; imprime una línea de
  tiempo.
- `bin/demo3_perverted`: una carrera *check-then-act* que siempre pasa bajo
  FIFO y falla al forzar conmutaciones en cada lock o al azar con una semilla
  fija (reproducible).

## Compilar y ejecutar

```sh
make          # compila la librería y las tres demos en bin/
make run      # ejecuta las tres demos
make test     # autoverificación mínima de la librería
```

Probado en macOS (Apple Silicon, clang) y debería compilar en Linux con gcc.
Requiere `ucontext.h` (`-D_XOPEN_SOURCE=700`).
