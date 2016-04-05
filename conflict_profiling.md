#Conflict Profiling

Wyatt-STM now has a *conflict profiling* system that can help you track down which transactions are conflicting with each other and which variables they are conflicting on. The conflict profiling system requires that you use a version of the library built with the `WSTM_CONFLICT_PROFILING` flag enabled and that you also define that flag in your own code so that it is visible to the wstm headers when they are included. In addition your project will also need to link with the `wstm_cp` library which is only built when `WSTM_CONFLICT_PROFILING` is defined.

## Building

First you need to build Wyatt-STM with conflict profiling turned on. This is done by adding `-DWSTM_CONFLICT_PROFILING=yes` to the cmake command line when configuring the build. You will also need to add `-DSQLITE_INCLUDE_DIR=<path to sqlite3.h> -DSQLITE_LIB_DIR=<path to the sqlite library>` to the cmake command line so that it can find sqlite. This will build both the wstm library with conflict profiling turned on, the `wstm_cp` library, and the `conflict_profiling_processor` program (see below for more information on that).

When building a project that uses Wyatt-STM you will need to define `WSTM_CONFLICT_PROFILING` before including `stm.h`. You will also need to link with the specially built version of the library above as well as with `wstm_cp`.

## Profiling

After building with conflict profiling enabled (see above) you just run your program like usual. When the program exits you will have a `wstm_<timestamp>.profile` file in the program's working directory (note that it must exit normally, if it crashes you will get no profiling output). This is the *raw profiling data* which needs to be processed by the `conflict_profiling_processor` program built above. The processor will output `wstm_<timestamp>.sqlite` containing the processed profiling data as a Sqlite database.

When building in conflict profiling mode your code is instrumented by `Atomically` which is converted to a macro so that it can capture the file and line from which it is called. This can be a problem if you somehow use a function pointer to `Atomically` since your program will no longer compile. It can also be an issue if you wrap all your `Atomically` calls in another function or function object as all your transaction will look the same to the profiling system and the output will be useless. So if you do either of these things you will need to fix your code before conflict profiling will be useful to you.

Note that only top-level transactions are profiled. The actions of child transactions are reported as part of the top-level parent-transaction.

### Terminology

Before continuing we should nail down some terminology. A *transaction* is defined as code that is contained within a call to `Atomically`. An *instance of a transaction* is the run-time execution of a transaction. For example, in

```
void f1()
{
   Atomically([](WAtomic& at) {/*transactional code*/});
}

void f2()
{
   Atomically([](WAtomic& at) {/*transactional code*/});
}
```

The calls to `Atomically` in `f1` and `f2` are each transactions. At run-time any call to `f1` would cause an instance of that transaction to run, while a call to `f2` would create an instance of that transaction. In the profiling data the transactions are identified by the filename and line number that `Atomically` is called from (transactions can be given more user friendly names, see below).

A *transaction attempt* is one attempt to complete an instance of a transaction. If an instance of a transaction commits on its first try then that counts as one attempt, but if it encounters a conflict then that instance enters its second attempt, and so on.

A *variable* is any declaration of a `WVar`, an *instance of a variable* is a run-time object that goes with the declaration. For example, in

```
WVar<int> globalVar;

void f()
{
   static WVar<int> staticVar;
   WVar<int> localVar;
}

struct S
{
   static WVar<int> staticMemberVar;
   WVar<int> memberVar;
};
```

`globalVar`, `staticVar`, `localVar`, `staticMemberVar`, and `memberVar` are all *variables*. At run-time there will only ever be one *instance* of `globalVar`, `staticVar`, and `staticMemberVar` while there will be one instance of `localVar` for each call of `f` and one instance of `memberVar` for each instance of `S`. Variables aren't shown in the profiling output unless they are named (see below).

### Naming

Both transactions and variables can be named. Since transactions can be identified by the file and line from which they started, the name is just a more user-friendly way to identify transactions. Data about variables will only be available in the profiling output for variables that are named, there is no way to figure out which instances belong to which variables without the variables being named.

Names must exist until the program exits (this is when profiling data is written to disk). This means that they must either be static or dynamically allocated and never deleted. The former case is preferable since the latter will cause a lot of bloat in the profiling data as a new string instance must be allocated for each instance of a transaction or variable named (this will also be a performance drag while running your program). Also, since the names are used to associate instances of variables with variables you don't want to assign two instances of the same variable different names. 

