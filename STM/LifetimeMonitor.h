/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#pragma once

#include "../../Common/NotCopyable.h"
#include "stm.h"

namespace bss { namespace thread { namespace STM
{
   /**
    * To allow another object to monitor the lifetime of an object embed one a WLifetimeBeacon
    * object in the class. Then the other class can use WLifetimeMonitor to monitor the lifetime of
    * the object that embeds WLifetimeBeacon. 
    */
   class BSS_CLASSAPI WLifetimeBeacon
   {
      NO_COPYING (WLifetimeBeacon);

   public:
      friend class WLifetimeMonitor;
      
      WLifetimeBeacon ();

      //!{
      /**
       * Move constructor/operator. Any monitors attached to this beacon will begin reporting false
       * for IsAlive and the monitors associated with the passed in beacon will become associated
       * with this beacon.
       *
       * @param b The beacon to move from. 
       */
      WLifetimeBeacon (WLifetimeBeacon&& b);
      WLifetimeBeacon& operator=(WLifetimeBeacon&& b);
      //!}
      
      ~WLifetimeBeacon ();

      //!{
      /**
       * "fakes the death" of the object that the WLifetimeBeacon object is embedded in. Any
       * WLifetimeMonitor objects attached to this beacon will start reporting IsAlive to be
       * false. WLifetimeMonitor objects attached to this beacon will report IsAlive to be true
       * until Reset is called again of the beacon is destroyed.
       */
      void Reset ();
      void Reset (WAtomic& at);
      //!}
      
   private:
      typedef std::shared_ptr<WVar<bool>> AlivePtr;
      WVar<AlivePtr> m_alive_v;
   };

   /**
    * Monitors the lifetime of a WLifetimeBeacon object.
    */
   class BSS_CLASSAPI WLifetimeMonitor
   {
   public:
      /**
       * Creates a monitor that isn't attached to a beacon.
       */
      WLifetimeMonitor ();
      
      /**
       * Create a monitor that is attached to the given beacon.
       */
      WLifetimeMonitor (const WLifetimeBeacon& beacon);

      /**
       * Copy constructor. The new monitor will be attached to the same beacon as the given
       * monitor.
       */
      WLifetimeMonitor (const WLifetimeMonitor& m);

      /**
       * Assignment operator. This monitor will be attached to the same beacon as the given
       * monitor.
       */
      WLifetimeMonitor& operator=(const WLifetimeMonitor& m);

      /**
       * Move constructor. The new monitor will be attached to the beacon that the given monitor
       * was, the given monitor will be in an invalid state. To resuse the given monitor it will
       * need to have another monitor assigned to it to get it back into a valid state.
       */
      WLifetimeMonitor (WLifetimeMonitor&& m);

      /**
       * Move operator. The new monitor will be attached to the beacon that the given monitor
       * was, the given monitor will be in an invalid state. To resuse the given monitor it will
       * need to have another monitor assigned to it to get it back into a valid state.
       */
      WLifetimeMonitor& operator=(WLifetimeMonitor&& m);
      
      //!{
      /**
       * Starts monitoring the given beacon. If another beacon was already being monitored it will
       * be ignored after this method is called.
       */
      void Monitor (const WLifetimeBeacon& beacon);
      void Monitor (const WLifetimeBeacon& beacon, WAtomic& at);
      //!}
      
      //!{
      /**
       * Returns true if the associated beacon still exists and Reset has not been called on it
       * since this monitor was attached to it. Otherwise false is returned. False is also returned
       * if this monitor is not attached to a beacon.
       */
      bool IsAlive () const;
      bool IsAlive (WAtomic& at) const;
      //!}
      
   private:
      typedef std::shared_ptr<WVar<bool>> AlivePtr;
      WVar<AlivePtr> m_alive_v;
   };

}}}
