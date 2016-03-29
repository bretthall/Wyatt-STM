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

#include "conflict_profiling_internal.h"
#include "conflict_profiling_processing.h"
#include "maybe_unused.h"

#include <boost/format.hpp>
#include <boost/range/algorithm/transform.hpp>

#include <limits>
#include <unordered_set>
#include <fstream>
#include <algorithm>

namespace WSTM
{
   namespace ConflictProfilingInternal
   {
      namespace Frames
      {
         struct WFrameHeader
         {
            FrameType m_type;
            const char* m_name;
         };
      
         struct WVarName : WFrameHeader
         {
            void* m_var_p;
         };

         struct WTransaction : WFrameHeader
         {
            std::thread::id m_threadId;
            const char* m_threadName;
            std::chrono::high_resolution_clock::time_point m_start;
            std::chrono::high_resolution_clock::time_point m_end;
            const char* m_file;
            uint32_t m_line;
            uint32_t m_numVars;
            //will be followed by m_numVars void*'s
         };

         struct WNameData : WFrameHeader
         {
            uint32_t m_numChars;
            //will be followed by m_numChars char's
         };

         ptrdiff_t FrameSize (const WFrameHeader* header_p)
         {
            switch(header_p->m_type)
            {
            case FrameType::varName:
               return sizeof(WVarName);

            case FrameType::commit:
            case FrameType::conflict:
            {
               const auto frame_p = reinterpret_cast<const WTransaction*>(header_p);
               return (sizeof(WTransaction) + frame_p->m_numVars*sizeof(void*));
            }

            case FrameType::nameData:
            {
               const auto frame_p = reinterpret_cast<const WNameData*>(header_p);
               return (sizeof(WNameData) + frame_p->m_numChars);
            }
            
            default:
               //if we get here it means we added a frame type and didn't include it in this function
               assert (false);
               return(0);
            };
         }
      
      }

      WPage::WPage ():
         m_used (0)
      {
#ifdef WSTM_CONFLICT_PROFILING_INTEGRITY_CHECKING
         m_data.fill (0);
#endif
      }

      const uint8_t* WPage::GetData () const
      {
         return m_data.data () + pagePadding;
      }

      size_t WPage::GetUsed () const
      {
         return m_used;
      }
   
      size_t WPage::GetLeft () const
      {
         return pageSize - m_used;
      }
   
      uint8_t* WPage::Reserve (const size_t numBytes)
      {
         if (numBytes <= GetLeft ())
         {
            auto pos_p = m_data.data () + pagePadding + m_used;
            m_used += numBytes;
            return pos_p;
         }
         else
         {
            return nullptr;
         }
      }
         
      WPage* WPage::NewPage ()
      {
         if (!m_next_p)
         {
            m_next_p = std::make_unique<WPage>();
         }
         CheckIntegrity ();
         return m_next_p.get ();
      }
   
      std::unique_ptr<WPage> WPage::ReleaseNext ()
      {
         return std::move (m_next_p);
      }

      void WPage::Capture (std::unique_ptr<WPage>&& next_p)
      {
         assert (!m_next_p);
         if (next_p && (next_p->m_used != 0))
         {
            //don't bother saving the pages if they're empty
            m_next_p = std::move (next_p);
         }
      }
   
      void WPage::CheckIntegrity () const
      {
#ifdef WSTM_CONFLICT_PROFILING_INTEGRITY_CHECKING
         for (auto i = 0; i < pagePadding; ++i)
         {
            assert (m_data[i] == 0);
         }

         for (auto i = (m_used + pagePadding); i < (pageSize + 2*pagePadding); ++i)
         {
            assert (m_data[i] == 0);
         }
#endif
      }

      void WPage::Clear ()
      {
#ifdef WSTM_CONFLICT_PROFILING_INTEGRITY_CHECKING
         m_data.fill (0);
#endif
         m_used = 0;
         if (m_next_p)
         {
            m_next_p->Clear ();
         }
      }

      WMainData::WMainData ():
         m_lastPage_p (nullptr),
         m_numThreads (0),
         m_clearIndex (0)
      {}

