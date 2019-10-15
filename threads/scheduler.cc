// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "scheduler.h"
#include "system.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads to empty.
//  Scheduler对进程状态的改变;
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    readyList = new List; 
     // 初始化所有进程列表
    AllThreads = new List;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyList;
    delete AllThreads;
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//
//  
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    DEBUG('t', "Putting thread %s on ready list.\n", thread->getName());

    thread->setStatus(READY);

#if PRIORITY
    // 带优先级的插入 随时保持顺序..
    readyList->SortedInsert((void *)thread, thread->getPriority());
    
#else
    // Append将线程插入列表末端
    // 要被放到队列末尾了..清空线程使用的时间片吧...
    thread->time_used = 0;
    readyList->Append((void *)thread);
    
#endif
    
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    // 返回链表最前的元素
    return (Thread *)readyList->Remove();
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//  记录了当前进程currentThread
//  切换上下文 包括:{
//      页表和页表大小,
//      所有寄存器(使用汇编语言,在SWITCH中实现),***
//      清理上一个进程,
//  }
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread)
{
    Thread *oldThread = currentThread;
    
#ifdef USER_PROGRAM			// ignore until running user programs 
    if (currentThread->space != NULL) {	// if this thread is a user program,
        currentThread->SaveUserState(); // save the user's CPU registers
	currentThread->space->SaveState();
    }
#endif
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    currentThread = nextThread;		    // switch to the next thread
    currentThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG('t', "Switching from thread \"%s\" to thread \"%s\"\n",
	  oldThread->getName(), nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    // 如果某一个线程运行到了这里
    // 这以上 是在旧线程的上下文中运行的
    nextThread->last_tick = stats->systemTicks;

    SWITCH(oldThread, nextThread);
    // 这以下 是在新线程的上下文中运行的
    
    DEBUG('t', "Now in thread \"%s\"\n", currentThread->getName());

    // 维护RR
    // currentThread->last_tick = stats->systemTicks;
    // printf("thread %s Started time recorded %d\n",currentThread->getName(),stats->systemTicks);

    // If the old thread gave up the processor because it was finishing,
    // we need to delete its carcass.  Note we cannot delete the thread
    // before now (for example, in Thread::Finish()), because up to this
    // point, we were still running on the old thread's stack!
    // 唔唔 在切换之前 你始终Running on oldstack...
    // 这说明nachos不会在进程切换时陷入内核
    
    if (threadToBeDestroyed != NULL){
        // printf("deleting ");
        // ThreadPrint((int)threadToBeDestroyed);
        // puts("");
        delete threadToBeDestroyed;
        threadToBeDestroyed = NULL;
    }

#ifdef USER_PROGRAM
    if (currentThread->space != NULL) {		// if there is an address space
        //  这是在回复用户寄存器...
        currentThread->RestoreUserState();     // to restore, do it.
    //  这是在恢复页表和页表大小
	currentThread->space->RestoreState();
    }
#endif
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    printf("Ready list contents:\n");
    readyList->Mapcar((VoidFunctionPtr) ThreadPrint);
}



//----------------------------------------------------------------------
// Scheduler::PrintAllThreads
// 打印全部正在运行的进程! 经由TS命令调用
// 具体是把Current 和 ReadyList 的进程打印一下...
//----------------------------------------------------------------------
void
Scheduler::PrintAllThreads()
{
    //currentThread->PrintInfo();
    AllThreads->Mapcar((VoidFunctionPtr)ThreadPrintInfo);
}