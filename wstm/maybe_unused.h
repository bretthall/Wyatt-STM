/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2016. All rights reserved.
****************************************************************************/

#pragma once

namespace WSTM
{
   namespace Internal
   {
      template <typename ... Types_t>
      void MaybeUnused (Types_t&&...)
      {}
   }

}