      WMainData::~WMainData ()
      {
         auto lock = std::unique_lock<std::mutex>(m_mutex);
         while (m_numThreads > 0)
         {
            m_numThreadsCond.wait (lock);
         }

         auto names = std::unordered_set<const char*> ();

         const auto filename = boost::str (boost::format ("wstm_%1%.profile") % std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ()));
         std::ofstream out (filename, std::ios::out | std::ios::binary);

         //reserve space for the number of records and the name offset (we need the name offset so that they can be read first
         //during processing)
         const auto beginPos = std::streamoff (out.tellp ());
         auto numRecords = size_t (0);
         out.write (reinterpret_cast<const char*>(&numRecords), sizeof(numRecords));
         out.write (reinterpret_cast<const char*>(&beginPos), sizeof(beginPos));
         
         auto skipBytes = ptrdiff_t (0);
         std::unique_ptr<WPage> cur_p = std::move (m_firstPage_p);
         while (cur_p)
         {
            auto data_p = cur_p->GetData ();
            out.write (reinterpret_cast<const char*>(data_p), cur_p->GetUsed ());

            const auto dataEnd_p = data_p + cur_p->GetUsed ();
            auto bytesLeft = (dataEnd_p - data_p);
            if (bytesLeft >= skipBytes)
            {
               data_p += skipBytes;
               bytesLeft -= skipBytes;
               while (data_p < dataEnd_p)
               {
                  ++numRecords;
                  auto header_p = reinterpret_cast<const Frames::WFrameHeader*>(data_p);
                  names.insert (header_p->m_name);
                  if ((header_p->m_type == Frames::FrameType::commit) || (header_p->m_type == Frames::FrameType::conflict))
                  {
                     auto frame_p = reinterpret_cast<const Frames::WTransaction*>(data_p);
                     names.insert (frame_p->m_threadName);
                     names.insert (frame_p->m_file);
                  }
               
                  skipBytes = FrameSize (header_p);
                  if (bytesLeft >= skipBytes)
                  {
                     data_p += skipBytes;
                     bytesLeft -= skipBytes;
                     skipBytes = 0;
                  }
                  else
                  {
                     skipBytes -= bytesLeft;
                     data_p = dataEnd_p;
                  }
               }
            }
            else
            {
               skipBytes -= bytesLeft;
            }            

            cur_p = cur_p->ReleaseNext ();
         }

         const auto namePos = std::streamoff (out.tellp ());

         auto nameData = Frames::WNameData ();
         nameData.m_type = Frames::FrameType::nameData;
         names.erase (nullptr);
         for (const auto name_p: names)
         {
            if (name_p != nullptr)
            {
               nameData.m_name = name_p;
               nameData.m_numChars = strlen (name_p);
               out.write (reinterpret_cast<const char*>(&nameData), sizeof(Frames::WNameData));
               out.write (name_p, nameData.m_numChars);
               ++numRecords;
            }
         }

         static_assert (sizeof(namePos) == sizeof(beginPos), "stream position size mismatch");
         out.seekp (beginPos);
         out.write (reinterpret_cast<const char*>(&numRecords), sizeof(numRecords));
         out.write (reinterpret_cast<const char*>(&namePos), sizeof(namePos));
      }

      void WMainData::NewThread ()
      {
         auto lock = std::unique_lock<std::mutex>(m_mutex);
         ++m_numThreads;
      }
   
      void WMainData::ThreadDone (std::unique_ptr<WPage>&& first_p, WPage* last_p)
      {
         auto lock = std::unique_lock<std::mutex>(m_mutex);

         if (first_p)
         {
            assert (last_p != nullptr);
         
            if (m_lastPage_p)
            {
               m_lastPage_p->Capture (std::move (first_p));
            }
            else
            {
               assert (!m_firstPage_p);
               m_firstPage_p = std::move (first_p);
            }
            m_lastPage_p = last_p;
         }
         else
         {
            assert (last_p == nullptr);
         }

         --m_numThreads;
         m_numThreadsCond.notify_one ();
      }

