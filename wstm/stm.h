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

#include "exports.h"
#include "find_arg.h"
#include "exception.h"

#ifdef WIN32
//There is a bug in boost::shared_mutex on windows (https://svn.boost.org/trac/boost/ticket/7720),
//this fixes it but has worse performance than the windows specific implementation. When the issue
//is fixed (boost 1.60?) we can remove this
#define BOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN
#endif //WIN32

#include <boost/variant.hpp>
#include <boost/format.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <chrono>

/**
 * @file stm.h
 * The core of the transactional memory system.
 */

/**
 * The main namespace of the transactional memory system.
 */
namespace WSTM
{
   /**
    * @defgroup Core Core
    *
    * The core of the transactional memory system.
    */
   ///@{

   /**
    * @defgroup versioning Library Versioning
    *
    * Getting the library version.
    */
   ///@{
   /**
    * Contains the library version.
    *
    * @see GetVersion
    */
   struct WLibraryVersion
   {
      unsigned int m_major;
      unsigned int m_minor;
      unsigned int m_patch;
   };

   /**
    * Gets the library version.
    */
   WLibraryVersion GetVersion ();
   ///@}

   /**
    * @defgroup profiling Profiling
    *
    * Profiling use of the library. These functions allow you to track how many conflicts your
    * getting in your transactions. In order to use any of them you must compile the library with
    * STM_PROFILING defined. It is not suggested that you leave profiling on in general as it is a
    * performance drag.
    */
   ///@{
   /**
    * Starts a profiling run. Note that STM_PROFILING must be defined
    * in stm.cpp for this to do anything.
    */
   void WSTM_LIBAPI StartProfiling ();

   /**
    * Data from a STM profile run. Pass these objects to Checkpoint.
    *
    * @see Checkpoint
    */
   struct WSTM_CLASSAPI WProfileData
   {
      //!The start time of the profile run.
      std::chrono::high_resolution_clock::time_point m_start;
      //!The end time of the profile run.
      std::chrono::high_resolution_clock::time_point m_end;
      
      //!The number of conflicts during the run.
      long m_numConflicts;
      //!The number of read-only commits during the run.
      long m_numReadCommits;
      //!The number of commits with writes during the run.
      long m_numWriteCommits;

      //!Formats the data for output.
      std::string FormatData () const;
   };
   
   /**
    * Ends a profiling run. Note that STM_PROFILING must be defined in stm.cpp for this to do
    * anything. StartProfiling must have been called before this is called.
    *  
    * @return The profile data for this run.
    */
   WProfileData WSTM_LIBAPI Checkpoint ();
   ///@}
   
   /**
    * Constant that denotes an unlimited number of tries.
    */
   WSTM_LIBAPI extern const unsigned int UNLIMITED;

   namespace Internal
   {
      template <typename Trans_t>
      struct WStmOp
      {
         virtual void Run (Trans_t& t) = 0;
      };
   }
   
   class WAtomic;
   class WInconsistent;
   namespace Internal
   {
      using WAtomicOp = Internal::WStmOp<WAtomic>;
      using WInconsistentOp = Internal::WStmOp<WInconsistent>;
   }
	
   /**
    * Enumeration that tells Atomically how to react when it reaches it's conflict limit.
    *
    * @see WMaxConflicts
    */
   enum class WConflictResolution
   {
      /**
       * A WMaxConflictsException will be thrown.
       */
      THROW,
      /**
       * The operation will be run with all other writes locked out thus guaranteeing that the
       * operation can complete successfully
       */
      RUN_LOCKED	
   };

   /**
    * A time value that can be initialized using either a std::chrono::time_point or a
    * std::chrono::duration. If a duration is passed then the time is initialized to be that
    * duration from now. WTimeArg objects can also be *unlimited*, i.e. they represent a time
    * infinitely far in the future. All other entities in the library that require a time take them
    * as this type so that we don't have to have separate methods for durations and explicit times.
    */
   struct WTimeArg
   {
      /**
       * This is the time_point definition.
       */
      using time_point = std::chrono::steady_clock::time_point;
      
      /**
       * Constructs an *unlimited* WTimeArg object.
       */
      WTimeArg ();

      /**
       * Constructs a WTimeArg object with the given time_point.
       *
       * @param t The time_point to use.
       */
      WTimeArg (const time_point& t);
      
      /**
       * Constructs a WTimeArg object that is the given duration from now.
       *
       * @param d The duration to use.
       */
      template <typename Rep_t, typename Period_t>
      WTimeArg (const std::chrono::duration<Rep_t, Period_t>& d):
         m_time_o (std::chrono::steady_clock::now () + std::chrono::duration_cast<std::chrono::steady_clock::duration>(d))
      {}

      /**
       * Constructs an *unlimited* object.
       */
      static WTimeArg Unlimited ();
      
      /**
       * Checks whether this object is *unlimited* or not.
       */
      bool IsUnlimited () const;

