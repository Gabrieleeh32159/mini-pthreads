# Apuntes: Mueller, "A Library Implementation of POSIX Threads under UNIX" (USENIX 1993)

## Problema y motivación
- Pthreads (POSIX 1003.4a, aún borrador en 1993) define hilos ligeros con
  prioridades, planificación apropiativa, señales por hilo, mutex y condvars.
- Tres formas de implementarlo: en el kernel, como librería, o mixto.
  Kernel = control simple pero cada llamada paga entrar/salir del SO.
  Librería = más rápida, pero complica señales y hay dos planificadores
  (procesos en el kernel, hilos en la librería).
- Objetivo: demostrar que una implementación **solo de librería** sobre
  SunOS 4.3 BSD / SPARC es factible y rápida. ~400 líneas dependientes de
  máquina (ensamblador).

## Objetivos de diseño (lista de la p. 30-31)
1. Apropiabilidad (round-robin, señales asíncronas, prioridades).
2. Cambios de contexto rápidos.
3. Secciones críticas pequeñas y baratas de entrar/salir.
4. Sin crecimiento ilimitado de la pila (manejadores apilados ad infinitum).
5. Pocas llamadas al SO, sobre todo en señales y cambio de contexto.
6. Interfaz independiente del lenguaje (Ada encima).

## Kernel de la librería (monitor monolítico)
- Estructuras internas se protegen con un **monitor monolítico**: solo un hilo
  a la vez ejecuta código crítico de la librería. Alternativa descartada:
  locking fino (más concurrencia en multiprocesador, pero más operaciones).
- **Kernel flag**: se pone al entrar; mientras está puesto, las modificaciones
  se hacen en exclusión mutua.
- **Dispatcher flag**: indica si al salir del kernel hay que invocar al
  dispatcher. Se pone cuando se planifica un hilo nuevo o cuando llega una
  señal dentro del kernel.
- Salir del kernel: si el dispatcher flag no está, solo se baja el kernel
  flag; si está, se llama al dispatcher (posible cambio de contexto).

## Entrega de señales
- Un **manejador universal** se instala para todas las señales enmascarables.
- Señal fuera del kernel: se entra al kernel, se habilitan señales, se dirige
  la señal al hilo correspondiente y se llama al dispatcher.
- Señal **dentro** del kernel: se registra y se **difiere** hasta que se llame
  al dispatcher; el control vuelve de inmediato al punto interrumpido.
- Al conmutar a un hilo que fue interrumpido, el manejador universal sigue
  pendiente en su pila; por eso el dispatcher **deshabilita señales antes de
  conmutar** a él; al retornar del manejador se rehabilitan. Esto evita el
  crecimiento ilimitado de la pila.

## Dispatcher y cambio de contexto (Figura 2)
- Elige el siguiente hilo listo según la política; si difiere del actual,
  conmuta. En SPARC: trap ST_FLUSH_WINDOWS para volcar las register windows,
  cargar fp con el tope de la pila del hilo, cargar errno, `restore`, saltar.
- Solo cambian registros locales (ins/outs/locals); globales, flotantes y
  status word no se tocan (son scratch o los guarda UNIX en la señal).
- Antes de transferir control se limpian ambos flags y se revisa si llegaron
  señales dentro del kernel; si sí, se atienden y se **reintenta** el dispatch.

## Fake calls (Figura 3)
- Los manejadores de usuario deben correr a la **prioridad del hilo** que
  recibe la señal, no en el momento de la señal.
- Se empuja un frame ("fake call") sobre la pila del hilo receptor: cuando
  ese hilo sea despachado, ejecuta el wrapper → manejador de usuario →
  restaura errno y máscara → vuelve al punto de interrupción.
- Cancelación = señal interna SIGCANCEL; si se acepta, fake call a
  pthread_exit (Tabla 1: disabled / controlled / asynchronous).

## Interfaz UNIX
- ~20 servicios UNIX, casi todos en inicialización. Excepciones: los dos
  traps del cambio de contexto en SPARC, sbrk en creación de hilos (evitable
  con un pool de TCB+pilas), y dos sigsetmask por señal recibida.

## Sincronización
- Mutex: al desbloquear, lo obtiene **el de mayor prioridad** de los que
  esperan. Condvar: el mutex se suelta atómicamente con la suspensión y se
  readquiere al despertar (siempre en estado conocido).
- Test-and-set solo no basta: la herencia exige registrar el dueño
  atómicamente con el lock → **secuencia atómica reiniciable** (si la señal
  la interrumpe, el manejador la reinicia). 7 instrucciones en SPARC
  (Figura 4). Propone compare-and-swap en todo ISA.

## Mediciones (Tabla 2, Sparc IPX, µs)
- Entrar y salir del kernel de Pthreads: **0.4** vs UNIX: **18** (getpid).
  Ese es el argumento central.
- mutex lock/unlock sin contención: 1; con contención: 51.
- Cambio de contexto de hilos: 37 vs proceso UNIX: 123.
- Crear hilo: 12 (Sun 56, Lynx 25).
- Manejador de señal de hilo: interno 52, externo 250 (UNIX 154): las
  señales son el punto débil de la implementación en librería.

## Perverted scheduling (pruebas y depuración)
- Errores "paralelos" no reproducibles con time-slicing: dependen del timer.
- Políticas pervertidas para simular paralelismo en un uniprocesador:
  **mutex switch** (conmutar en cada lock exitoso), **round-robin ordered
  switch** (conmutar al salir del kernel), **random switch** (conmutar al azar
  y elegir al azar el siguiente). Cambiar la semilla cambia el orden.
- Encontraron errores en el runtime de Ada que FIFO no mostraba; ninguno era
  inherente a multiprocesador.

## Inversión de prioridades (Figura 5, Tabla 3)
- Sin protocolo: P1 (baja) tiene el mutex, P3 (alta) se bloquea, P2 (media)
  corre y P3 espera indefinidamente.
- Herencia: P1 hereda la prioridad de P3 al contender → corre hasta soltar.
  Búsqueda lineal de mutex bloqueados al desbloquear. Se adapta a cambios
  dinámicos. Cota: suma de secciones críticas más largas.
- Techo (SRP): al tomar el mutex, el dueño sube al techo; al soltar, vuelve.
  Push/pop en una pila. Menos cambios de contexto, mutex bloqueado menos
  tiempo. Cota más ajustada: una sola sección crítica.
- Observaciones al estándar: los dos protocolos no se mezclan bien (Tabla 4:
  divergencia al anidar); el estándar permite techo solo con herencia
  (demasiado restrictivo); ambigüedad de cabeza/cola de cola al bajar la
  prioridad (prefiere cabeza).

## Conclusión
- Una implementación de librería pura es posible y rinde igual o mejor que
  implementaciones con soporte del kernel. La señalización por hilo es lo que
  más complica el diseño.