      void WMainData::Clear ()
      {
         auto lock = std::unique_lock<std::mutex>(m_mutex);
         ++m_clearIndex;
         m_firstPage_p = nullptr;
         m_lastPage_p = nullptr;
      }

      size_t WMainData::GetClearIndex () const
      {
         //don't need mutex lock here, m_clearIndex is atomic
         return m_clearIndex;
      }
   
      WThreadData::WThreadData (WMainData& mainData):
         m_mainData (mainData),
         m_curPage_p (nullptr),
         m_clearIndex (mainData.GetClearIndex ()),
         m_threadName (nullptr),
         m_curTransactionFile (nullptr),
         m_curTransactionLine (-1),
         m_curTransactionName (nullptr),
         m_inChildTransaction (0)
      {
         m_mainData.NewThread ();
      }

      WThreadData::~WThreadData ()
      {
         m_mainData.ThreadDone (std::move (m_firstPage_p), m_curPage_p);
      }
   
      void WThreadData::NameThread (const char* name)
      {
         assert (m_threadName == nullptr);
         m_threadName = name;
      }

      ConflictProfiling::Internal::WOnTransactionEnd WThreadData::StartTransaction (const char* file, const int line)
      {
         //WHat about sub-transactions??????????????????????????????????????????????????????????????????????????
         // -> array of transaction trackers, output "sub-transaction" frames for sub-transactions,
         //sub-transaction frames will need both get and set vars since we don't know if the top-level
         //will have a conflict or not
         // -> probably be better to only report data for top-level transactions

         //=> Currently only dealing with top-level transactions, we ignore child transactions, their
         //read and write sets will be recorded when the top-level transaction commits or aborts
#ifdef WSTM_CONFLICT_PROFILING
         if (m_inChildTransaction == 0)
         {
            m_curTransactionFile = file;
            m_curTransactionLine = line;
            m_curTransactionName = nullptr;
         }
         ++m_inChildTransaction;
         return ConflictProfiling::Internal::WOnTransactionEnd (&m_inChildTransaction);
#else
         Internal::MaybeUnused (file, line);
         return ConflictProfiling::Internal::WOnTransactionEnd ();
#endif
      }

      void WThreadData::StartTransactionAttempt ()
      {
         if (NotInChildTransaction ())
         {
            m_curTransactionStart = std::chrono::high_resolution_clock::now ();
         }
      }

      void WThreadData::TransactionEnd (const Frames::FrameType type, const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& vars)
      {
         Internal::MaybeUnused (type, end, vars);
      
         if (NotInChildTransaction ())
         {
            const auto oldPage_p = m_curPage_p;
            if (oldPage_p)
            {
               oldPage_p->CheckIntegrity ();
            }

            auto dest_p = GetNextDest (sizeof(Frames::WTransaction));
            auto frame_p = reinterpret_cast<Frames::WTransaction*>(dest_p);
            frame_p->m_type = type;
            frame_p->m_name = m_curTransactionName;
            frame_p->m_threadId = std::this_thread::get_id ();
            frame_p->m_threadName = m_threadName;
            frame_p->m_start = m_curTransactionStart;
            frame_p->m_end = end;
            frame_p->m_file = m_curTransactionFile;
            frame_p->m_line = static_cast<uint32_t>(m_curTransactionLine);
            frame_p->m_numVars = static_cast<uint32_t>(vars.size ());
            m_curPage_p->CheckIntegrity ();
         
            //we might not be able to fit all the vars on one page, split them up if need be
            auto varMemLeft = sizeof(void*)*vars.size ();
            auto varsIt = std::begin (vars);
            while (varMemLeft > 0)
            {
               assert (varsIt != std::end (vars));
            
               auto memToUse = std::min (varMemLeft, m_curPage_p->GetLeft ());
               auto numThatFit = memToUse/sizeof(void*);
               if (numThatFit == 0)
               {
                  m_curPage_p = m_curPage_p->NewPage ();
                  memToUse = std::min (varMemLeft, m_curPage_p->GetLeft ());
                  numThatFit = memToUse/sizeof(void*);
               }
               auto vars_p = reinterpret_cast<void**>(m_curPage_p->Reserve (memToUse));
               auto varsEnd = varsIt;
               std::advance (varsEnd, numThatFit);
               std::transform (varsIt, varsEnd,
                               vars_p,
                               [](const auto& val) -> void*
                               {
                                  return std::get<0>(val).get ();
                               });
               m_curPage_p->CheckIntegrity ();
               varsIt = varsEnd;
               varMemLeft -= memToUse;
            }

            m_curPage_p->CheckIntegrity ();
         }
         //NOTE: decrementing m_inChildTransaction is handled by the WTransactionEnd returned by StartTransaction
      }      

