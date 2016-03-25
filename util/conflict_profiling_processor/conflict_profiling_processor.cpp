/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2016. All rights reserved.
****************************************************************************/

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

struct WTransactionKey
{
   NameKey m_file;
   unsigned int m_line;
};

bool operator<(const WTransactionKey& k1, const WTransactionKey& k2)
{
   if (k1.m_file < k2.m_file)
   {
      return true;
   }
   else if ((k1.m_file == k2.m_file) && (k1.m_line < k2.m_line))
   {
      return true;
   }
   else
   {
      return false;
   }
}

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
   
   std::map<WTransactionKey, WCommitConflictRatio> m_commitConflictRatios;
   
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

   const auto it = m_commitConflictRatios.find ({item.m_file, item.m_line});
   if (it != std::end (m_commitConflictRatios))
   {
      ++(it->second.m_numConflicts);
   }
   else
   {
      m_commitConflictRatios[{item.m_file, item.m_line}] = {0, 1};
   }
}

void WProfileData::operator()(WCommit& item)
{
   boost::sort (item.m_set);
   m_commits[item.m_end] = std::move (item);

   const auto it = m_commitConflictRatios.find ({item.m_file, item.m_line});
   if (it != std::end (m_commitConflictRatios))
   {
      ++(it->second.m_numCommits);
   }
   else
   {
      m_commitConflictRatios[{item.m_file, item.m_line}] = {1, 0};
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
   std::map<WTransactionKey, WConflictingTransaction> m_conflicts;
};

void CleanUpVars (WProfileData& profData)
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
   
   for (auto& conflict: profData.m_conflicts)
   {
      auto newGot = std::vector<VarId>();
      newGot.reserve (conflict.second.m_got.size ());
      boost::copy (conflict.second.m_got
                   | boost::adaptors::transformed (ToNameKey)
                   | boost::adaptors::filtered (HasName),
                   std::back_inserter (newGot));
      conflict.second.m_got = std::move (newGot);
   }

   for (auto& commit: profData.m_commits)
   {
      auto newSet = std::vector<VarId>();
      newSet.reserve (commit.second.m_set.size ());
      boost::copy (commit.second.m_set
                   | boost::adaptors::transformed (ToNameKey)
                   | boost::adaptors::filtered (HasName),
                   std::back_inserter (newSet));
      commit.second.m_set = std::move (newSet);
   }
}

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

using ProcessedConflicts = std::map <WTransactionKey, WTransactionConflicts>;

ProcessedConflicts ProcessConflicts (const WProfileData& profData)
{
   ProcessedConflicts transactionConflicts;
   auto conflictVars = std::vector<VarId>();
   auto conflictVarsUnion = std::vector<NameKey>();
   const auto VarIdToNameKey = [&](VarId id) {return GetWithDefault (profData.m_varNames, id, -1);};
   for (const auto& conflict: profData.m_conflicts)
   {
      const auto conflictIt = GetOrInsert (transactionConflicts,
                                           {conflict.second.m_file, conflict.second.m_line},
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
                                                 {commitIt->second.m_file, commitIt->second.m_line},
                                                 [](){return WConflictingTransaction ();});
            
            ++(conTransIt->second.m_count);

            conflictVarsUnion.clear ();
            boost::set_union (conTransIt->second.m_conflictingVars, conflictVars, std::back_inserter (conflictVarsUnion));
            conTransIt->second.m_conflictingVars = std::move (conflictVarsUnion);
         }
         
         ++commitIt;
      }
   }
   return transactionConflicts;
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

