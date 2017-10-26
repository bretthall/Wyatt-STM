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

#include "stm.h"

#include <boost/format.hpp>
using boost::format;
using boost::str;

#ifdef NO_THREAD_LOCAL
#include <boost/thread/tss.hpp>
#endif

#include <condition_variable>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <list>
#include <thread>

//Define this to turn on stm profiling
//#define STM_PROFILING

namespace  WSTM
{
   WLibraryVersion GetVersion ()
   {
      return {WSTM_VERSION_MAJOR, WSTM_VERSION_MINOR, WSTM_VERSION_PATCH};
   }
   
   namespace Internal
   {
      WLocalValueBase::~WLocalValueBase ()
      {}

      uint64_t GetTransactionLocalKey ()
      {
         static std::atomic<uint64_t> nextKey (0);
         return nextKey.fetch_add (1);
      }
   }
   
   namespace
   {
#ifdef STM_PROFILING
      std::mutex s_profileMutex;
      std::chrono::high_resolution_clock::time_point s_profileStart;   
      std::atomic<unsigned int> s_numConflicts;
      std::atomic<unsigned int> s_numReadCommits;
      std::atomic<unsigned int> s_numWriteCommits;
#endif //STM_PROFILING

      void IncrementNumConflicts ()
      {
#ifdef STM_PROFILING
         ++s_numConflicts;
#endif
      }
   
      void IncrementNumReadCommits ()
      {
#ifdef STM_PROFILING
         ++s_numReadCommits;
#endif
      }

      void IncrementNumWriteCommits ()
      {
#ifdef STM_PROFILING
         ++s_numWriteCommits;
#endif
      }
   }

   void StartProfiling ()
   {
#ifdef STM_PROFILING
      std::unique_lock<std::mutex> lock (s_profileMutex);
      s_profileStart = std::chrono::high_resolution_clock::now ();
      s_numConflicts = 0;
      s_numReadCommits = 0;
      s_numWriteCommits = 0;      
#endif
   }

   std::string WProfileData::FormatData () const
   {
#ifdef STM_PROFILING
      const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(m_end - m_start).count ();
      return str (format ("\ttime = %1%secs\n"
                          "\tconflicts = %2%/sec (%3% total)\n"
                          "\treads = %4%/sec (%5% total)\n"
                          "\twrites = %6%/sec (%7% total)")
                  % elapsed
                  % (static_cast<double>(m_numConflicts)/elapsed)
                  % m_numConflicts
                  % (static_cast<double>(m_numReadCommits)/elapsed)
                  % m_numReadCommits
                  % (static_cast<double>(m_numWriteCommits)/elapsed)
                  % m_numWriteCommits);
#else
      return "\tProfiling not active";
#endif 
   }

   WProfileData Checkpoint ()
   {
      WProfileData data;
#ifdef STM_PROFILING
      data.m_numConflicts = s_numConflicts;
      data.m_numReadCommits = s_numReadCommits;
      data.m_numWriteCommits = s_numWriteCommits;
      data.m_end = std::chrono::high_resolution_clock::now ();
      std::unique_lock<std::mutex> lock (s_profileMutex);
      data.m_start = s_profileStart;
#endif      
      return data;
   }

   const unsigned int UNLIMITED = std::numeric_limits<unsigned int>::max();

   namespace
   {
      //This mutex locks out commits while a STM::Var is reading its own
      //value, while commiting this mutex is write locked so that all
      //var reads are held until the commit finishes. It is also
      //upgrade locked when a transaction is running with other
      //commits locked out.
      boost::upgrade_mutex s_readMutex;

#ifdef NO_THREAD_LOCAL

      //Some compilers don't support thread_local (e.g. apple clang), on those platforms we resort
      //to using boost::thread_specific_pointer
      template <typename Type_t>
      class WThreadLocalValueDefaultCtor
      {
      public:
         Type_t* operator->()
         {
            auto value_p = m_value_p.get ();
            if (!value_p)
            {
               value_p = new Type_t;
               m_value_p.reset (value_p);
            }
            return value_p;
         }

      protected:         
         boost::thread_specific_ptr<Type_t> m_value_p;
      };

#define THREAD_LOCAL(type, name) WThreadLocalValueDefaultCtor<type> name;//

      template <typename Type_t>
      class WThreadLocalValueWithInitValue
      {
      public:
         explicit WThreadLocalValueWithInitValue (const Type_t& value):
            m_initValue (value)
         {}

         operator Type_t ()
         {
            return *GetValuePtr ();
         }

         Type_t& operator= (const Type_t value)
         {
            auto value_p = GetValuePtr ();
            *value_p = value;
            return *value_p;
         }
         
      private:
         Type_t* GetValuePtr ()
         {
            auto value_p = m_value_p.get ();
            if (!value_p)
            {
               value_p = new Type_t (m_initValue);
               m_value_p.reset (value_p);
            }
            return value_p;
         }
         
