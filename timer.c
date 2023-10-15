#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
  
/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);

//******************************ADDED******************************



//NOTES:
// 1) Comment out the original `timer_sleep` function in devices/timer.c
// 2) Comment out the original `timer_interrupt` function in devices.timer.c
// 3) Insert `sleeping_threads_init()` line of code at the very end of `timer_init` function in devices/timer.c
// 4) Insert this code snippet after the last function prototype in the file devices/timer.c




// A struct that contains the necessary data to wake up a sleeping thread. 
struct sleeping_thread 
{
  int64_t wake_up_time;  
  // A counting semaphore     
  struct semaphore sema;        
  struct list_elem elem;      
};

// A list that contains all threads currently sleepings. Data structure used from lib/kernel/list.c
static struct list sleeping_threads;

//The new implementation of timer_interrupt function; replaces the existing timer_interrupt function within devices/timer.c
static void timer_interrupt (struct intr_frame *args UNUSED);

// Contains the code within original timer_interrupt function and will be called by the new implementation timer_interrupt function.
static void original_timer_interrupt(struct intr_frame *args UNUSED);

//Takes two sleeping threads and returns true if the first one has lower (a < b) sleep timer than the second one and false otherwise.
bool compare_wake_up_time (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

//NOTE:call it within timer_init of devices/timer.c
// Initializes the sleeping thread list according to specs in lib/kernel/list.h
void sleeping_threads_init (void);


void timer_sleep (int64_t ticks)
{
  int64_t start = timer_ticks ();
  struct sleeping_thread st;
  st.wake_up_time = start + ticks;
  // Initialize the value of the semphore to 0.
  sema_init(&st.sema, 0);
  enum intr_level old_level = intr_disable ();
  // Insert the current thread into the list of sleeping threads and sort the list based on thw value of wake up times.
  list_insert_ordered(&sleeping_threads, &st.elem, (list_less_func *) &compare_wake_up_time, NULL);
  intr_set_level(old_level);

  //MYNOTE: might move to where interrupt is disabled above
  //block the current thread
  // Down or "P" operation on a semaphore.
  // Waits for SEMA's value to become positive and then atomically decrements it.
  // This function may sleep, so it must not be called within an interrupt handler.
  //This function may be called with interrupts disabled, but if it sleeps then the next scheduled
  //thread will probably turn interrupts back on.
  sema_down(&st.sema);
}


bool compare_wake_up_time (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct sleeping_thread *st_a = list_entry(a, struct sleeping_thread, elem);
  struct sleeping_thread *st_b = list_entry(b, struct sleeping_thread, elem);
  return st_a->wake_up_time < st_b->wake_up_time;
}


static void timer_interrupt (struct intr_frame *args UNUSED)
{
  // Run the code of the original timer_interrupt function
  original_timer_interrupt(args);

  enum intr_level old_level = intr_disable ();

  while (!list_empty(&sleeping_threads)) 
    {
      // Take thread with least amount of sleep timer
      struct list_elem *e = list_front(&sleeping_threads);
      struct sleeping_thread *st = list_entry(e, struct sleeping_thread, elem);
      
      //
      if (st->wake_up_time > timer_ticks())
        break;
      
      // Remove the thread from sleeping thread list
      list_remove(e);

      // Wake up the thread
      // Up or "V" operation on a semaphore.
      // Increments SEMA's value and wakes up one thread of those waiting for SEMA, if any.
      // This function may be called from an interrupt handler.
      sema_up(&st->sema);
    }

  intr_set_level(old_level);
}


static void original_timer_interrupt(struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick ();
}


void sleeping_threads_init (void)
{
  list_init(&sleeping_threads);
}


//*****************************ADDED******************************

/* Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
  sleeping_threads_init(); //ADDED
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (loops_per_tick | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

//ADDED comment out!!!
/* Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. 
void
timer_sleep (int64_t ticks) 
{
  int64_t start = timer_ticks ();

  ASSERT (intr_get_level () == INTR_ON);
  while (timer_elapsed (start) < ticks) 
    thread_yield ();
}*/

/* Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/* Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/* Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/* Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

//ADDED comment out!!!
/* Timer interrupt handler. 
static void
timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick ();
}*/

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/* Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}
