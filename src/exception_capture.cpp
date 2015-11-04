/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "exception_capture.h"

namespace WSTM
{
   WExceptionCapture::WExceptionCapture ()
   {}

   WExceptionCapture::WExceptionCapture (const WExceptionCapture& exc):
      m_thrower_v (exc.m_thrower_v.GetReadOnly ())
   {}

   WExceptionCapture& WExceptionCapture::operator= (const WExceptionCapture& exc)
   {
      Atomically ([&](WAtomic& at)
                  {
                     m_thrower_v.Set (exc.m_thrower_v.Get(at), at);
                  });
      return *this;
   }

   WExceptionCapture::WExceptionCapture (WExceptionCapture&& exc):
      m_thrower_v (std::move (exc.m_thrower_v))
   {}

   WExceptionCapture& WExceptionCapture::operator= (WExceptionCapture&& exc)
   {
      m_thrower_v = std::move (exc.m_thrower_v);
      return *this;
   }

   void WExceptionCapture::Capture (const WExceptionCapture& exc)
   {
      Atomically ([&](WAtomic& at){Capture (exc, at);});
   }
   
   void WExceptionCapture::Capture (const WExceptionCapture& exc, WAtomic& at)
   {
      m_thrower_v.Set (exc.m_thrower_v.Get (at), at);
   }

   void WExceptionCapture::Reset ()
   {
      m_thrower_v.Set (Thrower ());
   }
   
   void WExceptionCapture::Reset (WAtomic& at)
   {
      m_thrower_v.Set (Thrower (), at);
   }

   void WExceptionCapture::ThrowCaptured () const
   {
      Atomically ([&](WAtomic& at){ThrowCaptured (at);});
   }
   
   void WExceptionCapture::ThrowCaptured (WAtomic& at) const
   {
      Thrower thrower = m_thrower_v.Get (at);
      if (thrower)
      {
         thrower ();
      }
   }

   WExceptionCapture::operator bool () const
   {
      return bool (m_thrower_v.GetReadOnly ());
   }
   
   bool WExceptionCapture::HasCaptured(WAtomic& at) const
   {
      return bool (m_thrower_v.Get (at));
   }

}
