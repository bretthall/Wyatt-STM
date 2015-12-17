# Wyatt-STM

Wyatt-STM is a library based software transactional memory (STM) system. It was developed as part of the [Dynamics](http://www.wyatt.com/products/software/dynamics.html) data collection and analysis application for dynamic light scattering instruments built by [Wyatt Technology](http://www.wyatt.com). The system's interface is inspired by that used in [Haskell](http://haskell.org) described in this [paper](http://research.microsoft.com/Users/simonpj/papers/stm/stm.pdf). Obviously we are working in C++ (an imperative language) instead of Haskell (a pure functional language) so we have had to add some additional constructs to help in managing side-effects. There is also the need for a bit of programmer discipline in making sure that side-effects in transactional code are handled properly. In our experience the benefits of the transactional system far out-weigh any complication created by this.

This document concentrates on how to use the system. For more information on how well the system has worked for us at Wyatt Technology please see either this [paper](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4438.pdf) or this [talk](https://youtu.be/k20nWb9fHj0) from CppCon 2015. 

## Core System

The core of the STM system is defined in the `WSTM` namespace in file `wstm/stm.h`. 

### Transactional Variables

The backbone of the transactional system are objects of the `WVar` class. These objects contain values that are read within transactions (transactions are started using `Atomically` about which we'll have more to say below). While a transaction is running all reads from `WVar` objects are tracked and the value that was read is saved. Once a `WVar` has been read in a transaction it will always yield the same value in that transaction, even if another thread stores a new value in the `WVar` between reads. The only way this value can change during the transaction is if the `WVar`'s value is set within that transaction. So the value yielded by a `WVar` will either be the first value read from it during the transaction or the last value set in it during the transaction with the set values taking precedence. For example (I'll explain how you get a `WAtomic` object later):

```C++
WVar<int> x(0);

void DoStuff(WAtomic& at)
{
   x.Get(at); // yields 0
   x.Get(at); // yields 0 again, even if another thread has changed the value of x
   x.Set(10, at);
   x.Get(at); //yields 10 
   x.Get(at); //still yields 10, even if another thread modifies x
   x.Set(11, at); 
   x.Get(at); //yields 11 
   x.Get(at); //still yields 11, even if another thread modifies x
}
```

When a transaction ends the values of the `WVar`s that were read are checked against the current values of the variables. If any of the `WVar`s has been changed by another thread since it was first read in the transaction then that transaction is deemed invalid, its effects are thrown away, and the transaction starts over from the beginning. If all of the read values are still good then the transaction commits and the values set in the transaction become visible to other threads in an atomic fashion (i.e. if another thread can see one change it can see them all).

`WVar` also has a `GetReadOnly` method that allows its value to read outside of a transaction. Note that this is just convenience method and is the equivalent of doing `Atomically ([&](WAtomic& at){return x.Get (at);});`. If you are reading multiple variables that you need to be consistent then you should read them all using `Get` in a single transaction instead of making multiple calls to `GetReadOnly`. The former will not only have better performance (starting a transaction is not free) but will also ensure that the set of values you get are consistent, i.e. none of them changed while you were doing the reads.

The values stored in `WVar` must be copyable. In fact, the values stored are always copied into the `WVar` (moving values into `WVar` objects is problematic in the face of the transactions having conflicts and needing to be repeated). Also note that when getting a value from a `WVar` you either get a `const` reference or a copy depending on the type stored and whether you are in a transaction or not (`Get` can only be called in a transaction and can return a reference, `GetReadOnly` always returns a copy). If you get a reference instead of a copy be aware that that reference is only good during the transaction that `Get` was called in. If the value is to be used outside of the transaction it must be copied when leaving the transaction (this is what `GetReadOnly` does).

### Starting Transactions

In the above example the `DoStuff` function takes a reference to a `WAtomic` object which represents the transaction. If you have one of these objects then you are in a transaction, but how do you get one? The `Atomically` function does this for you, you cannot directly create a `WAtomic` object yourself. `Atomically` takes a function object argument, creates a `WAtomic` object and then calls the function object passing the `WAtomic` object that it created. `Atomically` will then return whatever value the function object returns. `Atomically` handles validating and committing the transaction when the function object is done running and will call the function object again if the transaction is invalid. Because they may be called more than once it is very important that function objects passed to `Atomically` have no side-effects other than setting `WVar`s -- unless the side-effects can be harmlessly repeated like log statements. See `WAtomic::After` below for how to schedule non-idempotent (i.e. *not safe to repeat*) side-effects to occur when your transaction commits. 

So to call `DoStuff` from the example above one would do:

```C++
Atomically([](WAtomic& at){DoStuff(at);});
```

There can be cases where you start a transaction and, after reading some `WVar` objects, decide that the transaction needs to be stopped. You can't simply return since then the STM system will attempt to validate and commit the transaction, exactly the thing that you are trying to avoid. To escape from the transaction without committing just throw an exception that is caught outside of the call to `Atomically`. When this is done the transaction is thrown away. For example:

```C++
WVar<int> x (0);
WVar<int> y (0);

try
{
   Atomically ([&](WAtomic& at)
               {
                  y.Set (10, at);
                  if (x.Get (at) < 10)
                  {
                     //if we get here then y won't be changed
                     throw SomeException ();
                  }
               });
}
catch (SomeException&)
{
}
//at this point y still contains 0
```

While STM systems eliminate the possibility of dead-lock they can suffer from *starvation*. This occurs when a transaction that takes a long time to run is repeatedly invalidated by transactions running on other threads. To combat this you can set a limit on the number of times that a transaction will be restarted due to invalidation. When this limit is hit you have two options: have a `WMaxConflictsException` thrown or you can *run locked*. If you opt for the latter then your transaction will be restarted, only this time a flag is set that prevents all other transactions from committing until yours does. This guarantees that your transaction will be able to commit but is a heavy-handed approach and can cause performance degradation for the rest of the program as other threads have to wait until your transaction commits.

`Atomically` can take extra arguments after the function object to execute. These arguments set various options for the transaction and this is how you set the restart limit mentioned above. Specifically, you pass a `WMaxConflicts` object to `Atomically`. The `WMaxConflicts` constructor allows you to set what to do when the limit is hit, either `WConflictResolution::THROW` or `WConflictResolution::RUN_LOCKED`. For example, if you wanted to limit the restarts to five and then run locked when the limit was hit you could do

```C++
auto Func = [](WAtomic& at){return x.Get (at) + 1;};
Atomically (Func, WMaxConflicts (5, WConflictResolution::RUN_LOCKED));
```

There are other options that can be passed to `Atomically`, we'll cover those later. Note that the order of the options doesn't matter. The only requirement is that the function to execute in a transaction is the first argument to Atomically.

### Nested Transactions

`Atomically` can be called from a function that is already running under another call to `Atomically`. This will create a new transaction and when this new transaction commits the contents of the transaction will be moved into the enclosing transaction. They will not become visible to other threads until the top-level transaction commits.

If the child transaction is aborted by throwing an exception and that exception is caught before it has propogated out of the parenttransaction then the parent transaction will simply continue from the point where the exception was caught. The changes from the child transaction are thrown away and the parent transaction behaves as though it had never started the child transaction. If the exception is not caught within the parent transaction then the parent transaction will be aborted as well.

If you think that a function will be called within a transaction then you should have it take a `WAtomic` argument, even if it isn't meant to be called directly. It is much cheaper to pass an existing `WAtomic` object to a function then to go through all the rigmarole of calling it through `Atomically`. Much of the time it is a good idea to create *atomic* and *non-atomic* versions of the function for convenience's sake:

```C++
void DoSomething() {Atomically([](){DoSomething(at);});}
void DoSomething(WAtomic& at)
{
   //Does something
}
```

### Inconsistently

If you aren't going to set the values of variables and don't care about seeing a consistent view of memory then `Inconsistently` can be used instead of `Atomically`. This creates an *inconsistent* transaction that does not track variable values. Each time a variable is read in an inconsistent transaction you get the current value so you can get different values on different reads of the same variable in the same transaction. There is also no validation done at the end of the function passed to `Inconsistently`. It is not possible to set values in `WVar` objects in a call to `Inconsistently` and calls to `Inconsistently` cannot be nested in calls to `Atomically` (doing so will result in a runtime error). One uses `Inconsistently` like so

```C++
WVar<int> x (0);
Inconsistently ((WInconsistent& inc)
                {
                   const auto x1 = c.GetInconsistent (inc);
                   const auto x2 = c.GetInconsistent (inc);
                   //at this point we could have x1 != x2
                });
```

Making a bunch of calls to the `GetReadOnly` method does largely the same thing as `Inconsistently` but `Inconsistently` will have much better performance since `GetReadOnly` will create a new transaction on each call. Really `Inconsistently` is meant for certain GUI actions such as filling a data table where consistency doesn't matter since you will be getting *update* messages from a data store which will be used to remove any inconsistencies in the data.

### Retry

There can be cases where you're looking for certain conditions to be in force, say by reading a bunch of `WVar`s. If you are running a transaction and, after reading some `WVar`s, have determined that your condition hasn't been met yet and want to wait until it is then you'll want to call `Retry`. This will put your thread to sleep until one of the `WVar`s that you read changes, at which point your thread will be woken up and the transaction restarted so that you can check the values again. For example:

```C++
WVar<int> counter(0);

void WaitForCount(const int desiredCount, WAtomic& at)
{
   if(counter.Get(at) < desiredCount)
      Retry(at);
}
```

`WaitForCount` does what is says: it waits until `counter`, which is hopefully being incremented by another thread, reaches `desiredCount`. When `counter` is too small `Retry` is called which causes the thread to sleep until `counter` is updated. When the update happens `WaitForCount` is run from the beginning. This will repeat until `counter` is large enough that we skip the call to `Retry`. 

`Retry` takes a second argument that species a timeout. If the timeout is reached then a `WRetryTimeoutException` is thrown. By default `Retry` has no timeout and will wait forever. You can also specify a *total retry timeout* for you transaction by passing `WMaxRetryWait` to `Atomically`. This timeout applies to all calls to `Retry` made within the transaction. If a `Retry` call also specifies a timeout then the shorter timeout will be used. 

You can also limit how many times `Retry` can be called within a call to `Atomically` by passing `WMaxRetries` to `Atomically`. If this limit is hit a `WMaxRetriesException` is thrown. 

For example:

```C++
try
{
   Atomically([&](){SomeTransaction(at);}, WMaxRetries(5), WMaxRetryWait(seconds(10)));
}
catch(WMaxRetriesException& err)
{
   //get here if SomeTransaction retries 5 times
}
catch(WRetryTimeoutException& err)
{
   //get here if we wait at least ten seconds in a Retry
}
```

Again, the order of the extra arguments to `Atomically` doesn't matter, only that they follow the function to be executed.

### After Actions

As mentioned above, side-effects are to be avoided in transactions as the transaction can be repeated due to conflicts with transactions running in other threads. If you determine within a transaction that a side-effect needs to happen then you need to call `WAtomic::After` passing it a function object that enacts the side effect. When the transaction commits, all the functions passed to `WAtomic::After` for that transaction are run. For example:

```C++
WVar<int> x(0);

void DoSomething(WAtomic& at)
{
   if(x.Get(at) > 10)
      at.After([](){std::cout << "x is greater than 10";});
}
```

`DoSomething` will cause `x is greater than 10` to be printed whenever `x` is greater than 10. Note that the message will not be printed until the transaction that `DoSomething` is running in commits successfully. 

The functions passed to `After` in a nested transaction will not be run until the top-level transaction commits successfully. 

As the functions passed to `After` are called after the current function returns it should go without saying that you need to be careful about object lifetime when capturing objects into a function object that is passed to `After`.

### Before Commit Actions

There can be rare cases where you need to do something just before the transaction commits. In this case you can pass a function to `WAtomic::BeforeCommit`. Any function passed to `BeforeCommit` will be called after the transaction function returns, but before transaction validation is done. As with *after* functions, *before* functions added in a nested transaction will not be run until the top-level transaction is about to commit.

### On Fail Actions

In some cases you may want to take some action if a transaction fails to commit. `WAtomic::OnFail` is the what you want in this case. Any function passed to `OnFail` will be called if the transaction ends without successfully committing. The commit could fail due to a conflict, an exception being thrown, or `Retry` being called. In any of those cases the `OnFail` functions will be called. The use cases for `OnFail` are few, most of the time the same effect can be accomplished using RAII and/or `shared_ptr` for resource management. 

### NO_ATOMIC

If a function explicitly does side-effects and you want to be sure that it is not called from within a transaction then you should flag it with `NO_ATOMIC`. Just add an argument to the function that is `NO_ATOMIC` in the function declaration and in the definition use `NO_ATOMIC_IMPL`. If this function is called from within a transaction a `WInAtomicError` will be thrown. For example:

```C++
void DoesSideEffects (int x, NO_ATOMIC);

void DoesSideEffects (int x, NO_ATOMIC_IMPL)
{}
```

### Read-Locks

The transaction system uses a reader/writer mutex internally. Each time a variable is accessed a read lock on the mutex has to be obtained while a write lock is obtained when committing a transaction. This can create a performance problem for code that needs to read lots of variables in one go. In these cases one can create a `WReadLockGuard` object that will create a lasting read-lock on the mutex. While this object is locked, calls to get and set variables will not need to create their own read-lock and thus they will proceed faster. Do not hold the lock too long though as no other thread can commit a transaction while the lock is held. Note that holding the lock until you are ready to commit will not increase the chances of your commit succeeding. The read-lock will be released before the commit begins and if there are other threads already waiting to commit they will commit first possibly invalidating your transaction. 

```C++
void ReadLotsOfVars(WAtomic& at)
{
   WReadLockGuard<WAtomic> lock(at);
   //read lots of WVars
}
```
### Manual Validation

Normally it is best to minimize the amount of computation being done in a transaction. The longer a transaction runs for, the higher chance it has of having a conflict and needing to be repeated. But sometimes there's just no good way to avoid having a long transaction. When this is the case it can be advantageous to validate the transaction at various points during the calculation. This way, if the transaction is already doomed due to a conflict you can bail out and have the transaction restart without going through the whole calculation. The `WAtomic::Validate` method is used to do this. If the transaction has a conflict then `Validate` will cause the current transaction to be restarted immediately.

```C++
Atomically ([&](WAtomic& at)
            {
               //do some stuff
               at.Validate ();
               //do some more stuff
               at.Validate ();
               //do even more stuff that would be better done outside
               //of a transaction but for some reason we need to do it
               //inside the transaction
            });
```

### InAtomic

If you need to know if you are in a transaction or not at a certain point in the code you can call `InAtomic`. Normally this is unnecessary, if you have a `WAtomic` object then you know you're in a transaction. If you want to prevent a function from being called from within a transaction then `NO_ATOMIC` is what you want to use.

### Transaction Local Values

Sometimes it is useful to have a variable that contains a unique value in each transaction. `WTransactionLocalValue` is used in this case. When a transaction is started any given `WTransactionLocalValue` object will be empty and its `Get` method will return a null pointer. If `Set` is called then that `WTransactionLocalValue` object will acquire a value that is only accessible within the transaction `Set` was called in. The type stored must be either copyable or movable. When the transaction ends any values stored in `WTransactionLocalValue` objects are destroyed.

```C++
WTransactionLocalValue<int> localData;

void RunTransactions ()
{
   Atomically ([&](WAtomic& at)
               {
                  assert (!localData.Get (at));
                  localData.Set (10, at);
                  const auto local_p = localData.Get (at);
                  assert (local_p);
                  assert (*local_p == 10);
               });
   Atomically ([&](WAtomic& at)
               {
                  //The local value from the previous transaction
                  //should have been thrown away
                  assert (!localData.Get (at));
               });
}
```

If you are trying to limit an operation to only being done once a transaction then you could use `WTransactionLocalValue`, but `WTransactionLocalFlag` would probably be a better choice. It has a single method, `TestAndSet`, which sets the flag and returns the previous state of the the flag so that you can know if a call is the first one to set the flag.

```C++
WTransactionLocalFlag flag;

void DoSomethingOnce (WAtomic& at)
{
   if (!flag.TestAndSet (at))
   {
      //Do the thing we only want to do once
   }
}

WVar<int> x (0);
WVar<int> y (0);

void DoSomethingOnceMaybe ()
{
   Atomically ([&](WAtomic& at)
               {
                  if (x.Get (at) > 10)
                  {
                     DoSomethingOnce (at);
                  }
                  if (y.Get (at) > 10)
                  {
                     DoSomethingOnce (at);
                  }
               });
}
```

In the above example we have something that we only want to do once per transaction, but multiple code paths that can lead to it being done. So we use a `WTransactionLocalFlag` to protect the doing of the thing. This example is a contrived, and we could easily accomplish doing the thing only once by combining the conditions on `x` and `y` or be returning after calling `DoSomethingOnce`. But in more complicated code where these simplifications aren't possible `WTransactionLocalFlag` can help simplify things.

### Pitfalls

There are some pitfalls to watch out for when using STM.

#### Side Effects

Since transactions are repeated when conflicts are encountered one has to be careful about side-effects in transactional code. If something is missed you can get repeated side effects. Normally this is obvious, such as a message or dialog box appearing more times than it should. In these cases the side-effect simply needs to be moved into an *after action* using `WAtomic::After`.

#### Value Consistency

Let's say we have two `WVar` objects called `x` and `y` and they have an invariant `x > y` that is enforced by all transactions that touch those variables. Unfortunately, it is still possible in our system for a transaction to see the invariant being violated. When this happens the transaction that can see the invariant being violated already has a conflict and will not be able to commit, but we can still run into problems depending on what is being done with the inconsistent values while still in the transaction. To see how this can happen and a problem that can arise say we are doing this

```C++
WVar<size_t> x (10);
WVar<size_t> y (9);

void ChangeXAndY ()
{
   Atomically ([&](WAtomic& at)
               {
                  x.Set (12, at);
                  y.Set (11, at);
               });
}

std::vector<int> AllocateBasedOnXAndY ()
{
   return Atomically ([&](WAtomic& at)
                      {
                         const auto x1 = x.Get (at);
                         const auto y1 = y.Get (at);
                         return std::vector<int> (x1 - y1);   
                      });
}
```

We have two threads, one is executing `AllocateBasedOnXAndY` and the other is executing `ChangeXAndY`. Say that one thread does the read of `x` in `AllocateBasedOnXAndY` and then, before is can read `y`, the other thread commits the changes made in `ChangeXAndY` (which maintains the invariant that `x > y`). We then read `y` in the first thread and end up with `x1 == 10` and `y1 == 11` violating the invariant. Obviously we have a conflict here and the transaction won't commit, but that won't be discovered until we try to create a ridiculously large vector resulting in an exception being thrown when enough memory cannot be allocated.

To fix this we can add a manual validation before allocating the vector

```C++
std::vector<int> AllocateBasedOnXAndY ()
{
   return Atomically ([&](WAtomic& at)
                      {
                         const auto x1 = x.Get (at);
                         const auto y1 = y.Get (at);
                         at.Validate ();
                         return std::vector<int> (x1 - y1);   
                      });
}
```

This way the transaction will be restarted if the `x` and `y` values aren't consistent. In this case we could also move the creation of the vector out of the transaction and only retrieve the values in the transaction. This would avoid the allocation issue, as long as `AllocateBasedOnXAndY` hasn't been called from within another transaction. To avoid this we would need to stick a `NO_ATOMIC` on `AllocateBasedOnXAndY`. That might be something you want to avoid, in that case `Validate` is the way to go.

Generally it is rare that `Validate` needs to be used like this. Cases where invariant violation can cause problems in transactions that leak out into the code that started the transaction are few. Arguably, memory allocation is a side-effect that shouldn't be done in a transaction and that is the real problem in the above example. But if RAII is used then memory allocation can be considered an idempotent side-effect that can be safely repeated. One just has to watch out for invariant violation as we saw above.

#### Expensive To Copy Objects

Values stored in `WVar` objects are always copied. This can be a problem if you want to store something is either not copyable or expensive to copy. In that case you'll need to store a pointer to the object. Due to the indeterminacy of the lifetimes of objects stored in `WVar` objects (one thread can replace a value in a `WVar` object, but if another transaction read the old value then the old value will be kept alive until the last transaction that read it finishes) using bare pointers is a recipe for disaster. `unique_ptr` won't work either because it can't be copied. So you're left with `shared_ptr`, but what should the pointer point at?

We need to be sure that what we point at is thread-safe. You have two options: immutable or *internally transacted*. In the first case we store a `shared_ptr<const Type>` so that the object pointed to can't be modified. In order to update it you have to first get the pointer from the `WVar` object, copy the underlying object, modify the copy, and finally put the pointer to the new object in the `WVar` object. This all needs to be done in one transaction. For example

```C++
struct BigThing
{
   //bunch of fields that make this painful to copy
   int m_thing;
};

WVar<std::shared_ptr<const BigThing>> bigThing (std::make_shared<BigThing>());

void UpdateBigThing ()
{
   Atomically ([&](WAtomic& at)
               {
                  const auto old_p = bigThing.Get (at);
                  const auto new_p = boost::make_shared<BigThing>(*old_p);
                  new_p->m_thing = 10;
                  bigThing.Set (new_p, at);
               });
}
```

This strategy works well when the stored object is read often but only updated rarely. 

The other option is to make the pointed to object *internally transacted*. This just means that any mutable state in the object is transacted (e.g. stored in `WVar` objects). We also have to make sure that any side-effects in the objects methods are handled appropriately. For example

```C++
class InternallyTransacted
{
public:
   WVar<int> m_x;

   void DoSomething (WAtomic& at)
   {
      at.After ([&](){SideEffect ();});
   }
   
private:
   void SideEffect (NO_ATOMIC);
};

WVar<std::shared_ptr<InternallyTransacted>> intern (std::make_shared<InternallyTransacted>());

void DoStuff ()
{
   Atomically ([&](WAtomic& at)
               {
                  const auto intern_p = intern.Get (at);
                  intern_p->m_x.Set (10, at);
                  intern_p->DoSomething (at);
               });
}
```

Note that in this case the `WVar` stores a `std::shared_ptr<InternallyTransacted>`, there's no `const`. Because the object is internally transacted it is thread-safe to use it in a non-const fashion.

Internally transacted objects are useful when an object is going to written as much as it is read or if the methods of the object are used to trigger side-effects (e.g. the object is interface to an external piece of hardware).

## Extras

In addition to the core STM system, the Wyatt-STM includes a few useful structures built on top of the STM system.

### Exception Capture

The `WExceptionCapture` class (see `wstm/exception_capture.h`) allows you to capture an object that can then be transferred to another thread where it can be thrown as an exception. One captures an object using the `Capture` method. Note that objects passed to `Capture` must be copyable. The captured object can be thrown using the `ThrowCaptured` method.

```C++
WExceptionCapture captured;

void SignalError ()
{
   Atomically ([&](WAtomic& at)
               {
                  //oops, need to signal other threads that we have an error
                  captured.Capture (WSomeException (), at);
               });
}

void HandleException ()
{
   Atomically ([&](WAtomic& at)
               {
                  try
                  {
                     captured.ThrowCaptured (at);
                     //if we get to here there was no error in the other thread and we can continue
                  }
                  catch (WSomeException& exc)
                  {
                     //handle the error here
                  }
               });
}
```

If we have a thread that calls `SignalError` for some reason the other threads that are running `HandleException` can catch and handle the exception.

### Deferred Results

Wyatt-STM contains a *deferred results* system that functions very similar to the `std::future` and `std::promise` classes in the C++ standard library, only the *deferred results* system is transactional. The analogs of `std::future` and `std::promise` in Wyatt-STM are `WDeferredResult` and `WDeferredValue` (see `wstm/deferred_result.h`). The latter class is the *write end* of the result while the former can only be used to receive a result from the `WDeferredValue` that it is attached to. Usually the `WDeferredValue` of a value/result pair is sent into another thread so that when the calculation in that thread is done the result can be sent back to the original thread. For example:

```C++
WDeferredResult<int> StartCalc()
{
  WDeferredValue<int> value;
  std::thread([=]()
              {
                 int result = 0;
                 //do stuff to calculate result
                 value.Done(result);
              });
  return value;
}

auto res = StartCalc();
res.Wait();
assert(res.IsDone());
auto result = res.GetResult();
```

`StartCalc` launches a calculation in another thread that reports its result through the `WDeferredResult<int>` returned by `StartCalc`. Note that the calculation in the other thread uses the `WDeferredValue<int>` side of the pair. To get the value from the `WDeferredResult<int>` the caller of `StartCalc` must first wait for the calculation to be done by either using `WDeferredResult::IsDone` or `WDeferredResult::Wait` and then calling `GetResult`.

This system can also be used to report errors by calling `WDeferredValue::Fail` instead of `Done`. When `Fail` has been called the `WDeferredResult::IsDone` will report true but `Failed` will also report true. When there is an error `ThrowError` can be used to cause the `WDeferredResult` to throw the error. For example:

```C++
WDeferredResult<int> StartCalc()
{
  WDeferredValue<int> value;
  std::thread([=]()
              {
                 value.Fail(std::string("Oops"));
              });
  return value;
}

auto res = StartCalc();
res.Wait();
if(res.Failed())
{
   try
   {
      res.ThrowError();
   }
   catch(const std::string& err)
   {
      std::err << "Got error: " << err << std::endl;
   }
}
```

### Channels

Wyatt-STM also includes a multi-cast channel system comprised of the classes in `wstm/channel.h`. The core of the system is the `WChannel` class which is used to send messages on the channel. In order to receive messages one needs to connect a `WChannelReader` to the `WChannel`. Once the connection is made, the reader will receive any messages sent through the channel. Note that there will only ever be one copy of a given message regardless of now many readers there are. All readers share the same instance of the message, so messages must either be immutable or internally transacted.

`WChannel` also has a *write signal* that functions can be registered with. The registered functions are called whenever a message is written to the channel. Note that the functions are called in the thread that wrote the message, not the thread that registered the function. This is useful for connecting channels up to GUI handlers.

An *initial message* function can also be specified for a channel. If such a function is specified then it will be called whenever a new reader connects to the channel in order to generate an *initial message* to be sent to the new reader only. This is useful for sending the initial state of some object to a new reader when the channel carries messages about state updates for the object.

If one doesn't want to expose a `WChannel` object in the public interface of a class to allow readers to be connected then a `WReadOnlyChannel` should be exposed in the public interface and connected to the private `WChannel` object instead. `WReadOnlyChannel` can be used to connect readers to a channel and nothing else. That way the ability to send messages is kept private while allowing for reader connection.

The final class in the system is `WChannelWriter`. Objects of this class are connected to a `WChannel` and then can be used to send messages on that channel. This doesn't seem that useful since one could do the same thing with the `WChannel` object itself. The difference is that `WChannelWriter` holds a weak reference to the channel innards while `WChannel` and `WChannelReader` both have strong references. Normally, a writer will be sent into another thread so that that thread can send messages on the channel. If the main channel object and all the readers go away then there will be no one to read any messages that the writer writes, and no way for new readers to be added. In this case the writer's `Write` method becomes a no-op (other than returning false to alert the caller that the channel is dead) so it doesn't uselessly add messages to the channel and so that its owner can know that no one cares about the results that are being generated and the calculation can stop. 


