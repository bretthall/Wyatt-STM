// Copyright (c) 2015, Wyatt Technology Corporation
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:

// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "exception.h"

#include <boost/variant.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <vector>
#include <istream>
#include <thread>
#include <unordered_map>
#include <map>

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

      using TransactionId = unsigned int;
      using TimePoint = std::chrono::high_resolution_clock::time_point;
      using ThreadId = unsigned int;
      
      struct WConflict
      {
         TransactionId m_id;
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
         TransactionId m_id;
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

         size_t GetNumItems () const;
         boost::optional<WData> NextDataItem ();

         class WPtrTranslator
         {
         public:
            WPtrTranslator ();

            VarId GetVarId (const void* var_p);
            boost::optional<NameKey> GetUniqueNameKey (const void* name_p, const std::string& str);
            NameKey GetNameKey (const void* name_p);
            ThreadId GetThreadId (const std::thread::id id);
            TransactionId GetTransactionId (const NameKey filename, const unsigned int line);
            
         private:
            VarId m_nextVarId;
            std::unordered_map<const void*, VarId> m_varIds;
            NameKey m_nextNameKey;
            std::unordered_map<const void*, NameKey> m_nameKeys;
            std::unordered_map<std::string, NameKey> m_uniqueNames;
            NameKey m_nextThreadId;
            std::unordered_map<std::thread::id, ThreadId> m_threadIds;
            TransactionId m_nextTransactionId;
            std::map<std::pair<NameKey, unsigned int>, TransactionId> m_transactionIds;
         };

      private:
         std::istream& m_input;
         size_t m_numItems;
         std::istream::pos_type m_beginPos;
         std::istream::pos_type m_namePos;
         bool m_readingNames;
         std::vector<uint8_t> m_buffer;
         WPtrTranslator m_translator;
      };

   }
}
