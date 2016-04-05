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

#include "conflict_profiling_processing.h"
using namespace  WSTM::ConflictProfiling;
#include "maybe_unused.h"

#include <boost/optional.hpp>
#include <boost/range/algorithm/sort.hpp>
#include <boost/range/algorithm/set_algorithm.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <sqlite3.h>

#include <iostream>
#include <fstream>
#include <map>
#include <functional>

class WProgressReport
{
public:
   using DisplayStepFunc = std::function<void ()>;
   
   WProgressReport (const size_t numSteps, const size_t numDisplaySteps, DisplayStepFunc func);

   WProgressReport& operator++();
   
private:
   DisplayStepFunc m_displayFunc;
   size_t m_numSteps;
   size_t m_numDisplaySteps;
   size_t m_curAcc;
};

WProgressReport::WProgressReport (const size_t numSteps, const size_t numDisplaySteps, DisplayStepFunc func):
   m_displayFunc (std::move (func)),
   m_numSteps (numSteps),
   m_numDisplaySteps (numDisplaySteps),
   m_curAcc (0)
{
   assert (m_displayFunc);
}

WProgressReport& WProgressReport::operator++()
{
   m_curAcc += m_numDisplaySteps;
   while (m_curAcc >= m_numSteps)
   {
      m_displayFunc ();
      m_curAcc -= m_numSteps;
   }
   return *this;
}

auto DisplayPercentageDone (const unsigned int dotInterval, const unsigned int valueInterval)
{
   //things will look funny if these assertions fail
   assert ((valueInterval % dotInterval) == 0);
   assert (valueInterval > dotInterval);
   
   std::cout << "0%";
   auto dotCounter = 0u;
   auto valueCounter = 0u;
   auto pctDone = 0u;
   return [pctDone, dotCounter, dotInterval, valueCounter, valueInterval] () mutable
   {
      ++pctDone;
      
      ++dotCounter;
      if (dotCounter == dotInterval)
      {
         std::cout << '.';
         dotCounter = 0;
      }

      ++valueCounter;
      if (valueCounter == valueInterval)
      {
         std::cout << pctDone << '%';
         valueCounter = 0;
      }
   };
}

class WSubProgressReportFactory
{
public:
   WSubProgressReportFactory (const size_t numSuperSteps, WProgressReport& superReport):
      m_numSuperSteps (numSuperSteps),
      m_superReportIncrement ([&superReport]() {++superReport;})
   {}

   WProgressReport operator()(const size_t numSubSteps) const
   {
      return WProgressReport (numSubSteps, m_numSuperSteps, m_superReportIncrement);
   }

private:
   size_t m_numSuperSteps;
   WProgressReport::DisplayStepFunc m_superReportIncrement;
};

struct WCommitConflictRatio
{
   unsigned int m_numCommits;
   unsigned int m_numConflicts;
};

struct WProfileData : public boost::static_visitor<>
{
   std::map<VarId, NameKey> m_varNames;
   std::map<TimePoint, WConflict> m_conflicts;
   std::map<TimePoint, WCommit> m_commits;
   std::map<NameKey, std::string> m_names;
   
   std::map<TransactionId, WCommitConflictRatio> m_commitConflictRatios;
   
   void operator()(WVarName& item);
   void operator()(WConflict& item);
   void operator()(WCommit& item);
   void operator()(WName& item);
};

void WProfileData::operator()(WVarName& item)
{
   m_varNames[item.m_var] = item.m_name;
}

void WProfileData::operator()(WConflict& item)
{
   boost::sort (item.m_got);
   m_conflicts[item.m_end] = std::move (item);

   const auto it = m_commitConflictRatios.find (item.m_id);
   if (it != std::end (m_commitConflictRatios))
   {
      ++(it->second.m_numConflicts);
   }
   else
   {
      m_commitConflictRatios[item.m_id] = {0, 1};
   }
}

void WProfileData::operator()(WCommit& item)
{
   boost::sort (item.m_set);
   m_commits[item.m_end] = std::move (item);

   const auto it = m_commitConflictRatios.find (item.m_id);
   if (it != std::end (m_commitConflictRatios))
   {
      ++(it->second.m_numCommits);
   }
   else
   {
      m_commitConflictRatios[item.m_id] = {1, 0};
   }
}
   
void WProfileData::operator()(WName& item)
{
   m_names[item.m_key] = std::move (item.m_name);
}

