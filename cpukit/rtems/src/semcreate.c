/**
 * @file
 *
 * @brief rtems_semaphore_create
 * @ingroup ClassicSem Semaphores
 */

/*
 *  COPYRIGHT (c) 1989-2014.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <rtems/rtems/semimpl.h>
#include <rtems/rtems/attrimpl.h>
#include <rtems/rtems/statusimpl.h>
#include <rtems/rtems/tasksimpl.h>
#include <rtems/score/sysstate.h>

#define SEMAPHORE_KIND_MASK ( RTEMS_SEMAPHORE_CLASS | RTEMS_INHERIT_PRIORITY \
  | RTEMS_PRIORITY_CEILING | RTEMS_MULTIPROCESSOR_RESOURCE_SHARING )

rtems_status_code rtems_semaphore_create(
  rtems_name           name,
  uint32_t             count,
  rtems_attribute      attribute_set,
  rtems_task_priority  priority_ceiling,
  rtems_id            *id
)
{
  Semaphore_Control *the_semaphore;
  Thread_Control    *executing;
  Status_Control     status;
  rtems_attribute    maybe_global;
  rtems_attribute    mutex_with_protocol;
  Semaphore_Variant  variant;

  if ( !rtems_is_name_valid( name ) )
    return RTEMS_INVALID_NAME;

  if ( !id )
    return RTEMS_INVALID_ADDRESS;

#if defined(RTEMS_MULTIPROCESSING)
  if (
    _Attributes_Is_global( attribute_set )
      && !_System_state_Is_multiprocessing
  ) {
    return RTEMS_MP_NOT_CONFIGURED;
  }
#endif

  /* Attribute subset defining a potentially global semaphore variant */
  maybe_global = attribute_set & SEMAPHORE_KIND_MASK;

  /* Attribute subset defining a mutex variant with a locking protocol */
  mutex_with_protocol =
    attribute_set & ( SEMAPHORE_KIND_MASK | RTEMS_GLOBAL | RTEMS_PRIORITY );

  if ( maybe_global == RTEMS_COUNTING_SEMAPHORE ) {
    variant = SEMAPHORE_VARIANT_COUNTING;
  } else if ( count > 1 ) {
    /*
     * The remaining variants are all binary semphores, thus reject an invalid
     * count value.
     */
    return RTEMS_INVALID_NUMBER;
  } else if ( maybe_global == RTEMS_SIMPLE_BINARY_SEMAPHORE ) {
    variant = SEMAPHORE_VARIANT_SIMPLE_BINARY;
  } else if ( maybe_global == RTEMS_BINARY_SEMAPHORE ) {
    variant = SEMAPHORE_VARIANT_MUTEX_NO_PROTOCOL;
  } else if (
    mutex_with_protocol
      == ( RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY | RTEMS_INHERIT_PRIORITY )
  ) {
    variant = SEMAPHORE_VARIANT_MUTEX_INHERIT_PRIORITY;
  } else if (
    mutex_with_protocol
      == ( RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY | RTEMS_PRIORITY_CEILING )
  ) {
    variant = SEMAPHORE_VARIANT_MUTEX_PRIORITY_CEILING;
  } else if (
    mutex_with_protocol
      == ( RTEMS_BINARY_SEMAPHORE | RTEMS_MULTIPROCESSOR_RESOURCE_SHARING )
  ) {
#if defined(RTEMS_SMP)
    variant = SEMAPHORE_VARIANT_MRSP;
#else
    /*
     * On uni-processor configurations the Multiprocessor Resource Sharing
     * Protocol is equivalent to the Priority Ceiling Protocol.
     */
    variant = SEMAPHORE_VARIANT_MUTEX_PRIORITY_CEILING;
#endif
  } else {
    return RTEMS_NOT_DEFINED;
  }

  the_semaphore = _Semaphore_Allocate();

  if ( !the_semaphore ) {
    _Objects_Allocator_unlock();
    return RTEMS_TOO_MANY;
  }