         Type_t m_initValue;
         boost::thread_specific_ptr<Type_t> m_value_p;
      };

#define THREAD_LOCAL_WITH_INIT_VALUE(type, name, value) WThreadLocalValueWithInitValue<type> name (value);//

#else

#define THREAD_LOCAL(type, name) thread_local type name##__Obj; thread_local type* const name = &name##__Obj;//
#define THREAD_LOCAL_WITH_INIT_VALUE(type, name, value) thread_local type name = value;//

#endif //#else NO_THREAD_LOCAL      
      
#ifdef _DEBUG
THREAD_LOCAL_WITH_INIT_VALUE (bool, s_readMutexReadLocked, false);
THREAD_LOCAL_WITH_INIT_VALUE (bool, s_readMutexUpgradeLocked, false);
THREAD_LOCAL_WITH_INIT_VALUE (bool, s_readMutexWriteLocked, false);
THREAD_LOCAL_WITH_INIT_VALUE (bool, s_committing, false);
#endif //_DEBUG
      
      //This signal is notified when a commit succeeds. s_readMutex
      //must be used with this signal. A read lock lock should be
      //used when waiting. A write lock shoudl be held when
      //notifiying.
      std::condition_variable_any s_commitSignal;
         
      //exception thrown by Retry() to signal AtomicallyImpl that it should
      //"retry" the current operation. 
      struct WRetryException
      {
         WRetryException(const WTimeArg& timeout):
            m_timeout(timeout)
         {}

         const WTimeArg m_timeout;
      };   
   }
   
   WCantContinueException::WCantContinueException(const std::string& msg):
      WException(msg)
   {}
   
   WMaxConflicts::WMaxConflicts ():
      m_max (UNLIMITED),
      m_resolution (WConflictResolution::THROW)
   {}
   
   WMaxConflicts::WMaxConflicts (const unsigned int m, const WConflictResolution res) :
      m_max(m),
      m_resolution (res)
   {}

   WMaxRetries::WMaxRetries ():
      m_value (UNLIMITED)
   {}
   
   WMaxRetries::WMaxRetries (const unsigned int max):
      m_value (max)
   {}

   WMaxRetryWait::WMaxRetryWait ()
   {}
   
   WMaxRetryWait::WMaxRetryWait (const WTimeArg wait):
      m_value (wait)
   {}

   namespace
   {
      
      struct WReadLockTraits
      {
         using LockType = boost::shared_lock<boost::upgrade_mutex>;

         static void DoLock ()
         {
#ifdef _DEBUG            
            s_readMutexReadLocked = true;
#endif //_DEBUG
         }         

         static void DoUnlock ()
         {
#ifdef _DEBUG            
            s_readMutexReadLocked = false;
#endif //_DEBUG
         }
      };

      struct WUpgradeableLockTraits
      {
         using LockType = boost::upgrade_lock<boost::upgrade_mutex>;

         static void DoLock ()
         {
#ifdef _DEBUG            
            s_readMutexUpgradeLocked = true;
#endif //_DEBUG
         }         

         static void DoUnlock ()
         {
#ifdef _DEBUG            
            s_readMutexUpgradeLocked = false;
#endif //_DEBUG
         }
      };

      template <typename LockTraits_t>
      struct WLockImpl
      {
         explicit WLockImpl(bool doLock = true);
            
         //these methods must match the boost locking convention
         void lock();
         bool try_lock();
         bool locked() const {return m_lock.owns_lock ();}
         void unlock(const int i = 1);
         
         void UnlockAll ();
         bool WaitForCommit(const WTimeArg& timeout);
            
         int m_count;
         using Lock = typename LockTraits_t::LockType;
         Lock m_lock;

#ifdef _DEBUG
         std::thread::id m_threadId;
         bool m_writeLocked;
#endif //_DEBUG
      };

      template <typename LockTraits_t>
      WLockImpl<LockTraits_t>::WLockImpl (bool doLock):
         m_count(0),
         m_lock(s_readMutex, boost::defer_lock_t())
#ifdef _DEBUG
         ,m_threadId (std::this_thread::get_id ())
         ,m_writeLocked(false)
#endif //_DEBUG
      {
         if(doLock)
         {
            lock();
         }
      }

      template <typename LockTraits_t>
      void WLockImpl<LockTraits_t>::lock()
      {
         assert (m_threadId == std::this_thread::get_id ());
         if(m_count == 0)
         {
            if(m_lock.owns_lock())
            {
               throw boost::lock_error();
            }
            m_lock.lock();
            LockTraits_t::DoLock ();
         }
         ++m_count;
      }

      template <typename LockTraits_t>
      bool WLockImpl<LockTraits_t>::try_lock()
      {
         assert (m_threadId == std::this_thread::get_id ());
         if(m_count == 0)
         {
            if(m_lock.owns_lock())
            {
               throw boost::lock_error();
            }
            if(!m_lock.try_lock())
            {
               return false;
            }
            LockTraits_t::DoLock ();
         }
         ++m_count;
         return true;
      }

      template <typename LockTraits_t>
      void WLockImpl<LockTraits_t>::unlock(const int i)
      {
         assert (m_threadId == std::this_thread::get_id ());
         if (m_count == 0)
         {
            return;
         }

         if(!m_lock.owns_lock())
         {
            throw boost::lock_error();
         }

         assert(m_count >= i);
         m_count -= i;
         assert(m_count >= 0);
         if(m_count == 0)
         {
            m_lock.unlock();
            LockTraits_t::DoUnlock ();
         }
      }

      template <typename LockTraits_t>
      void WLockImpl<LockTraits_t>::UnlockAll ()
      {
         assert (m_threadId == std::this_thread::get_id ());
         unlock (m_count);
      }
      