      /**
       * Puts an ordering on WTimeArg objects. The ordering will be done on their time_points with
       * *unlimited* objects being considered to have infinite time_point. This means that if this
       * object is *unlimited* then operator< will always return false since infinity can't be less
       * than whatever is on the right side of the < operator.
       */
      bool operator<(const WTimeArg& t) const;
      
      /**
       * The time_point, its value will be boost::none if this WTimeArg object is *unlimited*.
       */
      boost::optional<std::chrono::steady_clock::time_point> m_time_o;
   };

   namespace Internal
   {
#ifdef _DEBUG
      bool WSTM_LIBAPI ReadLocked();
      bool WSTM_LIBAPI UpgradeLocked();
      bool WSTM_LIBAPI WriteLocked();
#endif //_DEBUG

      struct WTransactionData;

      //Thrown by WVarCoreBase::Validate when validation fails
      struct WSTM_CLASSAPI WFailedValidationException
      {};

      struct WSTM_CLASSAPI WValueBase
      {
         size_t m_version;

         WValueBase (const size_t version);
         ~WValueBase ();
      };

      template <typename Type_t>
      struct WValue : public WValueBase
      {
         Type_t m_value;

         WValue (const size_t version, const Type_t& value);
      };

      template <typename Type_t>
      WValue<Type_t>::WValue (const size_t version, const Type_t& value):
         WValueBase (version),
         m_value (value)
      {}

      struct WSTM_CLASSAPI WVarCoreBase
      {
         virtual bool Validate (const WValueBase& val) const = 0;
         virtual std::shared_ptr<WValueBase> Commit (const std::shared_ptr<WValueBase>& val_p) = 0;
         virtual ~WVarCoreBase ();
      };

      template <typename Type_t>
      struct WVarCore : public WVarCoreBase
      {
         virtual bool Validate (const WValueBase& val) const override
         {
            //have to define here, else we get C4505 from VC++
            return (val.m_version == m_value_p->m_version);
         }
         
         virtual std::shared_ptr<WValueBase> Commit (const std::shared_ptr<WValueBase>& val_p) override
         {
            //have to define here, else we get C4505 from VC++
            std::shared_ptr<WValueBase> old_p = m_value_p;
#ifdef _DEBUG
            const auto oldPtr_p = static_cast<const WValue<Type_t>*>(old_p.get ());
            (void) oldPtr_p;
#endif
            m_value_p = std::static_pointer_cast<WValue<Type_t> >(val_p);
            return old_p;
         }
         
         explicit WVarCore(std::shared_ptr<WValue<Type_t>>&& val_p);

         typename std::shared_ptr<WValue<Type_t>> m_value_p;
      };
         
      template<typename Type_t>
      WVarCore<Type_t>::WVarCore(std::shared_ptr<WValue<Type_t>>&& val_p):
         m_value_p (std::move (val_p))
      {}
      
      struct WSTM_CLASSAPI WLocalValueBase
      {
      public:
         virtual ~WLocalValueBase ();
      };

      uint64_t WSTM_LIBAPI GetTransactionLocalKey ();
   }

   /**
    * Read-lock manager for WAtomic and WInconsistent. When the lock is required just create one of
    * these objects and the lock will be released when this object goes out of scope.
    *
    * @param ReadLockable_t The type that will be read locked. Must have the methods readLock and
    * readUnlock.
    */
   template <typename ReadLockable_t>
   class WReadLockGuard
   {
   public:
      /**
       * Creates a guard and locks the given object.
       */
      WReadLockGuard(ReadLockable_t& lockable):
         m_lockable_p (&lockable)
      {
         lockable.ReadLock();
      }

      //@{
      /**
       * Moves the lock from the given lock guard to this one.
       *
       * @param g The guard to move the lock state from.
       *
       * @return This lock guard.
       */
      WReadLockGuard (WReadLockGuard&& g):
         m_lockable_p (g.m_lockable_p)
      {
         g.m_lockable_p = nullptr;
      }

      WReadLockGuard& operator=(WReadLockGuard&& g)
      {
         m_lockable_p = g.m_lockable_p;
         g.m_lockable_p = nullptr;
      }
      //@}
      
      /**
       * Destroys the lock guard, releasing the lock in the process.
       */
      ~WReadLockGuard()
      {
         Unlock ();
      }

      /**
       *  Unlocks the object that was passed to the constructor.
       */
      void Unlock ()
      {
         if (m_lockable_p)
         {
            m_lockable_p->ReadUnlock ();
            m_lockable_p = nullptr;
         }
      }
      
   private:
      ReadLockable_t* m_lockable_p;
   };

   /**
    * @defgroup atomically_options Options For Atomically
    *
    * These are the types that can be passed to Atomically as extra arguments in order to set
    * transaction options. The order that you pass these objects to Atomically doesn't matter, just
    * that they follow the function to be run in a transaction. If you pass multiple objects of the
    * same type only the first will be used, the rest will be ignored.
    */
   ///@{
   /**
    * Sets the the maximum number of times that the operation should be re-run due to WVar changes
    * by other threads before "conflict resolution" is used.
    *
    * @see Atomically, WConflictResolution
    */
   struct WSTM_CLASSAPI WMaxConflicts
   {
      /**
       * Creates an object that allows a unlimited number of conflicts.
       */
      WMaxConflicts ();
      
