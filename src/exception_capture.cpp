/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "stdafx.h"
#include "ExceptionCaptureAtomic.h"
#include "BSS/Common/ExceptionCapture.h"

#ifdef WIN32
#pragma warning (disable: 4127 4239 4244 4265 4503 4512 4640 6011)
#endif

#include <boost/bind.hpp>
using boost::bind;
using boost::cref;

namespace bss { namespace thread { namespace STM
{
   WExceptionCaptureAtomic::WExceptionCaptureAtomic ()
   {}

   WExceptionCaptureAtomic::WExceptionCaptureAtomic (const WExceptionCaptureAtomic& exc):
      m_thrower_v (exc.m_thrower_v.GetReadOnly ())
   {}

   WExceptionCaptureAtomic::WExceptionCaptureAtomic (const ::bss::WExceptionCapture& exc):
      m_thrower_v (exc.m_thrower)
   {}

   void WExceptionCaptureAtomic::Capture (const WExceptionCaptureAtomic& exc)
   {
      Atomically (boost::bind (&WExceptionCaptureAtomic::CaptureAt, this, cref (exc), _1));
   }
   
   void WExceptionCaptureAtomic::Capture (const WExceptionCaptureAtomic& exc, WAtomic& at)
   {
      CaptureAt (exc, at);
   }

   void WExceptionCaptureAtomic::CaptureAt (const WExceptionCaptureAtomic& exc, WAtomic& at)
   {
      m_thrower_v.Set (exc.m_thrower_v.Get (at), at);
   }

   void WExceptionCaptureAtomic::Capture (const WExceptionCapture& exc)
   {
      m_thrower_v.Set (exc.m_thrower);
   }
   
   void WExceptionCaptureAtomic::Capture (const WExceptionCapture& exc, WAtomic& at)
   {
      m_thrower_v.Set (exc.m_thrower, at);
   }

   void WExceptionCaptureAtomic::Reset ()
   {
      m_thrower_v.Set (Thrower ());
   }
   
   void WExceptionCaptureAtomic::Reset (WAtomic& at)
   {
      m_thrower_v.Set (Thrower (), at);
   }

   void WExceptionCaptureAtomic::ThrowCaptured () const
   {
      Atomically (
         boost::bind (&WExceptionCaptureAtomic::ThrowCaptured, this, _1));
   }
   
   void WExceptionCaptureAtomic::ThrowCaptured (WAtomic& at) const
   {
      Thrower thrower = m_thrower_v.Get (at);
      if (thrower)
      {
         thrower ();
      }
   }

   WExceptionCaptureAtomic::operator bool () const
   {
      return m_thrower_v.GetReadOnly ();
   }
   
   bool WExceptionCaptureAtomic::HasCaptured(WAtomic& at) const
   {
      return m_thrower_v.Get (at);
   }

}}}
