/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2016. All rights reserved.
****************************************************************************/

#include "conflict_profiling_processing.h"
using namespace  WSTM::ConflictProfiling;

#include <boost/optional.hpp>
#include <boost/range/algorithm/sort.hpp>
#include <boost/range/algorithm/set_algorithm.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <iostream>
#include <fstream>
#include <map>

using FileNameKey = const void*;

struct WTransactionKey
{
   FileNameKey m_file;
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

using NameKey = const void*;
using VarId = const void*;

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
   m_varNames[item.m_var_p] = item.m_nameKey;
}

void WProfileData::operator()(WConflict& item)
{
   boost::sort (item.m_got);
   m_conflicts[item.m_end] = std::move (item);

   const auto it = m_commitConflictRatios.find ({item.m_fileNameKey, item.m_line});
   if (it != std::end (m_commitConflictRatios))
   {
      ++(it->second.m_numConflicts);
   }
   else
   {
      m_commitConflictRatios[{item.m_fileNameKey, item.m_line}] = {0, 1};
   }
}

void WProfileData::operator()(WCommit& item)
{
   boost::sort (item.m_set);
   m_commits[item.m_end] = std::move (item);

   const auto it = m_commitConflictRatios.find ({item.m_fileNameKey, item.m_line});
   if (it != std::end (m_commitConflictRatios))
   {
      ++(it->second.m_numCommits);
   }
   else
   {
      m_commitConflictRatios[{item.m_fileNameKey, item.m_line}] = {1, 0};
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

std::map <WTransactionKey, WTransactionConflicts> ProcessConflicts (const WProfileData& profData)
{
   std::map <WTransactionKey, WTransactionConflicts> transactionConflicts;
   auto conflictVars = std::vector<VarId>();
   auto conflictVarsUnion = std::vector<NameKey>();
   const auto VarIdToNameKey = [&](VarId id) {return GetWithDefault (profData.m_varNames, id, nullptr);};
   for (const auto& conflict: profData.m_conflicts)
   {
      const auto conflictIt = GetOrInsert (transactionConflicts,
                                           {conflict.second.m_fileNameKey, conflict.second.m_line},
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
                                                 {commitIt->second.m_fileNameKey, commitIt->second.m_line},
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

   const auto transactionConflicts = ProcessConflicts (profData);
   
   return boost::none;
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