      /**
       * Creates an object with the given limit and resolution.
       *
       * @param limit The maximum number of conflicts allowed.
       * @param res The conflict resolution to use if the limit is hit.
       */
      WMaxConflicts(const unsigned int limit, const WConflictResolution res = WConflictResolution::THROW);

      //! The conflict limit.
      unsigned int m_max;
      //! The conflict resolution to use.
      WConflictResolution m_resolution;
   };

   /**
    * The maximum number of times that a transaction can call Retry. If this limit is hit a
    * WMaxRetriesException will be thrown.
    *
    * @see Atomically, Retry, WMaxRetriesException
    */
   struct WSTM_CLASSAPI WMaxRetries
   {
      /**
       * Creates an object that allows and unlimited number of retries.
       */
      WMaxRetries ();
      
      /**
       * Creates an object that allows the given number of retries.
       *
       * @param max The retry limit to use.
       */
      WMaxRetries (const unsigned int max);
      
      //! The retry limit.
      unsigned int m_value;
   };

   /**
    * The maximum amount of time to wait for a when Retry is called. If this
    * limit is hit a WRetryTimeoutException will be thrown. 
    *
    * @see Atomically, Retry, WRetryTimeoutException
    */
   struct WSTM_CLASSAPI WMaxRetryWait
   {
      /**
       * Creates an object that puts no time limit on Retry.
       */
      WMaxRetryWait ();
      
      /**
       * Creates an object that has the given time limit for Retry.
       *
       * @param max The retry time limit.
       */
      WMaxRetryWait (const WTimeArg wait);
      
      //! The retry time limit.
      WTimeArg m_value;
   };
   ///@}

   /**
    * Functions passed to Atomically must take a reference to one of these objects as their only
    * argument. The public interface allows one to do transaction validation and register functions
    * to be called when the transaction commits or fails.
    */
   class WSTM_CLASSAPI WAtomic
   {
      template <typename> friend class WVar;
      template <typename> friend class WTransactionLocalValue;

     public:
      /**
       * Checks the current transaction for memory consistency. If any of the WVar objects's that have
       * been read have been changed by another thread then the current transaction is aborted and
       * restarted. It is not a requirement to call this, it is done automatically when the
       * operation being run by Atomically completes. It should be called periodically by
       * long-running operations in order to avoid doing extra work when a transaction is already
       * invalid. It should also be called if you have read a set of variables that have an
       * associated invariant and need that invariant to be enforced. 
       */
      void Validate() const;
				
      /**
       * Causes the transaction to acquire a read lock. Normally during the course of reading a WVar
       * a read lock is acquired and released. This can be a performance problem when a lot of WVars
       * need to be read. In those cases this method should be called so that one read lock will be
       * held while doing the reads. The lock will be held until either readUnlock has been called
       * an equal number of times as readLock or the transaction ends. Normally WReadLockGuard
       * should be used instead of calling this directly.
       */
      void ReadLock();
      
      /**
       * Checks if a read lock is held or not.
       * 
       * @return true if the transaction is holding a read lock, false otherwise.
       */
      bool IsReadLocked() const;
      
      /**
       * Releases the read lock acquired by a call to readLock. This method must be called an equal
       * number of times as readLock has been called in order for the lock to actually be
       * released. Normally WReadLockGuard should be used instead of calling this directly.
       */
      void ReadUnlock();

      /**
       * Type of functions that can be passed to BeforeCommit.
       */
      using WBeforeCommitFunc = std::function<void (WAtomic&)>;

      /**
       * Adds a function to call just before the top-level transaction that is currently running
       * starts to commit.
       *
       * @param func The function to call, it will be passed this transaction. Be careful about what
       * you do in these functions. The function will not run until the TOP-LEVEL transaction
       * commits.  This may be much later than you expect.  Take steps to make sure that the data
       * you think will be around when the function runs will still be around by using shared_ptr or
       * something similar.
       */
      void BeforeCommit (WBeforeCommitFunc func);

      /**
       * Type of functions that can be passed to After.
       */
      using WAfterFunc = std::function<void (void)>;

      /**
       * Adds a function to call after the top-level transaction that is currently running commits
       * successfully.
       
       * @param func The function to call after the top-level transaction commits. Be careful about
       * what you do in these functions.  The function will not run until the TOP-LEVEL transaction
       * commits.  This may be much later than you expect.  Take steps to make sure that the data
       * you think will be around when the function runs will still be around by using shared_ptr or
       * something similar.
       */
      void After(WAfterFunc func);

      /**
       * Type of functions that can be passed to OnFail.
       */
      using WOnFailFunc = std::function<void (void)>;
      
      /**
       * Adds a function that will be called if this transaction fails to commit for some reason
       * (e.g. there is a conflict, an exception is thrown or the transacation is retried). This is
       * useful if you allocate resources that need to be cleaned up if the transaction fails to
       * commit.
       *
       * @param func The function to call.
       */
      void OnFail (WOnFailFunc func);
      