#if defined(RTEMS_MULTIPROCESSING)
  the_semaphore->is_global = _Attributes_Is_global( attribute_set );

  if ( _Attributes_Is_global( attribute_set ) &&
       ! ( _Objects_MP_Allocate_and_open( &_Semaphore_Information, name,
                            the_semaphore->Object.id, false ) ) ) {
    _Semaphore_Free( the_semaphore );
    _Objects_Allocator_unlock();
    return RTEMS_TOO_MANY;
  }
#endif

  priority_ceiling = _RTEMS_tasks_Priority_to_Core( priority_ceiling );
  executing = _Thread_Get_executing();

  the_semaphore->variant = variant;

  if ( _Attributes_Is_priority( attribute_set ) ) {
    the_semaphore->discipline = SEMAPHORE_DISCIPLINE_PRIORITY;
  } else {
    the_semaphore->discipline = SEMAPHORE_DISCIPLINE_FIFO;
  }

  switch ( the_semaphore->variant ) {
    case SEMAPHORE_VARIANT_MUTEX_NO_PROTOCOL:
    case SEMAPHORE_VARIANT_MUTEX_INHERIT_PRIORITY:
      _CORE_recursive_mutex_Initialize(
        &the_semaphore->Core_control.Mutex.Recursive
      );

      if ( count == 0 ) {
        _CORE_mutex_Set_owner(
          &the_semaphore->Core_control.Mutex.Recursive.Mutex,
          executing
        );

        if ( variant == SEMAPHORE_VARIANT_MUTEX_INHERIT_PRIORITY ) {
          ++executing->resource_count;
        }
      }

      status = STATUS_SUCCESSFUL;
      break;
    case SEMAPHORE_VARIANT_MUTEX_PRIORITY_CEILING:
      _CORE_ceiling_mutex_Initialize(
        &the_semaphore->Core_control.Mutex,
        priority_ceiling
      );

      if ( count == 0 ) {
        Thread_queue_Context queue_context;

        _Thread_queue_Context_initialize( &queue_context );
        _ISR_lock_ISR_disable( &queue_context.Lock_context );
        _CORE_mutex_Acquire_critical(
          &the_semaphore->Core_control.Mutex.Recursive.Mutex,
          &queue_context
        );
        status = _CORE_ceiling_mutex_Set_owner(
          &the_semaphore->Core_control.Mutex,
          executing,
          &queue_context
        );

        if ( status != STATUS_SUCCESSFUL ) {
          _Thread_queue_Destroy( &the_semaphore->Core_control.Wait_queue );
        }
      } else {
        status = STATUS_SUCCESSFUL;
      }

      break;
#if defined(RTEMS_SMP)
    case SEMAPHORE_VARIANT_MRSP:
      status = _MRSP_Initialize(
        &the_semaphore->Core_control.MRSP,
        priority_ceiling,
        executing,
        count == 0
      );
      break;
#endif
    default:
      _Assert(
        the_semaphore->variant == SEMAPHORE_VARIANT_SIMPLE_BINARY
          || the_semaphore->variant == SEMAPHORE_VARIANT_COUNTING
      );
      _CORE_semaphore_Initialize(
        &the_semaphore->Core_control.Semaphore,
        count
      );
      status = STATUS_SUCCESSFUL;
      break;
  }

  if ( status != STATUS_SUCCESSFUL ) {
    _Semaphore_Free( the_semaphore );
    _Objects_Allocator_unlock();
    return _Status_Get( status );
  }

  /*
   *  Whether we initialized it as a mutex or counting semaphore, it is
   *  now ready to be "offered" for use as a Classic API Semaphore.
   */
  _Objects_Open(
    &_Semaphore_Information,
    &the_semaphore->Object,
    (Objects_Name) name
  );

  *id = the_semaphore->Object.id;

#if defined(RTEMS_MULTIPROCESSING)
  if ( _Attributes_Is_global( attribute_set ) )
    _Semaphore_MP_Send_process_packet(
      SEMAPHORE_MP_ANNOUNCE_CREATE,
      the_semaphore->Object.id,
      name,
      0                          /* Not used */
    );
#endif
  _Objects_Allocator_unlock();
  return RTEMS_SUCCESSFUL;
}