void InsertRawConflicts (const DbPtr& db_p, const WProfileData& profData)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO RawConflicts VALUES (?, ?, ?, ?, ?, ?, ?);");
   const auto gotStmt_p = Prepare (db_p, "INSERT INTO RawConflictVars VALUES (?, ?, ?);");
   for (const auto& val: profData.m_conflicts)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, val.second.m_file);
      Bind (stmt_p, 2, val.second.m_line);
      Bind (stmt_p, 3, val.second.m_transaction);
      Bind (stmt_p, 4, val.second.m_threadId);
      Bind (stmt_p, 5, val.second.m_thread);
      Bind (stmt_p, 6, val.second.m_start);
      Bind (stmt_p, 7, val.second.m_end);
      Step (stmt_p);

      Reset (gotStmt_p);
      Bind (gotStmt_p, 1, val.second.m_file);
      Bind (gotStmt_p, 2, val.second.m_line);
      for (const auto got_p: val.second.m_got)
      {
         Reset (gotStmt_p);
         Bind (gotStmt_p, 3, got_p);
         Step (gotStmt_p);
      }
   }
}

void InsertRawCommits (const DbPtr& db_p, const WProfileData& profData)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO RawCommits VALUES (?, ?, ?, ?, ?, ?, ?);");
   const auto setStmt_p = Prepare (db_p, "INSERT INTO RawCommitVars VALUES (?, ?, ?);");
   for (const auto& val: profData.m_commits)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, val.second.m_file);
      Bind (stmt_p, 2, val.second.m_line);
      Bind (stmt_p, 3, val.second.m_transaction);
      Bind (stmt_p, 4, val.second.m_threadId);
      Bind (stmt_p, 5, val.second.m_thread);
      Bind (stmt_p, 6, val.second.m_start);
      Bind (stmt_p, 7, val.second.m_end);
      Step (stmt_p);

      Reset (setStmt_p);
      Bind (setStmt_p, 1, val.second.m_file);
      Bind (setStmt_p, 2, val.second.m_line);
      for (const auto set_p: val.second.m_set)
      {
         Reset (setStmt_p);
         Bind (setStmt_p, 3, set_p);
         Step (setStmt_p);
      }
   }
}

void InsertNames (const DbPtr& db_p, const WProfileData& profData)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO Names VALUES (?, ?);");
   
   for (const auto& val: profData.m_names)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, val.first);
      Bind (stmt_p, 2, val.second);
      Step (stmt_p);
   }
}

void InsertCommitConflictRatios (const DbPtr& db_p, const WProfileData& profData)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO CommitConflictRatios_ VALUES (?, ?, ?, ?, ?, ?);");
   
   for (const auto& val: profData.m_commitConflictRatios)
   {
      Reset (stmt_p);
      Bind (stmt_p, 1, val.first.m_file);
      Bind (stmt_p, 2, val.first.m_line);
      const auto total = val.second.m_numCommits + val.second.m_numConflicts;
      Bind (stmt_p, 3, total);
      Bind (stmt_p, 4, val.second.m_numCommits);
      Bind (stmt_p, 5, val.second.m_numConflicts);
      const auto ratio = 100.0*val.second.m_numConflicts/total;
      Bind (stmt_p, 6, ratio);
      Step (stmt_p);
   }
   
}