struct WConflictingTransaction
{
   unsigned int m_count;
   std::vector<VarId> m_conflictingVars;
   WConflictingTransaction ();
};

WConflictingTransaction::WConflictingTransaction ():
   m_count (0)
{}

struct WTransactionConflicts
{
   std::map<TransactionId, WConflictingTransaction> m_conflicts;
};

template <typename Map_t, typename Gen_t>
auto GetOrInsert (Map_t& map, const typename Map_t::key_type& key, Gen_t&& gen)
{
   auto it = map.find (key);
   if (it == std::end (map))
   {
      std::tie (it, std::ignore) = map.insert (std::make_pair (key, gen ()));
   }
   return it;
}

template <typename Map_t>
auto GetWithDefault (const Map_t& map, const typename Map_t::key_type& key, const typename Map_t::mapped_type& defaultValue)
{
   const auto it = map.find (key);
   if (it != std::end (map))
   {
      return it->second;
   }
   else
   {
      return defaultValue;
   }
}

using ProcessedConflicts = std::map <TransactionId, WTransactionConflicts>;

ProcessedConflicts ProcessConflicts (const WProfileData& profData, WSubProgressReportFactory&& progressFactory)
{
   ProcessedConflicts transactionConflicts;
   auto conflictVars = std::vector<VarId>();
   auto conflictVarsUnion = std::vector<NameKey>();
   auto progress = progressFactory (profData.m_conflicts.size ());
   for (const auto& conflict: profData.m_conflicts)
   {
      const auto conflictIt = GetOrInsert (transactionConflicts,
                                           conflict.second.m_id,
                                           []() {return WTransactionConflicts ();});
      auto commitIt = profData.m_commits.lower_bound (conflict.second.m_start);
      const auto endCommits = profData.m_commits.upper_bound (conflict.second.m_end);
      while (commitIt != endCommits)
      {
         conflictVars.clear ();
         boost::set_intersection (conflict.second.m_got, commitIt->second.m_set, std::back_inserter (conflictVars));
         if (!conflictVars.empty ())
         {
            const auto conTransIt = GetOrInsert (conflictIt->second.m_conflicts,
                                                 commitIt->second.m_id,
                                                 [](){return WConflictingTransaction ();});
            
            ++(conTransIt->second.m_count);

            conflictVarsUnion.clear ();
            boost::set_union (conTransIt->second.m_conflictingVars, conflictVars, std::back_inserter (conflictVarsUnion));
            conTransIt->second.m_conflictingVars = std::move (conflictVarsUnion);
         }
         
         ++commitIt;
      }
      ++progress;
   }
   return transactionConflicts;
}

void CleanUpVars (WProfileData& profData, ProcessedConflicts& processed, WSubProgressReportFactory&& progressFactory)
{
   //remove variables from got/set lists if they aren't named. Unnamed variables in the lists just
   //cause confusion as there's no way to connect them from on instance of a transaction to
   //another.

   const auto ToNameKey = [&profData](const VarId v)
      {
         const auto it = profData.m_varNames.find (v);
         if (it != std::end (profData.m_varNames))
         {
            return it->second;
         }
         else
         {
            return -1;
         }
      };
   const auto HasName = [](const VarId v) {return (v != -1);};

   const auto CleanNames = [&](std::vector<VarId>& vars)
      {
         auto newVars = std::vector<VarId>();
         newVars.reserve (vars.size ());
         boost::copy (vars
                      | boost::adaptors::transformed (ToNameKey)
                      | boost::adaptors::filtered (HasName),
                      std::back_inserter (newVars));
         vars = std::move (newVars);
      };
         
   auto progress = progressFactory (profData.m_conflicts.size () + profData.m_commits.size () + processed.size ());
   for (auto& conflict: profData.m_conflicts)
   {
      CleanNames (conflict.second.m_got);
      ++progress;
   }

   for (auto& commit: profData.m_commits)
   {
      CleanNames (commit.second.m_set);
      ++progress;
   }

   for (auto& proced: processed)
   {
      for (auto& conflict: proced.second.m_conflicts)
      {
         CleanNames (conflict.second.m_conflictingVars);
      }
      ++progress;
   }
}

struct WSqlError
{
   std::string m_errMsg;
};

