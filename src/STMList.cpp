/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2012. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "STMList.h"

namespace bss
{
	namespace thread
	{
      namespace STM
      {
         WSTMListOutOfBoundsError::WSTMListOutOfBoundsError ():
            std::exception ("STM list iterator out of bounds")
         {}
      }      
   }
}
   