      template <typename LockTraits_t>
      bool WLockImpl<LockTraits_t>::WaitForCommit(const WTimeArg& timeout)
      {
         assert (m_threadId == std::this_thread::get_id ());
         if(!m_lock.owns_lock())
         {
            throw boost::lock_error();
         }

         if(timeout.IsUnlimited ())
         {
            s_commitSignal.wait(m_lock);
            return true;
         }
         else
         {
            return (s_commitSignal.wait_until (m_lock, *timeout.m_time_o) != std::cv_status::timeout);
         }
      }

      using WReadLock = WLockImpl<WReadLockTraits>;
      using WUpgradeableLock = WLockImpl<WUpgradeableLockTraits>;
      
      class WWriteLock
      {
      public:
         WWriteLock(WUpgradeableLock& readLock);
         ~WWriteLock();
         
      private:
#ifdef _DEBUG
         WUpgradeableLock& m_readLock;
#endif //_DEBUG
         boost::upgrade_to_unique_lock<boost::upgrade_mutex> m_lock;
      };

      WWriteLock::WWriteLock(WUpgradeableLock& readLock):
#ifdef _DEBUG
         m_readLock (readLock),
#endif //_DEBUG
         m_lock (readLock.m_lock)
      {
#ifdef _DEBUG
         assert(!m_readLock.m_writeLocked);
         m_readLock.m_writeLocked = true;
         s_readMutexWriteLocked = true;
#endif //_DEBUG
      }
      
      WWriteLock::~WWriteLock()
      {
#ifdef _DEBUG
         assert(m_readLock.m_writeLocked);
         m_readLock.m_writeLocked = false;
         s_readMutexWriteLocked = false;
#endif //_DEBUG
      }

      struct WValueCoreBaseHash
      {
         size_t operator()(const std::shared_ptr<Internal::WVarCoreBase>& p) const
         {
            return m_hash (p.get ());
         }

         std::hash<Internal::WVarCoreBase*> m_hash;
      };

      using VarMap =  std::unordered_map<std::shared_ptr<Internal::WVarCoreBase>,
                                         std::shared_ptr<Internal::WValueBase>,
                                         WValueCoreBaseHash>;
   }
   
   namespace Internal
   {
      //Data used for each transaction
      struct WTransactionData
      {
         explicit WTransactionData (WUpgradeableLock& lock);

         WTransactionData* CreateChild ();

         //Don't use this directly, call CreateChild instead
         explicit WTransactionData (WTransactionData* parent);

         WTransactionData (const WTransactionData&) = delete;
         WTransactionData& operator=(const WTransactionData&) = delete;

         void Activate ();
         bool IsActive () const;
         
         WTransactionData* GetParent () const;
         WTransactionData* GetChild () const;

         int GetLevel () const;

         WReadLock& GetReadLock ();
         WUpgradeableLock& GetUpgradeLock ();

         VarMap& GetGot ();
         VarMap& GetSet ();

         Internal::WLocalValueBase* GetLocalValue (uint64_t key);
         void SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p);

         void AddBeforeCommit (WAtomic::WBeforeCommitFunc& after);
         using WBeforeCommitList = std::list<WAtomic::WBeforeCommitFunc> ;
         void GetBeforeCommits (WBeforeCommitList& beforeCommit);

         void AddAfter (WAtomic::WAfterFunc& after);
         using WAfterList = std::list<WAtomic::WAfterFunc>;
         void GetAfters (WAfterList& afters);

         void AddOnFail (WAtomic::WOnFailFunc& after);
         using WOnFailList = std::list<WAtomic::WOnFailFunc>;
         void RunOnFails ();
         
         void MergeToParent ();
         void MergeGetsToRoot ();
         void Clear ();
         void ClearWrites ();

#ifdef _DEBUG
         static void* const MARKER_VALUE;
         void* GetMarker () const;
#endif //_DEBUG
         
      private:         
#ifdef _DEBUG
         void* const m_marker;
#endif //_DEBUG
         
         bool m_active;

         //The transaction's level (1 = root transaction)
         int m_level;

         WTransactionData* m_parent_p;
         std::unique_ptr<WTransactionData> m_child_p;         

         //locks for this thread.
         WReadLock m_readLock;
         WUpgradeableLock& m_upgradeLock;
         
         //The WVar's that have been read.
         VarMap m_got;
         //The WVar's that have been set.
         VarMap m_set;
         
         //The "transaction local" values
         std::unordered_map<uint64_t, std::unique_ptr<Internal::WLocalValueBase>> m_locals;
         
         //list of functions to run just before the top-level
         //transaction commits.
         WBeforeCommitList m_beforeCommits;

         //list of functions to run after the top-level transaction
         //commits.
         WAfterList m_afters;

         //list of functions to run if transaction fails
         WOnFailList m_onFails;
      };

#ifdef _DEBUG
      void* const WTransactionData::MARKER_VALUE = (void*)0xdeadbeefdeadbeef;
#endif //_DEBUG

      WTransactionData::WTransactionData (WUpgradeableLock& lock):
#ifdef _DEBUG
         m_marker (MARKER_VALUE),
#endif //_DEBUG
         m_active (false),
         m_level (1),
         m_parent_p (nullptr),
         m_readLock (false),
         m_upgradeLock (lock)
      {}