struct WSqliteClose
{
   template <typename SqliteType_t>
   void operator()(SqliteType_t* obj_p) const
   {
      sqlite3_close (obj_p);
   }
};

struct WSqliteFree
{
   template <typename SqliteType_t>
   void operator()(SqliteType_t* obj_p) const
   {
      sqlite3_free (obj_p);
   }
};

struct WSqliteFinalize
{
   template <typename SqliteType_t>
   void operator()(SqliteType_t* obj_p) const
   {
      sqlite3_finalize (obj_p);
   }
};

using DbPtr = std::unique_ptr<sqlite3, WSqliteClose>;
using ErrMsgPtr = std::unique_ptr<char, WSqliteFree>;
using StmtPtr = std::unique_ptr<sqlite3_stmt, WSqliteFinalize>;

StmtPtr Prepare (const DbPtr& db_p, const char* sql)
{
   sqlite3_stmt* stmt_p = nullptr;
   if (sqlite3_prepare_v2 (db_p.get (), sql, -1, &stmt_p, nullptr) != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errmsg (db_p.get ())};
   }
   return StmtPtr (stmt_p);
}

void Reset (const StmtPtr& stmt_p)
{
   const auto errCode = sqlite3_reset (stmt_p.get ());
   if (errCode != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

void Bind (const StmtPtr& stmt_p, const int pos, const int value)
{
   const auto errCode = sqlite3_bind_int (stmt_p.get (), pos, value);
   if (errCode != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

void Bind (const StmtPtr& stmt_p, const int pos, const unsigned int value)
{
   const auto errCode = sqlite3_bind_int64 (stmt_p.get (), pos, static_cast<sqlite3_int64>(value));
   if (errCode != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

void Bind (const StmtPtr& stmt_p, const int pos, const std::string& str)
{
   const auto errCode = sqlite3_bind_text (stmt_p.get (), pos, str.c_str (), -1, SQLITE_STATIC);
   if (errCode != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

void Bind (const StmtPtr& stmt_p, const int pos, const double value)
{
   const auto errCode = sqlite3_bind_double (stmt_p.get (), pos, value);
   if (errCode != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

void Bind (const StmtPtr& stmt_p, const int pos, const TimePoint time)
{
   const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch ()).count ();
   const auto errCode = sqlite3_bind_int64 (stmt_p.get (), pos, ticks);
   if (errCode != SQLITE_OK)
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

void Step (const StmtPtr& stmt_p)
{
   const auto errCode = sqlite3_step (stmt_p.get ());
   if ((errCode != SQLITE_DONE) && (errCode != SQLITE_ROW))
   {
      throw WSqlError {sqlite3_errstr (errCode)};
   }
}

struct WTransactionInfo
{
   NameKey m_file;
   unsigned int m_line;
   NameKey m_name;
};

using TransactionMap = std::map<TransactionId, WTransactionInfo>;

void InsertTransactions (const DbPtr& db_p, const TransactionMap& transactions, WSubProgressReportFactory&& progressFactory)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO Transactions VALUES (?, ?, ?, ?);");
   auto progress = progressFactory (transactions.size ());
   for (const auto& trans: transactions)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, trans.first);
      Bind (stmt_p, 2, trans.second.m_file);
      Bind (stmt_p, 3, trans.second.m_line);
      Bind (stmt_p, 4, trans.second.m_name);
      Step (stmt_p);

      ++progress;
   }
}

void InsertRawConflicts (const DbPtr& db_p, const WProfileData& profData, TransactionMap& transactions, WSubProgressReportFactory&& progressFactory)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO RawConflicts VALUES (?, ?, ?, ?, ?);");
   const auto gotStmt_p = Prepare (db_p, "INSERT INTO RawConflictVars VALUES (?, ?);");
   auto progress = progressFactory (profData.m_conflicts.size ());
   for (const auto& val: profData.m_conflicts)
   {
      if (transactions.find (val.second.m_id) == std::end (transactions))
      {
         transactions[val.second.m_id] = {val.second.m_file, val.second.m_line, val.second.m_transaction};
      }

      Reset (stmt_p);
      Bind (stmt_p, 1, val.second.m_id);
      Bind (stmt_p, 2, val.second.m_threadId);
      Bind (stmt_p, 3, val.second.m_thread);
      Bind (stmt_p, 4, val.second.m_start);
      Bind (stmt_p, 5, val.second.m_end);
      Step (stmt_p);

      Reset (gotStmt_p);
      Bind (gotStmt_p, 1, val.second.m_id);
      for (const auto got_p: val.second.m_got)
      {
         Reset (gotStmt_p);
         Bind (gotStmt_p, 2, got_p);
         Step (gotStmt_p);
      }

      ++progress;
   }
}

void InsertRawCommits (const DbPtr& db_p, const WProfileData& profData, TransactionMap& transactions, WSubProgressReportFactory&& progressFactory)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO RawCommits VALUES (?, ?, ?, ?, ?);");
   const auto setStmt_p = Prepare (db_p, "INSERT INTO RawCommitVars VALUES (?, ?);");
   auto progress = progressFactory (profData.m_commits.size ());
   for (const auto& val: profData.m_commits)
   {
      if (transactions.find (val.second.m_id) == std::end (transactions))
      {
         transactions[val.second.m_id] = {val.second.m_file, val.second.m_line, val.second.m_transaction};
      }

      Reset (stmt_p);
      Bind (stmt_p, 1, val.second.m_id);
      Bind (stmt_p, 2, val.second.m_threadId);
      Bind (stmt_p, 3, val.second.m_thread);
      Bind (stmt_p, 4, val.second.m_start);
      Bind (stmt_p, 5, val.second.m_end);
      Step (stmt_p);

      Reset (setStmt_p);
      Bind (setStmt_p, 1, val.second.m_id);
      for (const auto set_p: val.second.m_set)
      {
         Reset (setStmt_p);
         Bind (setStmt_p, 2, set_p);
         Step (setStmt_p);
      }

      ++progress;
   }
}

void InsertNames (const DbPtr& db_p, const WProfileData& profData, WSubProgressReportFactory&& progressFactory)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO Names VALUES (?, ?);");
   auto progress = progressFactory (profData.m_names.size ());

   for (const auto& val: profData.m_names)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, val.first);
      Bind (stmt_p, 2, val.second);
      Step (stmt_p);

      ++progress;
   }
}

void InsertCommitConflictRatios (const DbPtr& db_p, const WProfileData& profData, WSubProgressReportFactory&& progressFactory)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO CommitConflictRatios_ VALUES (?, ?, ?, ?, ?);");
   auto progress = progressFactory (profData.m_commitConflictRatios.size ());
   for (const auto& val: profData.m_commitConflictRatios)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, val.first);
      const auto total = val.second.m_numCommits + val.second.m_numConflicts;
      Bind (stmt_p, 2, total);
      Bind (stmt_p, 3, val.second.m_numCommits);
      Bind (stmt_p, 4, val.second.m_numConflicts);
      const auto ratio = 100.0*val.second.m_numConflicts/total;
      Bind (stmt_p, 5, ratio);
      Step (stmt_p);

      ++progress;
   }   
}