      void WThreadData::Commit (const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& setVars)
      {
         TransactionEnd (Frames::FrameType::commit, end, setVars);
      }
   
      void WThreadData::Conflict (const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& getVars)
      {
         TransactionEnd (Frames::FrameType::conflict, end, getVars);
      }
   
      void WThreadData::NameTransaction (const char* name)
      {
         //only the top-level transaction can set the name
         if (NotInChildTransaction ())
         {
            m_curTransactionName = name;
         }
      }
   
      void WThreadData::NameVar (void* var_p, const char* name)
      {
         auto dest_p = GetNextDest (sizeof(Frames::WVarName));
         auto frame_p = reinterpret_cast<Frames::WVarName*>(dest_p);
         frame_p->m_type = Frames::FrameType::varName;
         frame_p->m_var_p = var_p;
         frame_p->m_name = name;
         m_curPage_p->CheckIntegrity ();
      }

      uint8_t* WThreadData::GetNextDest (const size_t size)
      {
         if (m_clearIndex != m_mainData.GetClearIndex ())
         {
            m_firstPage_p->Clear ();
         }

         if (m_curPage_p)
         {
            if (m_curPage_p->GetLeft () < size)
            {
               m_curPage_p = m_curPage_p->NewPage ();
            }
         }
         else
         {
            m_firstPage_p = std::make_unique<WPage>();
            m_curPage_p = m_firstPage_p.get ();
         }

         return m_curPage_p->Reserve (size);
      }