      /**
       * This method is used internally, just ignore it. You should be looking at Atomically
       * instead.
       */
      static void AtomicallyImpl(Internal::WAtomicOp& op,
                                 const WMaxConflicts& maxConflicts,
                                 const WMaxRetries& maxRetries,
                                 const WMaxRetryWait& maxRetryWait);
      //@}

      /**
       * Destroys the object.
       */
      ~WAtomic();
				
   private:		
      WAtomic();

      WAtomic (const WAtomic&);
      WAtomic& operator=(const WAtomic&);
      
      //does validation, expects a read lock to already be
      //in effect. Returns true if transaction is valid false
      //otherwise.
      bool DoValidation() const;
      //Starts a commit lock that will be in effect until
      //the transaction is committed or aborted.
      void CommitLock();
      //commits the transaction. Returns true if the
      //transaction committed successfully, false if there
      //is an invalid Var read at this level or throws
      //FailedValidationException if there is an invalid var
      //read at a higher level. 
      bool Commit();
      //Forgets any sets done in this transaction. Also
      //drops any after functions and releases the read
      //lock. This is called as part of abort but the OR
      //stuff needs to be able to clear out everything
      //except the gets.
      void ClearWrites();
      //aborts the transaction. 
      void Abort();
      //Clears the transaction and gets it ready to run again
      void Restart ();
      //Runs the "on fail" handlers.
      void RunOnFails ();
      //waits for one of the Vars read by this transaction
      //to change. 
      bool WaitForChanges(const WTimeArg& timeout);

