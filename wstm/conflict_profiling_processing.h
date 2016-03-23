/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2016. All rights reserved.
****************************************************************************/

#pragma once

#include "exception.h"

#include <boost/variant.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <vector>
#include <istream>
#include <thread>

namespace WSTM
{
   namespace ConflictProfiling
   {
      struct WVarName
      {
         const void* m_var_p;
         const void* m_nameKey;
      };

      using TimePoint = std::chrono::high_resolution_clock::time_point;
      
      struct WConflict
      {
         const void* m_transactionNameKey;
         std::thread::id m_threadId;
         const void* m_threadNameKey;
         TimePoint m_start;
         TimePoint m_end;
         const void* m_fileNameKey;
         unsigned int m_line;
         std::vector<const void*> m_got;
      };
      
      struct WCommit
      {
         const void* m_transactionNameKey;
         std::thread::id m_threadId;
         const void* m_threadNameKey;
         TimePoint m_start;
         TimePoint m_end;
         const void* m_fileNameKey;
         unsigned int m_line;
         std::vector<const void*> m_set;
      };

      struct WName
      {
         const void* m_key;
         std::string m_name;
      };
 
      using WData = boost::variant<WVarName, WConflict, WCommit, WName>;

      class WReadError : public WException
      {
      public:
         WReadError ();
      };

      class WDataProcessor
      {
      public:
         WDataProcessor (std::istream& input);

         boost::optional<WData> NextDataItem ();
         
      private:
         std::istream& m_input;
         std::vector<uint8_t> m_buffer;
      };

   }
}