void InsertConflictingTransactions (const DbPtr& db_p, const ProcessedConflicts& conflicts)
{
   const auto stmt_p = Prepare (db_p, "INSERT INTO ConflictingTransactions_ VALUES (?, ?, ?, ?, ?, ?);");
   const auto varStmt_p = Prepare (db_p, "INSERT INTO ConflictingTransactionVars_ VALUES (?, ?);");
   auto conId = unsigned int (0);
   for (const auto& conflictee: conflicts)
   {
      Reset (stmt_p);
      Bind (stmt_p, 2, conflictee.first.m_file);
      Bind (stmt_p, 3, conflictee.first.m_line);
      for (const auto& conflicter: conflictee.second.m_conflicts)
      {
         Reset (stmt_p);
         Bind (stmt_p, 1, conId);
         Bind (stmt_p, 4, conflicter.first.m_file);
         Bind (stmt_p, 5, conflicter.first.m_line);
         Bind (stmt_p, 6, conflicter.second.m_count);
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
      return sqlite3_errstr (errCode);
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
      CreateTable ("RawConflicts (File, Line, NameKey, ThreadId, ThreadNameKey, Start, End)");
      CreateIndex ("RawConflicts_File_Line ON RawConflicts (File, Line)");
      CreateTable ("RawConflictVars (File, Line, VarNameKey)");
      CreateIndex ("RawConflictVars_File_Line ON RawConflictVars (File, Line)");
      CreateTable ("RawCommits (File, Line, NameKey, ThreadId, ThreadNameKey, Start, End)");
      CreateIndex ("RawCommits_File_Line ON RawCommits (File, Line)");
      CreateTable ("RawCommitVars (File, Line, VarNameKey)");
      CreateIndex ("RawCommitVars_File_Line ON RawCommitVars (File, Line)");
      CreateTable ("Names (NameKey, Name)");
      CreateIndex ("Names_NameKey ON Names (NameKey)");
      CreateTable ("CommitConflictRatios_ (File, Line, TotalAttempts, Commits, Conflicts, PercentConflicts)");
      CreateIndex ("CommitConflictRatios_File_Line ON CommitConflictRatios_ (File, Line)");
      RunSql ("CREATE VIEW CommitConflictRatios AS\n"
              "SELECT\n"
              "   Names.Name AS Filename,\n"
              "   CommitConflictRatios_.Line,\n"
              "   CommitConflictRatios_.TotalAttempts,\n"
              "   CommitConflictRatios_.Commits,\n"
              "   CommitConflictRatios_.Conflicts,\n"
              "   CommitConflictRatios_.PercentConflicts\n"
              "FROM\n"
              "   CommitConflictRatios_\n"
              "   JOIN RawCommits ON RawCommits.File == CommitConflictRatios_.File AND RawCommits.Line == CommitConflictRatios_.Line\n"
              "   LEFT JOIN Names ON RawCommits.File==Names.NameKey;");
      CreateTable ("ConflictingTransactions_ (ConId, File, Line, ConFile, ConLine, Count)");
      CreateIndex ("ConflictingTransactions_File_Line ON ConflictingTransactions_ (File, Line)");
      RunSql ("CREATE VIEW ConflictingTransactions AS\n"
              "SELECT\n"
              "   ConflictingTransactions_.ConId,\n"
              "   N1.Name as File,\n"
              "   ConflictingTransactions_.Line,\n"
              "   N2.Name as ConFile,\n"
              "   ConflictingTransactions_.ConLine,\n"
              "   ConflictingTransactions_.Count\n"
              "FROM\n"
              "   ConflictingTransactions_\n"
              "   LEFT JOIN Names AS N1 ON ConflictingTransactions_.File==N1.NameKey\n"
              "   LEFT JOIN Names AS N2 ON ConflictingTransactions_.ConFile==N2.NameKey;");
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
      InsertRawConflicts (dbPtr_p, profData);
      InsertRawCommits (dbPtr_p, profData);
      InsertNames (dbPtr_p, profData);
      InsertCommitConflictRatios (dbPtr_p, profData);
      InsertConflictingTransactions (dbPtr_p, conflicts);
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
      return "Couldn't open file";
   }

   WProfileData profData;
   try
   {
      WDataProcessor proc (file);
      while (auto data_o = proc.NextDataItem ())
      {
         boost::apply_visitor (profData, *data_o);
      }
   }
   catch(WReadError&)
   {
      return "Error reading from file";
   }
   CleanUpVars (profData);
   const auto transactionConflicts = ProcessConflicts (profData);

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
      std::cout << "Processing " << argv[i] << ": ";
      if (const auto res_o = ProcessFile (argv[i]))
      {
         std::cout << "error" << std::endl;
         std::cout << *res_o << std::endl;
         std::cout << std::endl;
      }
      else
      {         
         std::cout << "done" << std::endl;
      }
   }
   
   return 0;
}