void InsertConflictingTransactions (const DbPtr& db_p, const ProcessedConflicts& conflicts, WSubProgressReportFactory&& progressFactory)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO ConflictingTransactions_ VALUES (?, ?, ?, ?);");
   const auto varStmt_p = Prepare (db_p, "INSERT INTO ConflictingTransactionVars_ VALUES (?, ?);");
   auto conId = 0u;
   auto progress = progressFactory (conflicts.size ());
   for (const auto& conflictee: conflicts)
   {
      Reset (stmt_p);
      Bind (stmt_p, 2, conflictee.first);
      for (const auto& conflicter: conflictee.second.m_conflicts)
      {
         Reset (stmt_p);
         Bind (stmt_p, 1, conId);
         Bind (stmt_p, 3, conflicter.first);
         Bind (stmt_p, 4, conflicter.second.m_count);
         Step (stmt_p);

         Reset (varStmt_p);
         Bind (varStmt_p, 1, conId);
         for (const auto var: conflicter.second.m_conflictingVars)
         {
            Reset (varStmt_p);
            Bind (varStmt_p, 2, var);
            Step (varStmt_p);
         }
         
         ++conId;
      }
      
      ++progress;
   }

}

boost::optional<std::string> WriteResults (const char* filename, const WProfileData& profData, const ProcessedConflicts& conflicts)
{      
   auto outPath = boost::filesystem::path (filename);
   outPath.replace_extension (".sqlite3");
   if (boost::filesystem::exists (outPath))
   {
      auto i = 1;
      using boost::str;
      using boost::format;
      auto newPath = (outPath.parent_path ()/(str (format ("%1%-%2%") % outPath.stem ().string () % i))).replace_extension (".sqlite3");
      while (boost::filesystem::exists (newPath))
      {
         ++i;
         newPath = (outPath.parent_path ()/(str (format ("%1%-%2%") % outPath.stem ().string () % i))).replace_extension (".sqlite3");
      }
      outPath = newPath;
   }

   sqlite3* db_p = nullptr;
   auto errCode = sqlite3_open (outPath.string ().c_str (), &db_p);
   if (errCode != SQLITE_OK)
   {
      return std::string (sqlite3_errstr (errCode));
   }
   DbPtr dbPtr_p (db_p);

   char* errMsg_p = nullptr;
   ErrMsgPtr errMsgPtr (errMsg_p);
   WSTM::Internal::MaybeUnused (errMsgPtr);
   const auto RunSql = [&](const char* sql)
      {
         if (sqlite3_exec (db_p, sql, nullptr, nullptr, &errMsg_p) != SQLITE_OK)
         {
            throw WSqlError {errMsg_p};
         }   
      };
   const auto ctStr = std::string ("CREATE TABLE ");
   const auto CreateTable = [&](const char* tableInfo)
      {
         RunSql ((ctStr + tableInfo + ";").c_str());
      };
   const auto ciStr = std::string ("CREATE INDEX ");
   const auto CreateIndex = [&](const char* indexInfo)
      {
         RunSql ((ciStr + indexInfo + ";").c_str());
      };
   try
   {
      CreateTable ("Transactions (TxnId INTEGER PRIMARY KEY, File, Line, NameKey)");
      
      CreateTable ("RawConflicts (TxnId, ThreadId, ThreadNameKey, Start, End)");
      CreateIndex ("RawConflicts_TxnId ON RawConflicts (TxnId)");

      CreateTable ("RawConflictVars (TxnId, VarNameKey)");
      CreateIndex ("RawConflictVars_TxnId ON RawConflictVars (TxnId)");

      CreateTable ("RawCommits (TxnId, ThreadId, ThreadNameKey, Start, End)");
      CreateIndex ("RawCommits_TxnId ON RawCommits (TxnId)");

      CreateTable ("RawCommitVars (TxnId, VarNameKey)");
      CreateIndex ("RawCommitVars_TxnId ON RawCommitVars (TxnId)");

      CreateTable ("Names (NameKey, Name)");
      CreateIndex ("Names_NameKey ON Names (NameKey)");

      CreateTable ("CommitConflictRatios_ (TxnId, TotalAttempts, Commits, Conflicts, PercentConflicts)");
      CreateIndex ("CommitConflictRatios_TxnId ON CommitConflictRatios_ (TxnId)");
      RunSql ("CREATE VIEW CommitConflictRatios AS\n"
              "SELECT\n"
              "   CommitConflictRatios_.TxnId,\n"
              "   NT.Name AS TxnName,\n"
              "   NN.Name AS Filename,\n"
              "   Transactions.Line,\n"
              "   CommitConflictRatios_.TotalAttempts,\n"
              "   CommitConflictRatios_.Commits,\n"
              "   CommitConflictRatios_.Conflicts,\n"
              "   CommitConflictRatios_.PercentConflicts\n"
              "FROM\n"
              "   CommitConflictRatios_\n"
              "   JOIN Transactions ON CommitConflictRatios_.TxnId==Transactions.TxnId\n"
              "   LEFT JOIN Names AS NT ON Transactions.NameKey==NT.NameKey\n"
              "   LEFT JOIN Names AS NN ON Transactions.File==NN.NameKey;");

      CreateTable ("ConflictingTransactions_ (ConId INTEGER PRIMAY KEY, TxnId, ConTxnId, Count)");
      CreateIndex ("ConflictingTransactions_TxnId ON ConflictingTransactions_ (TxnId)");
      CreateIndex ("ConflictingTransactions_ConTxnId ON ConflictingTransactions_ (ConTxnId)");
      RunSql ("CREATE VIEW ConflictingTransactions AS\n"
              "SELECT\n"
              "   ConflictingTransactions_.ConId,\n"
              "   ConflictingTransactions_.TxnId,\n"
              "   NT.Name AS TxnName,\n"
              "   NF.Name AS File,\n"
              "   T.Line,\n"
              "   ConflictingTransactions_.ConTxnId,\n"
              "   NConT.Name AS ConTxnName,\n"
              "   NConF.Name AS ConFile,\n"
              "   TCon.Line AS ConLine,\n"
              "   ConflictingTransactions_.Count\n"
              "FROM\n"
              "   ConflictingTransactions_\n"
              "   JOIN Transactions AS T ON ConflictingTransactions_.TxnId==T.TxnId\n"
              "   LEFT JOIN Names AS NT ON T.NameKey==NT.NameKey\n"
              "   JOIN Names AS NF ON T.File==NF.NameKey\n"
              "   JOIN Transactions AS TCon ON ConflictingTransactions_.ConTxnId==TCon.TxnId\n"
              "   LEFT JOIN Names AS NConT ON TCon.NameKey==NConT.NameKey\n"
              "   JOIN Names AS NConF ON TCon.File==NConF.NameKey;");
      CreateTable ("ConflictingTransactionVars_ (ConId, VarNameKey)");
      CreateIndex ("ConflictingTransactionVars_ConId ON ConflictingTransactionVars_ (ConId)");
      RunSql ("CREATE VIEW ConflictingTransactionVars AS\n"
              "SELECT\n"
              "   ConflictingTransactionVars_.ConId,\n"
              "   Names.Name\n"
              "FROM\n"
              "   ConflictingTransactionVars_\n"
              "   LEFT JOIN Names ON ConflictingTransactionVars_.VarNameKey==Names.NameKey;");
      
      RunSql ("BEGIN TRANSACTION;");
      auto readProgress = WProgressReport (6000, 100, DisplayPercentageDone (5, 20));
      auto transactions = TransactionMap ();
      InsertRawConflicts (dbPtr_p, profData, transactions, WSubProgressReportFactory (1000, readProgress));
      InsertRawCommits (dbPtr_p, profData, transactions, WSubProgressReportFactory (1000, readProgress));
      InsertTransactions (dbPtr_p, transactions, WSubProgressReportFactory (1000, readProgress));
      InsertNames (dbPtr_p, profData, WSubProgressReportFactory (1000, readProgress));
      InsertCommitConflictRatios (dbPtr_p, profData, WSubProgressReportFactory (1000, readProgress));
      InsertConflictingTransactions (dbPtr_p, conflicts, WSubProgressReportFactory (1000, readProgress));
      RunSql ("COMMIT TRANSACTION;");
   }
   catch (WSqlError& err)
   {
      return err.m_errMsg;
   }
   
   WSTM::Internal::MaybeUnused (profData, conflicts);

   return boost::none;
}

