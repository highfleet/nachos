// synchlist.cc
//	Routines for synchronized access to a list.
//
//	Implemented by surrounding the List abstraction
//	with synchronization routines.
//
// 	Implemented in "monitor"-style -- surround each procedure with a
// 	lock acquire and release pair, using condition signal and wait for
// 	synchronization.
//  lock 保证管程互斥进入
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
 
#include "copyright.h"
#include "synchlist.h"

//----------------------------------------------------------------------
// SynchList::SynchList
//	Allocate and initialize the data structures needed for a 
//	synchronized list, empty to start with.
//	Elements can now be added to the list.
//----------------------------------------------------------------------

SynchList::SynchList()
{
    list = new List();
    lock = new Lock("list lock"); 
    listEmpty = new Condition("list empty cond");
}

//----------------------------------------------------------------------
// SynchList::~SynchList
//	De-allocate the data structures created for synchronizing a list. 
//----------------------------------------------------------------------

SynchList::~SynchList()
{ 
    delete list; 
    delete lock;
    delete listEmpty;
}

//----------------------------------------------------------------------
// SynchList::Append
//      Append an "item" to the end of the list.  Wake up anyone
//	waiting for an element to be appended.
//
//	"item" is the thing to put on the list, it can be a pointer to 
//		anything.
//----------------------------------------------------------------------

void
SynchList::Append(void *item)
{
    lock->Acquire();		// enforce mutual exclusive access to the list 
    list->Append(item);
    listEmpty->Signal(lock);	// wake up a waiter, if any
    // 如果是Hoare语义:
    // 此处Signal交出锁， 唤醒等待进程并进入Signal队列等待， 等待其结束后立即拿回锁.
    // 有进程结束时会优先到Signal队列中调度 因此锁*一定*会拿回来。
    lock->Release();
}

//----------------------------------------------------------------------
// SynchList::Remove
//      Remove an "item" from the beginning of the list.  Wait if
//	the list is empty.
// Returns:
//	The removed item. 
//----------------------------------------------------------------------

void *
SynchList::Remove()
{
    void *item;

    lock->Acquire();			// enforce mutual exclusion
    while (list->IsEmpty())
	listEmpty->Wait(lock);		// wait until list isn't empty
    item = list->Remove();
    ASSERT(item != NULL);
    lock->Release();
    return item;
}

//----------------------------------------------------------------------
// SynchList::Mapcar
//      Apply function to every item on the list.  Obey mutual exclusion
//	constraints.
//
//	"func" is the procedure to be applied.
//----------------------------------------------------------------------

void
SynchList::Mapcar(VoidFunctionPtr func)
{ 
    lock->Acquire(); 
    list->Mapcar(func);
    lock->Release(); 
}
