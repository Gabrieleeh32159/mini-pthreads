# Factibilidad en Selfie

¿Se puede implementar la propuesta de Mueller (Pthreads como librería pura,
sin soporte del kernel) en Selfie? Respuesta corta: **como librería pura, no;
como extensión del emulador (mipster/hypster), sí, en una versión simplificada
que conserva las ideas centrales del paper.**

## Por qué la versión "librería pura" no es viable en C*

La tesis del paper es que todo vive en el espacio de usuario. Eso se apoya en
cosas que Selfie no ofrece al programa de usuario:

| El paper necesita | En Selfie | Consecuencia |
|---|---|---|
| Cambio de contexto en espacio de usuario (ensamblador SPARC; aquí `ucontext`) | C* no tiene ensamblador en línea, `setjmp/longjmp` ni acceso a registros o al puntero de pila | Un programa C* no puede saltar de una pila a otra: no hay forma de escribir el dispatcher en la librería |
| Señales UNIX (`SIGALRM`, manejador universal, fake calls, cancelación) | RISC-U no expone interrupciones al usuario y el kernel de Selfie no entrega señales | Sin señales no hay apropiación por temporizador a nivel de usuario ni fake calls |
| Instrucciones atómicas (`ldstub`, secuencias atómicas reiniciables) | RISC-U tiene `lui, addi, ld, sd, add, sub, mul, divu, remu, sltu, beq, jal, jalr, ecall`: no hay `amoswap`, `lr/sc` ni test-and-set | No se puede implementar el lock de la Figura 4 en el usuario |
| `struct`, punteros a función, arreglos de estructuras para el TCB | C* solo tiene `uint64_t` y `uint64_t*`, sin `struct` ni punteros a función | Los TCB tendrían que representarse como bloques de memoria con offsets fijos o arreglos paralelos |

Lo único que sí podría hacerse en C* puro es una librería **cooperativa sin
cambio de pila** (corrutinas por máquina de estados), pero eso ya no es la
propuesta del paper.

## Qué habría que modificar o extender en Selfie

La versión implementable mueve el "kernel de la librería" al emulador. En
Selfie el contexto ya existe: `mipster` ejecuta contextos con registros,
tabla de páginas y un `TIMESLICE`, e `hypster` crea y conmuta contextos con
`create_context` / `switch_context` sobre `mipster`. Sobre eso:

1. **Contexto → hilo.** Añadir a la estructura de contexto: prioridad,
   estado (listo, bloqueado, terminado), hilo que hace `join` y un enlace a
   la cola de listos. Un hilo es un contexto nuevo que **comparte la tabla
   de páginas** de su creador y recibe su propia región de pila.
2. **Nuevas llamadas al sistema (`ecall`).** `thread_create(fn, arg, prio)`,
   `thread_yield`, `thread_join`, `thread_exit`, `mutex_lock`,
   `mutex_unlock`. Se agregan al despachador de syscalls de `mipster`
   (`implement_*`) igual que `exit`, `read`, `write`, `brk`.
3. **Planificador por prioridad.** Reemplazar el round-robin por contexto de
   `mipster`/`hypster` por colas de listos por prioridad, con fin de quantum
   cuando expira `TIMESLICE`. Es el equivalente del dispatcher de la
   Figura 2 del paper.
4. **Mutex con protocolos.** El emulador conoce quién es el dueño de cada
   mutex, así que la herencia de prioridad (subir la prioridad del dueño
   cuando contiende uno mayor) y el techo (subir al techo al tomar, bajar al
   soltar) se implementan directamente en `mutex_lock`/`mutex_unlock`.
5. **Perverted scheduling.** Un flag en el emulador para forzar un cambio de
   contexto en cada `mutex_lock` exitoso (mutex switch) o al azar con una
   semilla (random switch). Es la parte más fácil y la más útil para probar
   programas C* concurrentes.

## Limitaciones principales

- **Se pierde la tesis central**: cada operación de hilos pasa por un `ecall`,
  que es exactamente el costo que Mueller quería evitar (0.4 µs de kernel de
  librería frente a 18 µs de kernel UNIX). En Selfie no hay alternativa,
  porque el usuario no puede conmutar pilas.
- **Sin señales** no hay fake calls, ni cancelación asíncrona, ni manejadores
  de usuario a la prioridad del hilo. El único evento asíncrono es el fin de
  quantum, y lo maneja el emulador.
- **Sin atomicidad de usuario**: no hace falta test-and-set porque el
  emulador es secuencial y cada `ecall` es atómico respecto al programa;
  eso fija el modelo a un solo procesador, que es también la suposición del
  paper.
- **C\*** obliga a que `thread_create` reciba la dirección de la función como
  un entero: el programa C* no tiene punteros a función tipados.
- **Memoria**: las pilas de los hilos deben reservarse dentro del mismo
  espacio de direcciones, como bloques del heap obtenidos con `brk`, y fijar
  el `sp` del nuevo contexto ahí.

## Versión simplificada que sí sería implementable

Hilos a nivel de kernel dentro de `mipster`:

- contextos que comparten tabla de páginas, con prioridad y estado;
- `ecall` para crear, ceder, esperar y terminar, y para lock/unlock;
- colas de listos por prioridad y apropiación por `TIMESLICE`;
- herencia de prioridad en el lock (el techo es opcional);
- flag de perverted scheduling (mutex switch y random switch con semilla).

Con eso se reproducen las tres demostraciones de este proyecto: la
inversión de prioridades de la Figura 5 y el perverted scheduling son
directos; la demostración del monitor monolítico se traduce en que el
emulador nunca es interrumpido a mitad de una operación sobre sus colas,
es decir, el emulador **es** el monitor monolítico. Lo que no se reproduce
es lo que hace interesante al paper en rendimiento: evitar el kernel.

## Relación con la simulación en C

| Mecanismo | mini-pthreads (C, este proyecto) | Selfie (versión simplificada) |
|---|---|---|
| Cambio de contexto | `swapcontext` en usuario | `switch_context` en el emulador |
| Monitor monolítico | kernel flag + dispatcher flag | implícito: el emulador no se interrumpe |
| Apropiación | `SIGALRM` + diferimiento | `TIMESLICE` del emulador |
| Mutex y protocolos | en la librería | en `implement_mutex_lock/unlock` |
| Perverted scheduling | hooks en lock y en salida del kernel | flag en el planificador del emulador |
