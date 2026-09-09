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

