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
#include <unordered_map>

namespace WSTM
{
   namespace ConflictProfiling
   {
      using VarId = int32_t;
      using NameKey = int32_t;
      
      struct WVarName
      {
         VarId m_var;
         NameKey m_name;
      };

      using TimePoint = std::chrono::high_resolution_clock::time_point;
      using ThreadId = unsigned int;
      
      struct WConflict
      {
         NameKey m_transaction;
         ThreadId m_threadId;
         NameKey m_thread;
         TimePoint m_start;
         TimePoint m_end;
         NameKey m_file;
         unsigned int m_line;
         std::vector<VarId> m_got;
      };

      
      struct WCommit
      {
         NameKey m_transaction;
         ThreadId m_threadId;
         NameKey m_thread;
         TimePoint m_start;
         TimePoint m_end;
         NameKey m_file;
         unsigned int m_line;
         std::vector<VarId> m_set;
      };

      struct WName
      {
         NameKey m_key;
         std::string m_name;
      };
 
      using WData = boost::variant<WVarName, WConflict, WCommit, WName>;

      class WReadError : public std::runtime_error
      {
      public:
         WReadError ();
      };

      class WDataProcessor
      {
      public:
         WDataProcessor (std::istream& input);

         boost::optional<WData> NextDataItem ();

         class WPtrTranslator
         {
         public:
            WPtrTranslator ();

            VarId GetVarId (const void* var_p);
            NameKey GetNameKey (const void* name_p);
            ThreadId GetThreadId (const std::thread::id id);
            
         private:
            VarId m_nextVarId;
            std::unordered_map<const void*, VarId> m_varIds;
            NameKey m_nextNameKey;
            std::unordered_map<const void*, NameKey> m_nameKeys;
            NameKey m_nextThreadId;
            std::unordered_map<std::thread::id, ThreadId> m_threadIds;
         };

      private:
         std::istream& m_input;
         std::vector<uint8_t> m_buffer;
         WPtrTranslator m_translator;
      };

   }
}