      //Gets the value for the given WVar, this will be null if a
      //value has not been "gotten" or "set" for this WVar in this transaction.
      const Internal::WValueBase* GetVarValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p);
      //Gets the value that has been "gotten" for the given WVar, this will be null if a value has
      //not been "gotten" for the WVar in this transaction. 
      const Internal::WValueBase* GetVarGotValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p);
      //Sets the "gotten" value for the given WVar in this transaction.
      void  SetVarGetValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p, std::shared_ptr<Internal::WValueBase>&& value_p);
      //Gets the value that has been set for the WVar, or null if no
      //value has been set.
      Internal::WValueBase* GetVarSetValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p);
      //Sets the given WVar's value in the transaction. 
      void SetVarValue (const std::shared_ptr<Internal::WVarCoreBase>& core_p, std::shared_ptr<Internal::WValueBase>&& value_p);

      //Used by WTransactionLocalValue
      Internal::WLocalValueBase* GetLocalValue (uint64_t key);
      void SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p);
      
      Internal::WTransactionData* m_data_p;
      bool m_committed;
   };
   
   /**
    * Functions passed to Inconsistently must take a reference to one of these objects as their
    * only argument. Its only use is to read values from WVar object's.
    */
   class WSTM_CLASSAPI WInconsistent
   {
   public:
      /**
       * This is used internally, you want to look at Inconsistently instead.
       */
      static void InconsistentlyImpl(Internal::WInconsistentOp& op);

      /**
       * Causes the transaction to acquire a read lock. Normally during the course of reading a WVar
       * a read lock is acquired and released. This can be a performance problem when a lot of WVars
       * need to be read. In those cases this method should be called so that one read lock will be
       * held while doing the reads. The lock will be held until either readUnlock has been called
       * an equal number of times as readLock or the transaction ends. Normally WReadLockGuard
       * should be used instead of calling this directly.
       */
      void ReadLock();
      
      /**
       * Checks if a read lock is held or not.
       * 
       * @return true if the transaction is holding a read lock, false otherwise.
       */
      bool IsReadLocked() const;
      
      /**
       * Releases the read lock acquired by a call to readLock. This method must be called an equal
       * number of times as readLock has been called in order for the lock to actually be
       * released. Normally WReadLockGuard should be used instead of calling this directly.
       */
      void ReadUnlock();
				
   private:
      WInconsistent();

      WInconsistent (const WInconsistent&);
      WInconsistent& operator= (const WInconsistent&);

      boost::shared_lock<boost::upgrade_mutex> m_lock;
      size_t m_lockCount;
   };

   namespace Internal
   {
      template <typename Trans_t, typename Op_t>
      struct WStmOpVoid : public WStmOp<Trans_t>
      {
         WStmOpVoid (Op_t& op): m_op(op) {}
         
         virtual void Run (Trans_t& t) override
         {
            m_op(t);
         }

         Op_t& m_op;
      };

      template <typename Trans_t, typename Op_t>
      WStmOpVoid<Trans_t, Op_t> MakeVoidOp (Op_t& op)
      {
         return WStmOpVoid<Trans_t, Op_t>(op);
      }
      
      template <typename Trans_t, typename Op_t, typename Result_t, bool IsRef>
      struct WValOp;

      template <typename Trans_t, typename Op_t, typename Result_t>
      struct WValOp <Trans_t, Op_t, Result_t, true> : public WStmOp<Trans_t>
      {
         WValOp(Op_t& op): m_op(op) {}

         virtual void Run (Trans_t& t) override
         {
            m_res_p = &m_op(t);
         }

         Result_t GetResult()
         {
            return *m_res_p;
         }

         Op_t& m_op;
         using Res_t = typename std::remove_reference<typename std::remove_const<Result_t>::type>::type;
         Res_t* m_res_p;
  
      private:
         WValOp& operator= (const WValOp&) { return *this; }   // Silence warning 4512.
      };

      template <typename Trans_t, typename Op_t, typename Result_t>
      struct WValOp <Trans_t, Op_t, Result_t, false> : public WStmOp<Trans_t>
      {
         using Res_t = typename std::remove_const<Result_t>::type;
			
         WValOp(Op_t& op): m_op_p(&op) {}

         virtual void Run (Trans_t& t) override
         {
            m_res = (*m_op_p)(t);
         }

         Result_t GetResult()
         {
            return std::move (*m_res);
         }

         //Using an optional to store the result so that we don't have to require Result_t to have a
         //default constructor. We also move into the variant so that we can handle "move-only"
         //objects.
         using Result = boost::optional<Res_t>;
         
         Op_t* m_op_p;
         Result m_res;
      };

      template <typename Trans_t, typename Op_t>
      auto MakeValOp (Op_t& op)
      {
         using ResultType = decltype (op (std::declval<std::add_lvalue_reference_t<Trans_t>>()));
         return Internal::WValOp<Trans_t, Op_t, ResultType, std::is_reference<ResultType>::value> (op);
      }
      
   }
   
   /**
    * Base class for exceptions thrown by Atomically.
    */
   struct WSTM_CLASSAPI WCantContinueException : public WException
   {
      /**
       * Creates an object.
       *
       * @param msg The exeption's message.
       */
      WCantContinueException(const std::string& msg);
   };

   /**
    * Exception thrown when STM::atomically hits it's retry limit.
    */
   struct WSTM_CLASSAPI WMaxRetriesException : public WCantContinueException
   {
      /**
       * Creates an exception object.
       *
       * @param retries The number of retires that have been done.
       */
      WMaxRetriesException(unsigned int retries):
         WCantContinueException(
            boost::str(boost::format("Hit maximum number of retries (%1%)") % retries))
      {}
   };

   /**
    * Exception thrown when STM::atomically hits it's conflict limit.
    */
   struct WSTM_CLASSAPI WMaxConflictsException : public WCantContinueException
   {
      /**
       * Creates an exception object.
       *
       * @param retries The number of conflicts that have happened.
       */
      WMaxConflictsException(unsigned int conflicts):
         WCantContinueException(
            boost::str(boost::format("Hit maximum number of conflicts (%1%)") % conflicts))
      {}
   };

   /**
    * Exception thrown when a retry times out.
    */
   struct WSTM_CLASSAPI WRetryTimeoutException : public WCantContinueException
   {
      /**
       * Creates an exception object.
       */
      WRetryTimeoutException():
         WCantContinueException("Retry timed out")
      {}
   };

   //@{
   /**
    * Runs the given operation in an atomic fashion. This means that when the operation runs any
    * changes it makes to WVar object's are done in a transaction and will only be visible to other
    * threads if the operation completes without any of the WVar object's that it reads being
    * changed by another thread. If any of the WVar object's that it has read did change then the
    * operation will be started over. Note that this means that the operation must only carry out
    * idempotent operations as there is a chance that it will be run multiple times. Note that you
    * can nest calls to Atomically all you want but if nested calls are retried or have conflicts
    * then they will be abandoned and the operation for the first call to Atomically will be
    * retried. To abort the atomic operation just throw any exception and catch it outside of the
    * call to Atomically. The return value of Atomically will be the value returned by the operation
    * being run.
    *	
    * @param op The operation to carry out. This function must have signature "result f(WAtomic&)"
    * where result is either void or a copy constructable type.
    *
    * @param options Arguments that set various options, see \ref atomically_options "here" for
    * options that are recognized.
    *
    * @return The result of op.
   */
   template <typename Op_t, typename ... Options_t>
   auto Atomically (const Op_t& op, const Options_t&... options) -> 
      typename std::enable_if<std::is_same<void, decltype (op (std::declval<WAtomic&>()))>::value, void>::type
   {
      auto voidOp = Internal::MakeVoidOp<WAtomic> (op);
      WAtomic::AtomicallyImpl(voidOp, findArg<WMaxConflicts>(options...), findArg<WMaxRetries>(options...), findArg<WMaxRetryWait>(options...));
   }
                   
   template <typename Op_t, typename ... Options_t>
   auto Atomically (const Op_t& op, const Options_t&... options) -> 
      typename std::enable_if<!std::is_same<void, decltype (op (std::declval<WAtomic&>()))>::value, decltype (op (std::declval<WAtomic&>()))>::type
   {
      auto valOp = Internal::MakeValOp<WAtomic> (op);
      WAtomic::AtomicallyImpl(valOp, findArg<WMaxConflicts>(options...), findArg<WMaxRetries>(options...), findArg<WMaxRetryWait>(options...));
      return valOp.GetResult();
   }   
   //@}

   /**
    * Creates a function object that runs the given function in a transaction with the given
    * options. 
    *
    * @param op The operation to carry out. This function must have signature "result f(WAtomic&)"
    * where result is either void or a copy constructable type.
    *
    * @param options Arguments that set various options, see \ref atomically_options "here" for
    * options that are recognized.
    *
    * @return A function object that will run f in a transaction using the given options.
    */
   template<typename Func_t, typename ... Options_t>
   auto RunAtomically (const Func_t& f, const Options_t& ... options)
   {
      return [=]()
      {
         return Atomically (f, options...);
      };
   }
   
   /**
    * Returns true if the current thread is running under Atomically.
    */
   WSTM_LIBAPI bool InAtomic();

   /**
    * Exception thrown by WNoAtomic if it is constructed within a transaction.
    */
   struct WSTM_CLASSAPI WInAtomicError : public WException
   {
      /**
       * Creates an exception object.
       */
      WInAtomicError();
   };

   /**
    * Functions that cannot be called from within a transaction should take one of these as an
    * argument, with a default constructed object as the default value. When this class is
    * constructed it checks to see if a transaction exists and if it finds a transaction it throws
    * WInAtomicError. Generally this is used via the NO_ATOMIC macro.
    *
    * @see NO_ATOMIC
   */
   struct WSTM_CLASSAPI WNoAtomic
   {
      /**
       * Creates an object. WInAtomicError will be thrown if this is called from within a
       * transaction.
       */
      WNoAtomic();
   };

   /**
    * Use this as an argument in the declaration of functions that cannot be called from within a
    * transaction. If the function is called in a transaction an InAtomicError will be thrown.
    */