      WTransactionData* WTransactionData::CreateChild ()
      {
         if (!m_child_p)
         {
            m_child_p = std::make_unique<WTransactionData>(this);
         }
         
         return m_child_p.get ();
      }

      WTransactionData::WTransactionData (WTransactionData* parent_p):
#ifdef _DEBUG
         m_marker (MARKER_VALUE),
#endif //_DEBUG
         m_active (false),
         m_level (parent_p->m_level + 1),
         m_parent_p (parent_p),
         m_readLock (false),
         m_upgradeLock (parent_p->m_upgradeLock)
      {}
      
      void WTransactionData::Activate ()
      {
         m_active = true;
      }
      
      bool WTransactionData::IsActive () const
      {
         return m_active;
      }
         
      WTransactionData* WTransactionData::GetParent () const
      {
         return m_parent_p;
      }
      
      WTransactionData* WTransactionData::GetChild () const
      {
         return m_child_p.get ();
      }

      int WTransactionData::GetLevel () const
      {
         return m_level;
      }

      WReadLock& WTransactionData::GetReadLock ()
      {
         assert (m_active);
         return m_readLock;
      }
      
      WUpgradeableLock& WTransactionData::GetUpgradeLock ()
      {
         assert (m_active);
         return m_upgradeLock;
      }

      VarMap& WTransactionData::GetGot ()
      {
         assert (m_active);
         return m_got;
      }
      
      VarMap& WTransactionData::GetSet ()
      {
         assert (m_active);
         return m_set;
      }      

      Internal::WLocalValueBase* WTransactionData::GetLocalValue (uint64_t key)
      {
         assert (m_active);
         
         WTransactionData* data_p = this;
         while (data_p)
         {
            const auto it = data_p->m_locals.find (key);
            if (it != data_p->m_locals.end ())
            {
               return it->second.get ();
            }
            data_p = data_p->m_parent_p;
         }
         
         return nullptr;         
      }
      
      void WTransactionData::SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p)
      {
         assert (m_active);
         m_locals[key] = std::move (value_p);
      }

      void WTransactionData::AddBeforeCommit (WAtomic::WBeforeCommitFunc& beforeCommit)
      {
         assert (m_active);
         m_beforeCommits.push_back (beforeCommit);
      }

      void WTransactionData::GetBeforeCommits (WBeforeCommitList& beforeCommits)
      {
         assert (m_active);
         beforeCommits.swap (m_beforeCommits);
      }

      void WTransactionData::AddAfter (WAtomic::WAfterFunc& after)
      {
         assert (m_active);
         m_afters.push_back (after);
      }

      void WTransactionData::GetAfters (WAfterList& afters)
      {
         assert (m_active);
         afters.swap (m_afters);
      }

      void WTransactionData::AddOnFail (WAtomic::WOnFailFunc& onFail)
      {
         assert (m_active);
         m_onFails.push_back (onFail);
      }

      void WTransactionData::RunOnFails ()
      {
         if (m_active)
         {
            for (const WAtomic::WOnFailFunc& func: m_onFails)
            {
               func ();
            }
         }
         m_onFails.clear ();
      }

      void WTransactionData::MergeToParent ()
      {
         assert (m_active);
         assert (m_parent_p);

         for (VarMap::value_type& value: m_got)
         {
            m_parent_p->m_got[std::get<0>(value)] = std::move (std::get<1>(value));
         }
         for (VarMap::value_type& value: m_set)
         {
            m_parent_p->m_set[std::get<0>(value)] = std::move (std::get<1>(value));
         }
         for (auto& val: m_locals)
         {
            m_parent_p->m_locals[val.first] = std::move (val.second);
         }

         m_parent_p->m_beforeCommits.splice (m_parent_p->m_beforeCommits.end (), m_beforeCommits);
         m_parent_p->m_afters.splice (m_parent_p->m_afters.end (), m_afters);
         m_parent_p->m_onFails.splice (m_parent_p->m_onFails.end (), m_onFails);
         
         Clear ();
      }

      void WTransactionData::MergeGetsToRoot ()
      {
         assert (m_active);
         assert (m_parent_p);

         WTransactionData* root_p = m_parent_p;
         while (root_p->m_parent_p)
         {
            root_p = root_p->m_parent_p;
         }
         
         for (VarMap::value_type& value: m_got)
         {
            m_parent_p->m_got[std::get<0>(value)] = std::move (std::get<1>(value));
         }

         Clear ();
      }
      
      void WTransactionData::Clear ()
      {
         if (!m_got.empty ())
         {
            m_got.clear ();
         }
         ClearWrites ();
         if (!m_onFails.empty ())
         {
            m_onFails.clear ();
         }
         m_active = false;
      }      

      void WTransactionData::ClearWrites()
      {
         if (!m_set.empty ())
         {
            m_set.clear ();
         }

         //For some reason we are getting random crashes when clearing
         //the after list when the list is empty so only clear it when it
         //is non-empty. 
         if (!m_beforeCommits.empty ())
         {
            m_beforeCommits.clear();
         }

         if (!m_afters.empty ())
         {
            m_afters.clear();
         }

         if (!m_locals.empty ())
         {
            m_locals.clear ();
         }

         if (!m_upgradeLock.locked ())
         {
            m_readLock.UnlockAll ();
         }
         else if (m_level == 1)
         {
            m_upgradeLock.UnlockAll ();
         }
      }