Transactions are named by calling `WSTM::ConflictProfiling::NameTransaction` during the transaction. When not in conflict profiling mode this function is a no-op. It also does nothing when called from a child transaction so it is safe to use in any transactional code, it will be ignored if that code happens to run in a child transaction.

Naming variables is accomplished by `WSTM::WVar::NameForConflictProfiling`. This needs to be called once per variable instance, but does not need to be called in a transaction. It too is a no-op when conflict profiling is not active.

Threads can also be named by calling `WSTM::ConflictProfiling::NameThread`. This is only useful if you're looking at the raw transaction data in the profiling output.

### Clearing Profile Data

While your program is runnign the profiling data can be cleared by calling `WSTM::ConflictProfiling::ClearProfileData`. This is useful if you only want to profile a portion of the program in which case you would call this function as you are about to enter the part of the program that you want to profile and then exit the program when the part you want to profile is done (there is no way to dump out the profiling data short of exiting the program cleanly). 

### Data Tables

The processed profiling data will consist of multiple tables in a Sqlite database. If you aren't familiar with Sqlite, or SQL in general, then *DB Browser* (http://sqlitebrowser.org/) can be used to view the profiling data without having to write sql queries (open the .sqlite file and view the *Browse Data* tab). The main tables of interest are `CommitConflictRatios`, `ConflictingTransactions`, and `ConflictingTransactionVars`. The other tables in the database contain the raw data used to build the tables of interest. 

#### CommitConflictRatios

This table contains one row for each transaction that holds data about how often that transaction encountered conflicts. This is a good starting place for tracking down transactions that are encountering a lot of contention. The columns in this table are

- `TxnId`: A numerical id assigned to the transaction by the profiling system.
- `TxnName`: The name assigned to the transaction by `WSTM::ConflictProfiling::NameTransaction`.
- `Filename` and `Line`: The source code file and line from which the transaction was started.
- `TotalAttempts`: The total number of attempts that were made over all instances of this transactions.
- `Commits`: The number of attempts that committed.
- `Conflicts`: The number of attempts that encountered conflicts.
- `PercentConflicts`: The percentage of attempts that encountered conflicts.

#### ConflictingTransactions

This table contains one row for each pair of conflicting transactions. Note that there can be cases where multiple transactions could be the cause of a conflict in another transaction. For example, say we have transaction 1 that reads variables a and b. Then, before transaction 1 can commit, transaction 2 writes to a and transaction 3 writes to b. Both transactions 2 and 3 commit before transaction 1 can. Now transaction 1 will have a conflict, and both transactions 2 and 3 will have caused it. So the ConflictingTransactions table will contain lines for the pair of transactions 1 and 2 and the pair of transactions 1 and 3. Note that it is possible (and quite likely) that a pair of transactions will appear in two rows with the *conflictee* and *conflicter* roles reversed. This table's columns are

- `ConId`: A numerical id assigned to this pair of transactions. It can be used in the `ConflictingTransactionVars` table to determine which variables the transactions conflicted on.
- `TxnId`, `TxnName`, `File`, `Line`: The id, name, file and line of the transaction that had the conflict.
- `ConTxnId`, `ConTxnName`, `ConFile`, `ConLine`: The id, name, file and line of the transaction that caused the conflict.
- `Count`: The number of times that this conflict happened.

#### ConflictingTransactionVars

This table holds data about which variables caused which conflicts. Note that variables will only appear in this table if they were named, unnamed variables are used to determine conflicts but are then thrown away since without a name it is impossible to associate a variable instance with the variable it is an instance of. The table's columns are

- `ConId`: The id of the conflict that this variable caused (see the `ConId` column in `ConflictingTransactions`).
- `Name`: The variable's name.

### Workflow

If you suspect contention between transactions to be causing performance issues then the first step is to do a preliminary profiling run. If the `CommitConflictRatios` table is showing a lot of conflicts for a transaction that is involved in the part of the code that is having performance issues then you'll probably want to dig deeper by naming the variables that the transaction accesses as well as those of the transactions that are causing the conflicts (you'll probably want to name the transactions as well). Then another profiling run will reveal which variables are causing your contention. At this point you can refactor your transactions to reduce contention on those variables. Repeat until you've reduced your contention issues to an acceptable level.