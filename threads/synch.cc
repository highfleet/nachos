// synch.cc 
//	Routines for synchronizing threads.  Three kinds of
//	synchronization routines are defined here: semaphores, locks 
//   	and condition variables (the implementation of the last two
//	are left to the reader).
//
// Any implementation of a synchronization routine needs some
// primitive atomic operation.  We assume Nachos is running on
// a uniprocessor, and thus atomicity can be provided by
// turning off interrupts.  While interrupts are disabled, no
// context switch can occur, and thus the current thread is guaranteed
// to hold the CPU throughout, until interrupts are reenabled.
//
// Because some of these routines might be called with interrupts
// already disabled (Semaphore::V for one), instead of turning
// on interrupts at the end of the atomic operation, we always simply
// re-set the interrupt state back to its original value (whether
// that be disabled or enabled).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synch.h"
#include "system.h"

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	Initialize a semaphore, so that it can be used for synchronization.
//
//	"debugName" is an arbitrary name, useful for debugging.
//	"initialValue" is the initial value of the semaphore.
//----------------------------------------------------------------------

Semaphore::Semaphore(char* debugName, int initialValue)
{
    name = debugName;
    value = initialValue;
    queue = new List;
}

//----------------------------------------------------------------------
// Semaphore::Semaphore
// 	De-allocate semaphore, when no longer needed.  Assume no one
//	is still waiting on the semaphore!
//----------------------------------------------------------------------

Semaphore::~Semaphore()
{
    delete queue;
}

//----------------------------------------------------------------------
// Semaphore::P
// 	Wait until semaphore value > 0, then decrement.  Checking the
//	value and decrementing must be done atomically, so we
//	need to disable interrupts before checking the value.
//
//	Note that Thread::Sleep assumes that interrupts are disabled
//	when it is called.
//----------------------------------------------------------------------

void
Semaphore::P()
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);	// disable interrupts
    
    // while不可以换成if:
    // 可能被其他线程抢占
    while (value == 0) { 			// semaphore not available
	queue->Append((void *)currentThread);	// so go to sleep
	// 让线程进入BLOCKED（仅有V才能将它唤醒）
    currentThread->Sleep();
    } 
    value--; 					// semaphore available, 
						// consume its value                        
    (void) interrupt->SetLevel(oldLevel);	// re-enable interrupts
}

//----------------------------------------------------------------------
// Semaphore::V
// 	Increment semaphore value, waking up a waiter if necessary.
//	As with P(), this operation must be atomic, so we need to disable
//	interrupts.  Scheduler::ReadyToRun() assumes that threads
//	are disabled when it is called.
//----------------------------------------------------------------------

void
Semaphore::V()
{
    Thread *thread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);

    thread = (Thread *)queue->Remove();
    if (thread != NULL)	   // make thread ready, consuming the V immediately
	scheduler->ReadyToRun(thread);
    value++;
    (void) interrupt->SetLevel(oldLevel);
}

Lock::Lock(char* debugName) 
{
    name = debugName;
    lock = new Semaphore(debugName, 1);
    holder = NULL;
}
Lock::~Lock() 
{
    delete lock;
}
void Lock::Acquire() 
{
    // 这期间中断始终被关闭
    // 包括进入P内部时...
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    lock -> P();
    holder = currentThread;
    (void)interrupt->SetLevel(oldLevel);
}
void Lock::Release() 
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    // 保证锁的释放者必须持有锁
    ASSERT(isHeldByCurrentThread())
    lock -> V();
    holder = NULL;
    (void)interrupt->SetLevel(oldLevel);
}
bool Lock::isHeldByCurrentThread()
{
    // 必须在关闭中断的上下文中调用
    return holder == currentThread;
}

Condition::Condition(char* debugName) 
{
    queue = new List;
}
Condition::~Condition() 
{
    delete queue;
}
void Condition::Wait(Lock* conditionLock) 
{
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    ASSERT(conditionLock->isHeldByCurrentThread());
    queue->Append((void *)currentThread);
    conditionLock->Release();
    currentThread->Sleep();
    conditionLock->Acquire();
    (void)interrupt->SetLevel(oldLevel);
}
void Condition::Signal(Lock* conditionLock) 
{ 
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    ASSERT(conditionLock->isHeldByCurrentThread());

    if (queue->IsEmpty())
        return;

    Thread *t = (Thread *)queue->Remove();
    // Wake me up when it's all over ♪
    scheduler->ReadyToRun(t);
    (void)interrupt->SetLevel(oldLevel);
}
void Condition::Broadcast(Lock* conditionLock) 
{ 
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    ASSERT(conditionLock->isHeldByCurrentThread());
    queue->Mapcar((VoidFunctionPtr)Wakeup);
    (void)interrupt->SetLevel(oldLevel);
}