#ifdef _DEBUG
      void* WTransactionData::GetMarker () const
      {
         return m_marker;
      }
#endif //_DEBUG
   }
   

   namespace 
   {      
      //Stores the transaction data for each thread
      class WTransactionDataList
      {
      public:
         WTransactionDataList ();
         
         Internal::WTransactionData* Get ();
         Internal::WTransactionData* GetNew ();

         class WPushGuard
         {
         public:
            WPushGuard (std::unique_ptr<Internal::WTransactionData>&& root_p, Internal::WTransactionData* cur_p);
            WPushGuard (WPushGuard&& g);
            ~WPushGuard ();
            
         private:
            std::unique_ptr<Internal::WTransactionData> m_root_p;
            Internal::WTransactionData* m_cur_p;
         };
         friend WPushGuard;
         WPushGuard Push ();
         
         void MergeToParent ();
         void Abandon ();

         void CheckIntegrity () const;
         
      private:
         Internal::WTransactionData* GetNewNoActivate ();

         std::unique_ptr<Internal::WTransactionData> m_root_p;
         Internal::WTransactionData* m_cur_p;
         WUpgradeableLock m_lock;
      };

      THREAD_LOCAL (WTransactionDataList, s_transData_p);

      WTransactionDataList::WTransactionDataList ():
         m_cur_p (nullptr),
         m_lock (false)
      {}

      Internal::WTransactionData* WTransactionDataList::Get ()
      {
         if (m_cur_p)
         {
            return m_cur_p;
         }
         else
         {
            return GetNewNoActivate ();
         }
      }

      Internal::WTransactionData* WTransactionDataList::GetNew ()
      {
         Internal::WTransactionData* data_p = GetNewNoActivate ();
         data_p->Activate();
         return data_p;
      }

      Internal::WTransactionData* WTransactionDataList::GetNewNoActivate ()
      {
         CheckIntegrity ();
         if (!m_cur_p)
         {
            assert (!m_root_p);
            m_root_p = std::make_unique<Internal::WTransactionData>(m_lock);
            m_cur_p = m_root_p.get ();
         }
         else if (m_cur_p->IsActive ())
         {
            m_cur_p = m_cur_p->CreateChild ();
            assert (!m_cur_p->IsActive ());
         }
         //Note: if m_cur_p wasn't active above then we just use it
         CheckIntegrity ();
         
         return m_cur_p;
      }

      WTransactionDataList::WPushGuard::WPushGuard (std::unique_ptr<Internal::WTransactionData>&& root_p, Internal::WTransactionData* cur_p):
         m_root_p (std::move (root_p)),
         m_cur_p (cur_p)
      {}
      
      WTransactionDataList::WPushGuard::WPushGuard (WPushGuard&& g):
         m_root_p (std::move (g.m_root_p)),
         m_cur_p (g.m_cur_p)
      {}
      
      WTransactionDataList::WPushGuard::~WPushGuard ()
      {
         s_transData_p->m_root_p = std::move (m_root_p);
         s_transData_p->m_cur_p = m_cur_p;
      }

      WTransactionDataList::WPushGuard WTransactionDataList::Push ()
      {
         CheckIntegrity ();
         auto oldRoot_p = std::move (m_root_p);
         auto oldCur_p = m_cur_p;
         m_root_p.reset ();
         m_cur_p = nullptr;
         return WPushGuard (std::move (oldRoot_p), oldCur_p);
      }

      void WTransactionDataList::MergeToParent ()
      {
         CheckIntegrity ();
         assert (m_cur_p->IsActive ());
         assert (m_cur_p->GetParent ());
         if (m_cur_p->GetParent ())
         {
            m_cur_p->MergeToParent ();
            m_cur_p = m_cur_p->GetParent ();
            CheckIntegrity ();
         }
      }

      void WTransactionDataList::Abandon ()
      {
         assert (m_cur_p);
         if (!m_cur_p)
         {
            return;
         }
         m_cur_p->Clear ();
         
         if (m_cur_p->GetParent ())
         {
            m_cur_p = m_cur_p->GetParent ();
            CheckIntegrity ();
         }         
      }

//#define CHECK_TLS_INTEGRITY
#ifdef CHECK_TLS_INTEGRITY

      void WTransactionDataList::CheckIntegrity () const
      {
         if (m_cur_p)
         {            
            Internal::WTransactionData* cur_p = m_cur_p;
            Internal::WTransactionData* parent_p = cur_p->GetParent ();
            assert (cur_p->GetMarker () == Internal::WTransactionData::MARKER_VALUE);
            
            while (parent_p)
            {
               assert (parent_p->GetMarker () == Internal::WTransactionData::MARKER_VALUE);
               assert (parent_p->GetChild () == cur_p);
               assert (parent_p->GetLevel () == (cur_p->GetLevel () - 1));
               cur_p = parent_p;
               parent_p = cur_p->GetParent ();
            }

            cur_p = m_cur_p;
            Internal::WTransactionData* child_p = cur_p->GetChild ();
            while (child_p)
            {
               assert (child_p->GetMarker () == Internal::WTransactionData::MARKER_VALUE);
               assert (child_p->GetParent () == cur_p);
               assert (child_p->GetLevel () == (cur_p->GetLevel () + 1));
               cur_p = child_p;
               child_p = cur_p->GetChild ();
            }
         }
      }
      
#else
      void WTransactionDataList::CheckIntegrity () const
      {}
#endif //CHECK_TLS_INTEGRITY

   }

   WTimeArg::WTimeArg ()
   {}
   
   WTimeArg::WTimeArg (const time_point& t):
      m_time_o (t)
   {}

   WTimeArg WTimeArg::Unlimited ()
   {
      return WTimeArg ();
   }

   bool WTimeArg::IsUnlimited () const
   {
      return !m_time_o;
   }

   bool WTimeArg::operator<(const WTimeArg& t) const
   {
      if (IsUnlimited ())
      {
         //nothing is less than unlimited
         return false;
      }
      else if (!t.IsUnlimited ())
      {
         return (*m_time_o < *t.m_time_o);
      }
      else
      {
         //something limited is always less than an unlimited value
         return true;
      }
   }

   namespace Internal
   {
      
#ifdef _DEBUG

      bool ReadLocked()
      {
         return s_readMutexReadLocked;
      }
      
      bool UpgradeLocked()
      {
         return s_readMutexUpgradeLocked;
      }
      
      bool WriteLocked()
      {
         return s_readMutexWriteLocked;
      }      
#endif //_DEBUG

      WValueBase::WValueBase (const size_t version):
         m_version (version)
      {}

      WValueBase::~WValueBase ()
      {
      }

      WVarCoreBase::~WVarCoreBase ()
      {
      }

   }
   
   WAtomic::WAtomic ():
      m_data_p (s_transData_p->GetNew ()),
      m_committed (false)
   {
#ifdef _DEBUG
      Internal::WTransactionData* data_p = m_data_p;
      while (data_p)
      {
         assert (data_p->IsActive ());
         data_p = data_p->GetParent ();
      }
#endif //DEBUG
   }

   void WAtomic::Validate() const
   {
      boost::unique_lock<WReadLock> lock(m_data_p->GetReadLock (), boost::defer_lock_t ());
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         lock.lock ();
      }
      if(!DoValidation())
      {
         throw Internal::WFailedValidationException();
      }
   }

   bool WAtomic::DoValidation() const
   {
      assert(Internal::ReadLocked() || Internal::UpgradeLocked ());
      for (const VarMap::value_type& val: m_data_p->GetGot ())
      {
         if (!val.first->Validate (*val.second))
         {
            return false;
         }
      }

      return true;
   }

   void WAtomic::ReadLock()
   {
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetReadLock ().lock();
      }
   }

   bool WAtomic::IsReadLocked() const
   {
      return m_data_p->GetReadLock ().locked ();
   }

   void WAtomic::ReadUnlock()
   {
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetReadLock ().unlock();
      }
   }

   void WAtomic::BeforeCommit (WBeforeCommitFunc func)
   {
      m_data_p->AddBeforeCommit (func);
   }

   void WAtomic::After(WAfterFunc func)
   {
      m_data_p->AddAfter (func);
   }

   void WAtomic::OnFail (WOnFailFunc func)
   {
      m_data_p->AddOnFail (func);
   }

   void WAtomic::ClearWrites()
   {
      m_data_p->ClearWrites ();
   }

   void WAtomic::Abort ()
   {
      m_data_p->Clear ();
   }

   void WAtomic::Restart ()
   {
      //We need to push our transaction data aside here, if we don't
      //and a destructor triggered by the clearing of data below
      //starts a new transaction it will end up being a child of the
      //transaction that is being restarted. If this happens at best
      //the actions of the detstructor will never be comitted, at
      //worst memory corruption will result.
      WTransactionDataList::WPushGuard guard = s_transData_p->Push ();
      m_data_p->Clear ();
      m_data_p->Activate ();
   }

   void WAtomic::RunOnFails ()
   {
      //We need to push our transaction data aside here so that
      //transactions in the "on fail" handlers will run properly
      //(properly here means: no memory corruption)
      WTransactionDataList::WPushGuard guard = s_transData_p->Push ();
      m_data_p->RunOnFails ();
   }

   void WAtomic::CommitLock()
   {
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetUpgradeLock ().lock ();
         //unlock all the old read-locks here else we won't be able to
         //promote our upgrade lock later
         m_data_p->GetReadLock ().UnlockAll ();
      }
   }
   
   bool WAtomic::Commit()
   {
      assert (m_data_p->GetLevel () == 1);
      if(m_data_p->GetLevel () == 1)
      {
         Internal::WTransactionData::WBeforeCommitList beforeCommits;
         m_data_p->GetBeforeCommits (beforeCommits);         
         for (WAtomic::WBeforeCommitFunc& beforeCommit: beforeCommits)
         {
            beforeCommit (*this);
         }
         
#ifdef _DEBUG
         s_committing = true;
         struct WClearFlag
         {
            ~WClearFlag()
            {
               s_committing = false;
            }
         };
         WClearFlag clearFlag;
         (void)clearFlag; //avoid a compiler warning
#endif //_DEBUG
         
         std::list<std::shared_ptr<Internal::WValueBase>> dead;
         if (!m_data_p->GetSet ().empty ())
         {
            CommitLock ();
            
            if(!DoValidation())
            {
               m_data_p->GetUpgradeLock ().UnlockAll ();
               return false;
            }
            
            {   
               //scope introduced so that wlock goes away at end of block
               WWriteLock wlock(m_data_p->GetUpgradeLock ());
               for (const VarMap::value_type& val: m_data_p->GetSet ())
               {
                  //save old values until after we're done committing
                  //in case they run transactions in their destructors
                  dead.push_back (val.first->Commit (val.second));
               }
               s_commitSignal.notify_all();
            }

            m_data_p->GetUpgradeLock ().UnlockAll ();
            IncrementNumWriteCommits ();
         }
         else
         {
            if (m_data_p->GetUpgradeLock ().locked ())
            {
               if(!DoValidation())
               {
                  m_data_p->GetUpgradeLock ().UnlockAll ();
                  return false;
               }
               m_data_p->GetUpgradeLock ().UnlockAll ();
            }
            else
            {
               m_data_p->GetReadLock ().lock ();
               if(!DoValidation())
               {
                  m_data_p->GetReadLock ().UnlockAll ();
                  return false;
               }
               m_data_p->GetReadLock ().UnlockAll ();
            }
            IncrementNumReadCommits ();
         }

         //reset transaction data here so that after funcs will see no
         //transaction in progress         
         Internal::WTransactionData::WAfterList afters;
         m_data_p->GetAfters (afters);
         m_data_p->Clear ();
#ifdef _DEBUG
         s_committing = false;
#endif //_DEBUG
         m_committed = true;

         //The dead must be cleared before the afters run, else we coudl be holding on to things
         //that the afters are relying on being gone already (specifically
         //WChannelReader::Wdata::Release assumes that the after function that it attaches won't be
         //fighting with the transaction to release the channel nodes in order to avoid a stack overflow). 
         dead.clear ();
         
         for (WAtomic::WAfterFunc& after: afters)
         {
            after ();
         }

      }

      return true;
   }

   bool WAtomic::WaitForChanges(const WTimeArg& timeout)
   {
      if (m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetUpgradeLock ().unlock ();
      }

      WReadLockGuard<WAtomic> rlock(*this);
      if(!timeout.IsUnlimited ())
      {
         do
         {
            if(!DoValidation())
            {
               return true;
            }
            m_data_p->GetReadLock ().WaitForCommit(timeout);
         }while(std::chrono::steady_clock::now () < *timeout.m_time_o);
      }
      else
      {
         for (;;)
         {
            if(!DoValidation())
            {
               return true;
            }
            m_data_p->GetReadLock ().WaitForCommit(timeout);
         }
      }

      return false;
   }

   const Internal::WValueBase* WAtomic::GetVarValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p)
   {
      //Look in the values of this transaction and its parents
      Internal::WTransactionData* data_p = m_data_p;
      while (data_p)
      {
         //first try the set value
         VarMap::iterator it = data_p->GetSet ().find (core_p);
         if (it != data_p->GetSet ().end ())
         {
            return it->second.get ();
         }

         //next the got value
         it = data_p->GetGot ().find (core_p);
         if (it != data_p->GetGot ().end ())
         {
            return it->second.get ();
         }

         //try the parent transaction
         data_p = data_p->GetParent ();
      }

      //We haven't seen this var yet
      return nullptr;
   }

   const Internal::WValueBase* WAtomic::GetVarGotValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p)
   {
      //Look in the values of this transaction and its parents
      Internal::WTransactionData* data_p = m_data_p;
      while (data_p)
      {
         //next the got value
         auto it = data_p->GetGot ().find (core_p);
         if (it != data_p->GetGot ().end ())
         {
            return it->second.get ();
         }

         //try the parent transaction
         data_p = data_p->GetParent ();
      }

      //We haven't seen this var yet
      return nullptr;      
   }

   void WAtomic::SetVarGetValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p, std::shared_ptr<Internal::WValueBase>&& value_p)
   {
      m_data_p->GetGot ()[core_p] = std::move (value_p);
   }

   Internal::WValueBase* WAtomic::GetVarSetValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p)
   {
      //Note that we only check this transaction's set values not the
      //parent's. Values need to be set in the current transaction,
      //even if they have already been set in the parent. When this
      //transaction commits the set values will be merged into the
      //parent transaction.
      VarMap::iterator it = m_data_p->GetSet ().find (core_p);
      if (it != m_data_p->GetSet ().end ())
      {
         return it->second.get ();
      }
      else
      {
         return 0;
      }
   }
   
   void WAtomic::SetVarValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p, std::shared_ptr<Internal::WValueBase>&& value_p)
   {
      m_data_p->GetSet ()[core_p] = std::move (value_p);
   }

   Internal::WLocalValueBase* WAtomic::GetLocalValue (uint64_t key)
   {
      return m_data_p->GetLocalValue (key);
   }

   void WAtomic::SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p)
   {
      m_data_p->SetLocalValue (key, std::move (value_p));
   }
   
   WAtomic::~WAtomic()
   {
      if (!m_committed)
      {
         s_transData_p->Abandon ();
      }
   }

   namespace
   {
//#define TRACK_LAST_TRANS_CONFLICTS
#ifdef TRACK_LAST_TRANS_CONFLICTS

      THREAD_LOCAL_WITH_INIT_VALUE (unsigned int, s_lastTransConflicts, 0);

      struct WSetLastTransConflicts
      {
         const unsigned int& m_badCommits;

         WSetLastTransConflicts (const unsigned int& badCommits);

         ~WSetLastTransConflicts ();
      };

      WSetLastTransConflicts::WSetLastTransConflicts (const unsigned int& badCommits):
         m_badCommits (badCommits)
      {}

      WSetLastTransConflicts::~WSetLastTransConflicts ()
      {
         s_lastTransConflicts = m_badCommits;
      }
#endif //TRACK_LAST_TRANS_CONFLICTS

   }

   void WAtomic::AtomicallyImpl(Internal::WAtomicOp& op,
                                const WMaxConflicts& maxConflicts,
                                const WMaxRetries& maxRetries,
                                const WMaxRetryWait& maxRetryWait)
   {      
#ifdef _DEBUG
      //if this assertion fails we got a new transaction starting
      //while the current transaction is committing
      assert(!s_committing);
#endif //_DEBUG

      WAtomic at;
      assert (!at.m_committed);
      struct WRunOnFailHandlers
      {
         WAtomic& m_at;

         WRunOnFailHandlers (WAtomic& at): m_at (at) {}

         ~WRunOnFailHandlers ()
         {
            if (!m_at.m_committed)
            {
               m_at.RunOnFails ();
            }
         }
      };
      WRunOnFailHandlers runOnFailHandlers (at);
      if (at.m_data_p->GetLevel () > 1)
      {
         try
         {
            //this is a child transaction so just run the op and
            //merge to parent, exceptions and committing will be handled by the
            //root transaction
            op.Run (at);
            s_transData_p->MergeToParent ();
            at.m_committed = true;
            return;
         }
         catch(WRetryException&)
         {
            at.RunOnFails ();
            //we need to merge our gets into the root transaction so
            //that those vars get checked in the root transaction
            //retry handler
            at.m_data_p->MergeGetsToRoot ();
            throw;
         }
      }

      unsigned int badCommits = 0;
#ifdef TRACK_LAST_TRANS_CONFLICTS
      const size_t numConflictsLastTime = s_lastTransConflicts;
      (void)numConflictsLastTime;
      WSetLastTransConflicts setLastTransConflicts (badCommits);
#endif //TRACK_LAST_TRANS_CONFLICTS
      unsigned int retries = 0;
      for (;;)
      {
         if(maxConflicts.m_max != UNLIMITED && badCommits >= maxConflicts.m_max)
         {
            if(WConflictResolution::THROW == maxConflicts.m_resolution)
            {
               throw WMaxConflictsException(badCommits);
            }
            else
            {
               at.CommitLock();
            }
         }
         
         try
         {
            op.Run (at);

            //have to commit in this try block in case a "before action" does something that throws
            //an exception
            if(at.Commit())
            {
               break;
            }
         }
         catch(Internal::WFailedValidationException&)
         {
            ++badCommits;
            IncrementNumConflicts ();
            at.RunOnFails ();
            at.Restart();
            continue;
         }
         catch(WRetryException& exc)
         {
            ++retries;
            if(maxRetries.m_value != UNLIMITED && retries >= maxRetries.m_value)
            {
               throw WMaxRetriesException(retries);
            }
            at.RunOnFails ();

            const auto timeout = std::min (exc.m_timeout, maxRetryWait.m_value);
            if(!at.WaitForChanges(timeout))
            {
               throw WRetryTimeoutException();
            }
            at.Restart();
            continue;
         }
         catch (...)
         {
            at.RunOnFails ();
            at.Restart();
            throw;
         }

         at.RunOnFails ();
         at.Restart();
         ++badCommits;
         IncrementNumConflicts ();
      }
   }

   void WInconsistent::InconsistentlyImpl(Internal::WInconsistentOp& op)
   {
      WInconsistent ins;
      op.Run (ins);
   }

   WInconsistent::WInconsistent() :
      m_lock (s_readMutex, boost::defer_lock_t ()),
      m_lockCount (0)
   {}

   void WInconsistent::ReadLock()
   {
      if (m_lockCount == 0)
      {
         m_lock.lock();
      }
      ++m_lockCount;
   }
   
   bool WInconsistent::IsReadLocked() const
   {
      return m_lock.owns_lock();
   }
   
   void WInconsistent::ReadUnlock()
   {
      if(m_lockCount > 0)
      {
         --m_lockCount;
         if (m_lockCount == 0)
         {
            m_lock.unlock();
         }
      }
   }

   bool InAtomic()
   {
      return (s_transData_p->Get()->IsActive ());
   }

   WNoAtomic::WNoAtomic ()
   {
      if (InAtomic ())
      {
         throw WInAtomicError ();
      }
   }

   WInAtomicError::WInAtomicError ():
      WException ("Attempt to use function marked NO ATOMIC from within a transaction")
   {}
   
   void Retry(WAtomic&, const WTimeArg& timeout)
   {
      throw WRetryException(timeout);
   }

   bool WTransactionLocalFlag::TestAndSet (WAtomic& at)
   {
      const auto isSet = m_flag.Get (at) && *m_flag.Get (at);
      m_flag.Set (true, at);
      return isSet;
   }

}
