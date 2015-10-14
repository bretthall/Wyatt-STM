/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2009. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "MailBox.h"

namespace bss { namespace thread { namespace STM
{

   WMailBoxBadThreadError::WMailBoxBadThreadError (const char* method):
      m_method (method)
   {}

}}}
