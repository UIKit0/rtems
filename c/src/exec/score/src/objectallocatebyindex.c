/*
 *  Object Handler
 *
 *
 *  COPYRIGHT (c) 1989-1998.
 *  On-Line Applications Research Corporation (OAR).
 *  Copyright assigned to U.S. Government, 1994.
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id$
 */

#include <rtems/system.h>
#include <rtems/score/address.h>
#include <rtems/score/chain.h>
#include <rtems/score/object.h>
#if defined(RTEMS_MULTIPROCESSING)
#include <rtems/score/objectmp.h>
#endif
#include <rtems/score/thread.h>
#include <rtems/score/wkspace.h>
#include <rtems/score/sysstate.h>
#include <rtems/score/isr.h>

/*PAGE
 *
 *  _Objects_Allocate_by_index
 *
 *  DESCRIPTION:
 *
 *  This function allocates the object control block 
 *  specified by the index from the inactive chain of 
 *  free object control blocks.
 */

Objects_Control *_Objects_Allocate_by_index(
  Objects_Information *information,
  unsigned32           index,
  unsigned32           sizeof_control
)
{
  Objects_Control *the_object;
  void            *p;

  if ( index && information->maximum >= index ) {
    the_object = _Objects_Get_local_object( information, index );
    if ( the_object )
      return NULL;

    /* XXX
     *  This whole section of code needs to be addressed.
     *    +  The 0 should be dealt with more properly so we can autoextend.
     *    +  The pointer arithmetic is probably too expensive.
     *    +  etc.
     */
    
    p = _Addresses_Add_offset( information->object_blocks[ 0 ],
        (information->allocation_size * information->name_length) ),

    p = _Addresses_Add_offset( p, (sizeof_control * (index - 1)) );
    the_object = (Objects_Control *)p;
    _Chain_Extract( &the_object->Node );
 
    return the_object;   
  }    

  /*
   *  Autoextend will have to be thought out as it applies
   *  to user assigned indices.
   */

  return NULL;
}