      bool WThreadData::NotInChildTransaction () const
      {
         return m_inChildTransaction == 1;
      }
   }

   namespace ConflictProfiling
   {
      namespace
      {
         WVarName ConvertVarName (WDataProcessor::WPtrTranslator& translator, const ConflictProfilingInternal::Frames::WFrameHeader* header_p)
         {
            assert (header_p->m_type == ConflictProfilingInternal::Frames::FrameType::varName);
            auto frame_p = reinterpret_cast<const ConflictProfilingInternal::Frames::WVarName*>(header_p);
            auto data = WVarName ();
            data.m_var = translator.GetVarId (frame_p->m_var_p);
            data.m_name = translator.GetNameKey (frame_p->m_name);
            return data;
         }

         WConflict ConvertConflict (WDataProcessor::WPtrTranslator& translator, const ConflictProfilingInternal::Frames::WFrameHeader* header_p)
         {
            assert (header_p->m_type == ConflictProfilingInternal::Frames::FrameType::conflict);
            auto frame_p = reinterpret_cast<const ConflictProfilingInternal::Frames::WTransaction*>(header_p);
            auto data = WConflict ();
            data.m_transaction = translator.GetNameKey (frame_p->m_name);
            data.m_threadId = translator.GetThreadId (frame_p->m_threadId);
            data.m_thread = translator.GetNameKey (frame_p->m_threadName);
            data.m_start = frame_p->m_start;
            data.m_end = frame_p->m_end;
            data.m_file = translator.GetNameKey (frame_p->m_file);
            data.m_line = frame_p->m_line;
            data.m_id = translator.GetTransactionId (data.m_file, data.m_line);
            auto var_p = reinterpret_cast<const void* const*>(reinterpret_cast<const uint8_t*>(frame_p) + sizeof(ConflictProfilingInternal::Frames::WTransaction));
            data.m_got = std::vector<VarId>(frame_p->m_numVars);
            std::transform (var_p, var_p + frame_p->m_numVars,
                            std::begin (data.m_got),
                            [&translator](const void* v_p) {return translator.GetVarId (v_p);});
            return data;
         }

         WCommit ConvertCommit (WDataProcessor::WPtrTranslator& translator, const ConflictProfilingInternal::Frames::WFrameHeader* header_p)
         {
            assert (header_p->m_type == ConflictProfilingInternal::Frames::FrameType::commit);
            auto frame_p = reinterpret_cast<const ConflictProfilingInternal::Frames::WTransaction*>(header_p);
            auto data = WCommit ();
            data.m_transaction = translator.GetNameKey (frame_p->m_name);
            data.m_threadId = translator.GetThreadId (frame_p->m_threadId);
            data.m_thread = translator.GetNameKey (frame_p->m_threadName);
            data.m_start = frame_p->m_start;
            data.m_end = frame_p->m_end;
            data.m_file = translator.GetNameKey (frame_p->m_file);
            data.m_line = frame_p->m_line;
            data.m_id = translator.GetTransactionId (data.m_file, data.m_line);
            auto var_p = reinterpret_cast<const void* const*>(reinterpret_cast<const uint8_t*>(frame_p) + sizeof(ConflictProfilingInternal::Frames::WTransaction));
            data.m_set = std::vector<VarId>(frame_p->m_numVars);
            std::transform (var_p, var_p + frame_p->m_numVars,
                            std::begin (data.m_set),
                            [&translator](const void* v_p) {return translator.GetVarId (v_p);});
            return data;
         }

         boost::optional<WName> ConvertNameData (WDataProcessor::WPtrTranslator& translator, const ConflictProfilingInternal::Frames::WNameData* frame_p)
         {
            auto chars_p = reinterpret_cast<const char*>(frame_p) + sizeof(ConflictProfilingInternal::Frames::WNameData);
            auto str = std::string(chars_p, chars_p + frame_p->m_numChars);            
            const auto key_o = translator.GetUniqueNameKey (frame_p->m_name, str);
            if (key_o)
            {
               auto data = WName ();
               data.m_name = std::move (str);
               data.m_key = *key_o;
               return data;
            }
            else
            {
               return boost::none;
            }
         }

         constexpr auto defaultBufferSize = size_t (1024);
      }

      WReadError::WReadError ():
         std::runtime_error ("Error reading from conflict profiling data file")
      {}

      WDataProcessor::WDataProcessor (std::istream& input):
         m_input (input),
         m_readingNames (true),
         m_buffer (defaultBufferSize)
      {
         m_input.read (reinterpret_cast<char*>(&m_numItems), sizeof(m_numItems));
         auto namePos = std::streamoff (0);
         m_input.read (reinterpret_cast<char*>(&namePos), sizeof(namePos));
         m_namePos = namePos;
         m_beginPos = m_input.tellg ();
         m_input.seekg (m_namePos);
      }

      size_t WDataProcessor::GetNumItems () const
      {
         return m_numItems;
      }

      namespace
      {
         size_t GetFrameSize (const uint8_t* data_p)
         {
            using namespace  ConflictProfilingInternal;
            const auto header_p = reinterpret_cast<const Frames::WFrameHeader*>(data_p);
            switch(header_p->m_type)
            {
            case Frames::FrameType::varName:
               return sizeof (Frames::WVarName);
        
            case Frames::FrameType::commit:
            case Frames::FrameType::conflict:
               return sizeof (Frames::WTransaction);
        
            case Frames::FrameType::nameData:
               //shouldn't get here, we handle name data differently
               assert (false);
               return sizeof (Frames::WNameData);

            default:
               //if we get here a new frame type was added but not handled here
               assert (false);
               return 0;
            };
         }

         size_t GetTotalSize (const uint8_t* data_p)
         {
            using namespace  ConflictProfilingInternal;
            const auto header_p = reinterpret_cast<const Frames::WFrameHeader*>(data_p);
            switch(header_p->m_type)
            {
            case Frames::FrameType::varName:
               return sizeof(Frames::WVarName);
            
            case Frames::FrameType::commit:
            case Frames::FrameType::conflict:
            {
               const auto frame_p = reinterpret_cast<const Frames::WTransaction*>(data_p);
               return sizeof (Frames::WTransaction) + sizeof(void*)*frame_p->m_numVars;
            }
         
            case Frames::FrameType::nameData:
            {
               //shouldn't get here, we handle name data differently
               assert (false);
               const auto frame_p = reinterpret_cast<const Frames::WNameData*>(data_p);
               return sizeof (Frames::WNameData) + frame_p->m_numChars;
            }
         
            default:
               //if we get here a new frame type was added but not handled here
               assert (false);
               return 0;
            };

         }
      
      }

      boost::optional<WData> WDataProcessor::NextDataItem ()
      {
         using namespace  ConflictProfilingInternal;

         if (m_readingNames)
         {
            auto CheckEOF = [&]()
               {
                  if (m_input.eof ())
                  {
                     m_readingNames = false;
                     m_input.clear ();
                     //seek back to just past the name offset at start of file
                     m_input.seekg (m_beginPos);
                     const auto pos = m_input.tellg ();
                     WSTM::Internal::MaybeUnused (pos);
                     return NextDataItem ();
                  }
                  else
                  {
                     throw WReadError ();
                  }
               };

            assert (m_buffer.size () >= sizeof(Frames::WNameData));
            auto name_o = boost::optional<WName>();
            while (!name_o)
            {
               if (!m_input.read (reinterpret_cast<char*>(m_buffer.data ()), sizeof(Frames::WNameData)))
               {
                  return CheckEOF ();
               }
               auto name_p = reinterpret_cast<const Frames::WNameData*>(m_buffer.data ());
               if (name_p->m_type != Frames::FrameType::nameData)
               {
                  throw WReadError ();
               }

               const auto frameSize = sizeof(Frames::WNameData) + name_p->m_numChars;
               if (m_buffer.size () < frameSize)
               {
                  m_buffer.resize (frameSize);
                  name_p = reinterpret_cast<const Frames::WNameData*>(m_buffer.data ());
               }
               if (!m_input.read (reinterpret_cast<char*>(m_buffer.data ()) + sizeof(Frames::WNameData), frameSize - sizeof(Frames::WNameData)))
               {
                  return CheckEOF ();
               }

               name_o = ConvertNameData (m_translator, name_p);
            }
            
            return WData (*name_o);            
         }
         else
         {
            static auto count = 0;
            ++count;
            
            const auto curPos = m_input.tellg ();
            if (curPos >= m_namePos)
            {
               return boost::none;
            }

            assert (m_buffer.size () >= sizeof(Frames::WFrameHeader));
            if (!m_input.read (reinterpret_cast<char*>(m_buffer.data ()), sizeof(Frames::WFrameHeader)))
            {
               throw WReadError ();
            }
            auto header_p = reinterpret_cast<const Frames::WFrameHeader*>(m_buffer.data ());
            if (header_p->m_type == Frames::FrameType::nameData)
            {
               throw WReadError ();
            }

            const auto frameSize = GetFrameSize (m_buffer.data ());
            if (!m_input.read (reinterpret_cast<char*>(m_buffer.data ()) + sizeof(Frames::WFrameHeader), frameSize - sizeof(Frames::WFrameHeader)))
            {
               throw WReadError ();
            }

            const auto totalSize = GetTotalSize (m_buffer.data ());
            if (totalSize != frameSize)
            {
               if (totalSize > m_buffer.size ())
               {
                  m_buffer.resize (totalSize);
                  header_p = reinterpret_cast<const Frames::WFrameHeader*>(m_buffer.data ());
               }

               if (!m_input.read (reinterpret_cast<char*>(m_buffer.data ()) + frameSize, totalSize - frameSize))
               {
                  throw WReadError ();
               }
            }
      
            switch(header_p->m_type)
            {
            case Frames::FrameType::varName:
               return WData (ConvertVarName (m_translator, header_p));
        
            case Frames::FrameType::commit:
               return WData (ConvertCommit (m_translator, header_p));
        
            case Frames::FrameType::conflict:
               return WData (ConvertConflict (m_translator, header_p));
        
            case Frames::FrameType::nameData:
               //we shouldn't get here
               assert (false);
               throw WReadError ();
         
            default:
               //if we get here a new frame type was added but not handled here
               assert (false);
               return boost::none;
            }
         }
         
      }

      WDataProcessor::WPtrTranslator::WPtrTranslator ():
         m_nextVarId (0),
         m_nextNameKey (0),
         m_nextThreadId (0),
         m_nextTransactionId (0)
      {}
         
      VarId WDataProcessor::WPtrTranslator::GetVarId (const void* var_p)
      {
         if (var_p != nullptr)
         {
            const auto it = m_varIds.find (var_p);
            if (it != std::end (m_varIds))
            {
               return it->second;
            }
            else
            {
               m_varIds[var_p] = m_nextVarId;
               return m_nextVarId++;
            }
         }
         else
         {
            return -1;
         }
      }

      boost::optional<NameKey> WDataProcessor::WPtrTranslator::GetUniqueNameKey (const void* name_p, const std::string& str)
      {
         //this should only be used when reading WNameData objects which won't have null name_p
         assert (name_p != nullptr);
         if (name_p != nullptr)
         {
            const auto it = m_uniqueNames.find (str);
            if (it != std::end (m_uniqueNames))
            {
               m_nameKeys[name_p] = it->second;
               return boost::none;
            }
            else
            {
               m_uniqueNames[str] = m_nextNameKey;
               m_nameKeys[name_p] = m_nextNameKey;
               return m_nextNameKey++;
            }
         }
         else
         {
            return -1;
         }
      }

      NameKey WDataProcessor::WPtrTranslator::GetNameKey (const void* name_p)
      {
         if (name_p != nullptr)
         {
            const auto it = m_nameKeys.find (name_p);
            if (it != std::end (m_nameKeys))
            {
               return it->second;
            }
            else
            {
               //all the name keys for which we have strings were set up when we read the names
               assert (false);
               return -1;
            }
         }
         else
         {
            return -1;
         }
      }

      ThreadId WDataProcessor::WPtrTranslator::GetThreadId (const std::thread::id tid)
      {
         const auto it = m_threadIds.find (tid);
         if (it != std::end (m_threadIds))
         {
            return it->second;
         }
         else
         {
            m_threadIds[tid] = m_nextThreadId;
            return m_nextThreadId++;
         }
      }

      TransactionId WDataProcessor::WPtrTranslator::GetTransactionId (const NameKey filename, const unsigned int line)
      {
         const auto key = std::make_pair (filename, line);
         const auto it = m_transactionIds.find (key);
         if (it != std::end (m_transactionIds))
         {
            return it->second;
         }
         else
         {
            m_transactionIds[key] = m_nextTransactionId;
            return m_nextTransactionId++;
         }
      }

      namespace Internal
      {
         
         WOnTransactionEnd::WOnTransactionEnd (unsigned int* inChildTransaction_p):
            m_inChildTransaction_p (inChildTransaction_p)
         {}
   
         WOnTransactionEnd::WOnTransactionEnd (WOnTransactionEnd&& e):
            m_inChildTransaction_p (e.m_inChildTransaction_p)
         {
            e.m_inChildTransaction_p = nullptr;
         }
   
         WOnTransactionEnd& WOnTransactionEnd::operator= (WOnTransactionEnd&& e)
         {
            m_inChildTransaction_p = e.m_inChildTransaction_p;
            e.m_inChildTransaction_p = nullptr;
            return *this;
         }

         WOnTransactionEnd::~WOnTransactionEnd ()
         {
            if (m_inChildTransaction_p)
            {
               --(*m_inChildTransaction_p);
               m_inChildTransaction_p = nullptr;
            }
         }
      }

   }
}