boost::optional<std::string> ProcessFile (const char* filename)
{
   std::ifstream file (filename, std::ios::in | std::ios::binary);
   if (!file)
   {
      return std::string ("Couldn't open file");
   }

   WProfileData profData;
   try
   {
      WDataProcessor proc (file);
      std::cout << "Reading Data:    ";
      auto readProgress = WProgressReport (proc.GetNumItems (), 100, DisplayPercentageDone (5, 20));
      while (auto data_o = proc.NextDataItem ())
      {
         boost::apply_visitor (profData, *data_o);
         ++readProgress;
      }
      std::cout << std::endl;
   }
   catch(WReadError&)
   {
      return std::string ("Error reading from file");
   }
   std::cout << "Processing:      ";
   auto procProgress = WProgressReport (1000, 100, DisplayPercentageDone (5, 20));   
   auto transactionConflicts = ProcessConflicts (profData, WSubProgressReportFactory (900, procProgress));
   CleanUpVars (profData, transactionConflicts, WSubProgressReportFactory (100, procProgress));
   std::cout << std::endl;
   
   std::cout << "Writing Results: ";
   return WriteResults (filename, profData, transactionConflicts);
}

int main (const int argc, const char** argv)
{
   if (argc < 2)
   {
      std::cout << "Please specifiy at least one input file" << std::endl;
      return 1;
   }

   for (auto i = 1; i < argc; ++i)
   {
      std::cout << argv[i] << ": " << std::endl;
      if (const auto res_o = ProcessFile (argv[i]))
      {
         std::cout << std::endl;
         std::cout << "error: " << *res_o << std::endl;
      }
      std::cout << std::endl;
   }
   
   return 0;
}
