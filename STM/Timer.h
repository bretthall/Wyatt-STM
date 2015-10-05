/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#pragma once

#include "stm.h"
#include "BSS/wtcbss.h"

#include <boost/chrono.hpp>

#include <type_traits>

namespace bss { namespace thread { namespace STM
{

   /**
    * A transaction version of boost::timer. Here "transactional" means that resetting the timer is
    * transacted (and thus thread safe).
    */
   class BSS_CLASSAPI WTimer
   {
   public:
      /**
       * Constructor. Starts the timer at zero.
       */
      WTimer ();

      //!{
      /**
       * Restarts the timer to zero.
       */
      void Restart ();
      void Restart (WAtomic& at);
      //!}

      //!{
      /**
       * Gets the elapsed time since the last reset.
       *
       * @param Duration_t The boost::chrono::duration to use. boost::chrono::duration_cast is used
       * to do the conversion so be aware that your might get roinding dependiong on the duration type.
       */
      template <typename Duration_t>
      Duration_t Elapsed () const;
      template <typename Duration_t>
      Duration_t Elapsed (WAtomic& at) const;
      //!}

      //!{
      /**
       * Gets the number of seconds that have elapsed since the last reset.
       */
      double ElapsedSeconds () const;
      double ElapsedSeconds (WAtomic& at) const;
      //!}

      //!{
      /**
       * Gets the time_point for the last time the timer was reset.
       */
      boost::chrono::steady_clock::time_point GetStart () const;
      boost::chrono::steady_clock::time_point GetStart (WAtomic& at) const;
      //!}
      
   private:
      static_assert (std::is_same<
                     boost::chrono::steady_clock::duration,
                     boost::chrono::nanoseconds>::value,
                     "Bad clock duration");
      
      boost::chrono::nanoseconds ElapsedDefault (WAtomic& at) const;
      
      WVar<boost::chrono::steady_clock::time_point> m_start_v;
   };

   template <typename Duration_t>
   Duration_t WTimer::Elapsed () const
   {
      return Atomically ([&](WAtomic& at){return this->Elapsed<Duration_t>(at);});
   }
   

   template <typename Duration_t>
   Duration_t WTimer::Elapsed (WAtomic& at) const
   {
      return boost::chrono::duration_cast<Duration_t>(ElapsedDefault (at));
   }

   
}}}

