/**
 * @file
 *
 * @brief POSIX Threads Private Support
 *
 * This include file contains all the private support information for
 * POSIX threads.
 */

/*
 *  COPYRIGHT (c) 1989-2011.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifndef _RTEMS_POSIX_PTHREADIMPL_H
#define _RTEMS_POSIX_PTHREADIMPL_H

#include <rtems/posix/pthread.h>
#include <rtems/posix/config.h>
#include <rtems/posix/threadsup.h>
#include <rtems/score/assert.h>
#include <rtems/score/objectimpl.h>
#include <rtems/score/timespec.h>
#include <rtems/score/threadimpl.h>
#include <rtems/score/watchdogimpl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup POSIX_PTHREAD
 */
/**@{**/

/**
 * The following sets the minimum stack size for POSIX threads.
 */
#define PTHREAD_MINIMUM_STACK_SIZE (_Stack_Minimum() * 2)

/**
 * The following defines the information control block used to manage
 * this class of objects.
 */
extern Thread_Information _POSIX_Threads_Information;

/**
 * This variable contains the default POSIX Thread attributes.
 */
extern pthread_attr_t _POSIX_Threads_Default_attributes;

RTEMS_INLINE_ROUTINE void _POSIX_Threads_Sporadic_timer_insert(
  Thread_Control    *the_thread,
  POSIX_API_Control *api
)
{
  the_thread->cpu_time_budget =
    _Timespec_To_ticks( &api->Attributes.schedparam.sched_ss_init_budget );

  _Watchdog_Per_CPU_insert_relative(
    &api->Sporadic_timer,
    _Per_CPU_Get(),
    _Timespec_To_ticks( &api->Attributes.schedparam.sched_ss_repl_period )
  );
}

/**
 * @brief POSIX threads sporadic budget callout.
 *
 * This routine handles the sporadic scheduling algorithm.
 *
 * @param[in] the_thread is a pointer to the thread whose budget
 * has been exceeded.
 */
void _POSIX_Threads_Sporadic_budget_callout(
  Thread_Control *the_thread
);

/**
 * This routine supports the sporadic scheduling algorithm.  It
 * is scheduled to be executed at the end of each replenishment
 * period.  In sporadic scheduling a thread will execute at a
 * high priority for a user specified amount of CPU time.  When
 * it exceeds that amount of CPU time, its priority is automatically
 * lowered. This TSR is executed when it is time to replenish
 * the thread's processor budget and raise its priority.
 *
 * @param[in] id is ignored
 * @param[in] argument is a pointer to the Thread_Control structure
 *            for the thread being replenished.
 */
void _POSIX_Threads_Sporadic_budget_TSR( Watchdog_Control *watchdog );

/**
 * @brief Translate sched_param into SuperCore terms.
 *
 * This method translates the POSIX API sched_param into the corresponding
 * SuperCore settings.
 *
 * @param[in] policy is the POSIX scheduling policy
 * @param[in] param points to the scheduling parameter structure
 * @param[in] budget_algorithm points to the output CPU Budget algorithm
 * @param[in] budget_callout points to the output CPU Callout
 *
 * @retval 0 Indicates success.
 * @retval error_code POSIX error code indicating failure.
 */
int _POSIX_Thread_Translate_sched_param(
  int                                  policy,
  struct sched_param                  *param,
  Thread_CPU_budget_algorithms        *budget_algorithm,
  Thread_CPU_budget_algorithm_callout *budget_callout
);

/*
 * rtems_pthread_attribute_compare
 */
int rtems_pthread_attribute_compare(
  const pthread_attr_t *attr1,
  const pthread_attr_t *attr2
);

RTEMS_INLINE_ROUTINE Thread_Control *_POSIX_Threads_Allocate(void)
{
  _Objects_Allocator_lock();

  _Thread_Kill_zombies();

  return (Thread_Control *)
    _Objects_Allocate_unprotected( &_POSIX_Threads_Information.Objects );
}

RTEMS_INLINE_ROUTINE void _POSIX_Threads_Copy_attributes(
  pthread_attr_t        *dst_attr,
  const pthread_attr_t  *src_attr
)
{
  *dst_attr = *src_attr;
#if defined(RTEMS_SMP) && defined(__RTEMS_HAVE_SYS_CPUSET_H__)
  _Assert(
    dst_attr->affinitysetsize == sizeof(dst_attr->affinitysetpreallocated)
  );
  dst_attr->affinityset = &dst_attr->affinitysetpreallocated;
#endif
}

RTEMS_INLINE_ROUTINE void _POSIX_Threads_Free (
  Thread_Control *the_pthread
)
{
  _Objects_Free( &_POSIX_Threads_Information.Objects, &the_pthread->Object );
}

RTEMS_INLINE_ROUTINE void _POSIX_Threads_Initialize_attributes(
  pthread_attr_t  *attr
)
{
  _POSIX_Threads_Copy_attributes(
    attr,
    &_POSIX_Threads_Default_attributes
  );
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif
/*  end of include file */