#define NO_ATOMIC const ::WSTM::WNoAtomic& = ::WSTM::WNoAtomic()
   /**
    * If NO_ATOMIC is used in a function's declaration this macro should be used for that argument
    * in the function's definition.
    */
#define NO_ATOMIC_IMPL const ::WSTM::WNoAtomic&
            
   //@{
   /**
    * Runs the given function in an "inconsistent" transaction. This transaction is not committable
    * and you cannot set WVar values when using this type of transaction. Also the value read from a
    * WVar is not saved in the transaction so multiple reads from the same WVar in one of these
    * transactions can yield different values. Note that reading the WVar values is still
    * thread-safe, just not guaranteed consistent if the same WVar is read multiple times. This
    * transaction also does not support Retry, and since it does not commit it will not be re-run if
    * read WVar's change. Call this function if you are already operating under an atomic
    * transaction will result in a run-time error. You can call Atomically from a function running
    * under an inconsistent transaction, the atomic transaction will commit when the call to
    * Atomically returns though. This type of transaction should only be used when a bunch of
    * variables need to be read and it doesn't matter that they can change while the reading is
    * going on.
    *			   
    * @param op The operation to run under an inconsistent transaction. Must have the signature type
    * (WInconsistent&) where "type" can be void or any other type.
    *
    * @return The return value of op.
    *		   
    * @throw WInAtomicError if this function is called from a function running under an atomic
    * transaction.
   */
   template <typename Op_t>
   auto Inconsistently(const Op_t& op, NO_ATOMIC) ->
      typename std::enable_if<std::is_same<void, decltype (op (std::declval<WInconsistent&>()))>::value, void>::type
   {
      auto voidOp = Internal::MakeVoidOp<WInconsistent>(op);
      WInconsistent::InconsistentlyImpl(voidOp);
   }

   template <typename Op_t>
   auto Inconsistently(const Op_t& op, NO_ATOMIC) ->
      typename std::enable_if<!std::is_same<void, decltype (op (std::declval<WInconsistent&>()))>::value, decltype (op (std::declval<WInconsistent&>()))>::type
   {
      auto valOp = Internal::MakeValOp<WInconsistent>(op);
      WInconsistent::InconsistentlyImpl(valOp);
      return valOp.GetResult();
   }
   //@}
			
   /**
    * If a function passed to Atomically determines that one of the WVar objects's that it has read
    * has a value that prevents the function from finishing then the function should call Retry.
    * When Retry is called the current transaction is abandoned and the thread is blocked until one
    * of the WVar object's that was read is changed by another thread.  When a change is detected
    * the blocked thread will be resumed and the atomic operation will be restarted.  Note that if
    * there have been nested atomically calls then the outermost call will be the one restarted.
    *			
    * @param wait An optional timeout value.  If the retry waits for this long then a
    * WRetryTimeoutException will be thrown out of the outermost atomically call. By default this
    * function waits forever.
   */
   WSTM_LIBAPI void Retry(WAtomic& at, const WTimeArg& timeout = WTimeArg::Unlimited ());

   /**
    * A transactional variable.  Access to the contents of the variable is restricted to functions
    * passed to Atomically, see the description of Atomically for details on what "transactional"
    * means.
    *
    * Note that WVar's are not copyable. This is intentional since the semantics of copying a WVar
    * are not clear. Copying could mean copying the current value, but it is better for this to be
    * done explicitly using Get/Set and the initializing constructor so that transaction use is
    * explicit. Copying could also mean: making two WVar's share the same internals. But this seems
    * dangerous and the same thing can be accomplished by using shared_ptr's to WVars. Using
    * shared_ptr's makes the WVar sharing much clearer as well.
    *			
    * @param Type_t The type stored. This type must be copyable and must not use Atomically in
    * its copy constructor or its destructor.
   */
   template <typename Type_t>
   class WVar
   {
   public:
      friend class WAtomic;
      
      //! The type stored in the variable.
      using Type = Type_t;
      //! The type used for passing objects of type Type_t.
      using param_type = typename boost::call_traits<Type>::param_type;
		
      /**
       * Default Constructor.  This can only be used if Type_t has a default constructor.
       */
      WVar():
         m_core_p(std::make_shared<Internal::WVarCore<Type_t>>(std::make_shared<Internal::WValue<Type_t>> (0, Type_t ())))
      {}

      /**
       * Constructor.
       *		
       *  @param val The initial value for the variable.
       */
      explicit WVar(param_type val):
         m_core_p(std::make_shared<Internal::WVarCore<Type_t>>(std::make_shared<Internal::WValue<Type_t>> (0, val)))
      {}

      //! No copying.
      WVar (const WVar&) = delete;
      //! No copying.
      WVar& operator= (const WVar&) = delete;

      //@{
      /**
       * Moves the internals of the given variable into this one. This transfers the value without
       * cpoying it.
       *
       * @param var The variable to move from.
       */
      WVar (WVar&& var):
         m_core_p (std::move (var.m_core_p))
      {}

      WVar& operator=(WVar&& var)
      {
         m_core_p = std::move (var.m_core_p);
         return *this;
      }
      //@}
      
      /**
       * Gets the variable's current value.
       *
       * @param at The transaction to use.
       *
       * @return The current value of the variable in this transaction. If this is the first time
       * that the variable is being read in the given transaction then this value will the last
       * commited value for this variable. On later reads the same value will be returned unless a
       * new value is set in the given transaction. This method will return a const reference if
       * Type_t is not a primitive type. This reference is only good during the given transaction,
       * once that transaction commits the reference could be dangling. So if you need the value
       * after the transaction is done you will need to copy it.
       */
      param_type Get(WAtomic& at) const
      {
         auto val_p = static_cast<const Internal::WValue<Type_t>*>(at.GetVarValue (m_core_p));
         if (!val_p)
         {
            WReadLockGuard<WAtomic> lock (at);
            auto value_p = m_core_p->m_value_p;
            lock.Unlock ();
            val_p = value_p.get ();
            at.SetVarGetValue (m_core_p, std::move (value_p));
         }
         return val_p->m_value;
      }

      /**
       * Gets the variable's current value. Can only be used within a call to Inconsistently.
       * Multiple calls to this for the same Var object can result in different values under the
       * same transaction.
       *
       * @param ins The WInconsistent object currently in use.
       *
       * @return The last committed value of the variable.
       */
      Type GetInconsistent(WInconsistent& ins) const
      {
         WReadLockGuard<WInconsistent> lock (ins);
         const std::shared_ptr<const Internal::WValue<Type_t>> val_p = m_core_p->m_value_p;
         lock.Unlock ();
         return val_p->m_value;
      }

      /**
       * Gets the variable's current value. This version can be used outside of a call to
       * Atomically in order to get the value in a read-only fashion. Note this function is
       * slower than calling Get() from within a call to Atomically.
       *
       * @return The value of the variable.
       */
      Type GetReadOnly() const
      {
         return Atomically ([&](WAtomic& at){return Get (at);});
      }

      /**
       * Sets the value of the variable. Can only be used within a call to Atomically. The value
       * will not become visible to other threads until the transaction commits.
       *
       * @param val The value to set, this value will be copied.
       * @param at The transaction to use.
      */
      void Set(param_type val, WAtomic& at)
      {
         //DO NOT TRY TO USE R-VALUE REFS AND MOVES.
         //Moves are unusable in this case due to transaction
         //restarts. If a movable object is passed into a transaction,
         //moved into a WVar and then the transaction gets restarted
         //the object that we need to move from is now in its
         //post-move state and unusable.
         auto val_p = static_cast<Internal::WValue<Type_t>*>(at.GetVarSetValue (m_core_p));
         if (!val_p)
         {
            WReadLockGuard<WAtomic> lock (at);
            const auto oldVersion = m_core_p->m_value_p->m_version;
            lock.Unlock ();            
            auto newVal_p = std::make_shared<Internal::WValue<Type_t>>(oldVersion + 1, val);
            at.SetVarValue (m_core_p, std::move (newVal_p));
         }
         else
         {
            val_p->m_value = val;
         }
      }

      /**
       * Sets the value of the variable. Creates a transaction to do this in so it will be slower
       * than the other version of set if you are already in a transaction.
       *
       * @param val The value to set, this value will be copied.
       */
      void Set(param_type val)
      {
         Atomically ([&](WAtomic& at){Set (val, at);});
      }

      /**
       * Validates just this variable, if validation fails the transaction will be restarted
       * immediately. This can be useful if you need to watch just the validity of a single variable
       * during a long transaction and a full validation would be prohibitively expensive. Note that
       * a full validation will be done at the end of the transaction whether this method is called
       * or not.
       *
       * @param The current transaction.
       */
      void Validate (WAtomic& at) const
      {
         const auto val_p = at.GetVarGotValue (m_core_p);
         if (val_p)
         {
            WReadLockGuard<WAtomic> lock (at);
            const auto valid = m_core_p->Validate (*val_p);
            lock.Unlock ();
            if (!valid)
            {
               throw Internal::WFailedValidationException();
            }
         }
      }
      
   private:
      typename std::shared_ptr<Internal::WVarCore<Type_t>> m_core_p;
   };

   /**
    * A variable that has values "local" to a given transaction, sort of like
    * a thread_local variable but for transactions instead of threads. The variable starts out
    * empty at the start of a transaction, in this state Get will return a null pointer. It stays
    * that way until Set is called, after which Get will return a pointer to the set value. Any
    * value that is set will only last until the transaction ends. Note that any value that is set
    * will be gone by time the "after" actions for the transaction run, but will still be available
    * during BeforeCommit actions. In child transactions the variable will contain the parent
    * transaction's value at the start and any value set will become the parent's value when the
    * child transaction commits. If the child transaction aborts then any values set in it will be
    * thrown away and the parent transaction will continue to see the same value as it saw before
    * the child transaction started.
    */
   template <typename Type_t>
   class WTransactionLocalValue
   {
   public:
      /**
       * Creates an object.
       */
      WTransactionLocalValue ():
         m_key (Internal::GetTransactionLocalKey ())
      {}

      /**
       * No copying allowed.
       */
      WTransactionLocalValue (const WTransactionLocalValue&) =delete;
      /**
       * No copying allowed.
       */
      WTransactionLocalValue& operator=(const WTransactionLocalValue&) =delete;
      
      //@{
      /**
       * Gets the current value of variable.
       *
       * @param at The current transaction.
       *
       * @return A pointer to the current value, will be null if no value has been set for this
       * variable in the current transaction or one of it's parents.
       */
      Type_t* Get (WAtomic& at)
      {
         if (auto value_p = at.GetLocalValue (m_key))
         {
            return &static_cast<WValue*>(value_p)->m_value;
         }
         else
         {
            return nullptr;
         }
      }

      const Type_t* Get (WAtomic& at) const
      {
         if (auto value_p = at.GetLocalValue (m_key))
         {
            return &static_cast<WValue*>(value_p)->m_value;
         }
         else
         {
            return nullptr;
         }
      }
      //@}

      //@{
      /**
       * Sets the value for the current transaction.
       *
       * @param value The value to set. This value will either be copied or moved depending on which
       * variant of set was called.
       *
       * @return A reference to the new value in the variable.
       */
      Type_t& Set (const Type_t& value, WAtomic& at)
      {
         auto value_p = std::unique_ptr<WValue>(new WValue (value));
         auto& result = value_p->m_value;
         at.SetLocalValue (m_key, std::move (value_p));
         return result;
      }
      
      Type_t& Set (Type_t&& value, WAtomic& at)
      {
         auto value_p = std::unique_ptr<WValue>(new WValue (std::move (value)));
         auto& result = value_p->m_value;
         at.SetLocalValue (m_key, std::move (value_p));
         return result;
      }
      //@}
      
   private:
      //We use an integer key here instead of the this pointer to avoid "reused memory" issues. Say
      //we create a WTransactionLocalValue in a transaction, set its value, destroy it, and then
      //create another WTransactionLocalValue. There is a chance that the second
      //WTransactionLocalValue will have the same address as the first and will thus pick up the
      //value for the first that is still in the transaction object's local value hash map. This
      //would be problematic if Type_t is the same between the two locals, and disastrous if they
      //are different. So instead we use an integer key that will be different for every
      //WTransactionLocalValue object that is created. The key is 64 bit so we don't have to worry
      //about it rolling over in any reasonable amount of time.
      uint64_t m_key;
      
      struct WValue : public Internal::WLocalValueBase
      {
         Type_t m_value;

         explicit WValue (const Type_t& value):
            m_value (value)
         {}

         explicit WValue (Type_t&& value):
            m_value (std::move (value))
         {}
      };

   };

   /**
    * A transaction local flag. This flag is only visible within this transaction and its parents
    * and children. This method is here for those cases where an operation needs to be done at most
    * once per transaction.
    */
   class WSTM_CLASSAPI WTransactionLocalFlag
   {
   public:
      /**
       * Sets the flag and returns the prior value of the flag.
       *
       * @param at The current transaction.
       *
       * @return True if the flag was already set, false otherwise.
       */
      bool TestAndSet (WAtomic& at);
      
   private:
      WTransactionLocalValue<bool> m_flag;
   };

   ///@}
}
