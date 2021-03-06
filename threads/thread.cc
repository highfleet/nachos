// thread.cc 
//	Routines to manage threads.  There are four main operations:
//
//	Fork -- create a thread to run a procedure concurrently
//		with the caller (this is done in two steps -- first
//		allocate the Thread object, then call Fork on it)
//	Finish -- called when the forked procedure finishes, to clean up
//	Yield -- relinquish control over the CPU to another ready thread
//	Sleep -- relinquish control over the CPU, but thread is now blocked.
//		In other words, it will not run again, until explicitly 
//		put back on the ready queue.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "thread.h"
#include "switch.h"
#include "synch.h"
#include "system.h" 

#define STACK_FENCEPOST 0xdeadbeef	// this is put at the top of the
					// execution stack, for detecting 
					// stack overflows
MsgQueue *msgQueue;
//----------------------------------------------------------------------
// Thread::Thread
// 	Initialize a thread control block, so that we can then call
//	Thread::Fork.
//
//	"threadName" is an arbitrary string, useful for debugging.
//
//  Lab1
//  分配TID
//  Lab2
//  增加了初始化优先级的参数 默认为最低
//----------------------------------------------------------------------

Thread::Thread(char* threadName, int priorityLevel = minPriority)
{
    name = threadName;
    stackTop = NULL;
    stack = NULL;
    status = JUST_CREATED;

    //nachos还没有多用户机制 暂且认为都在0号用户下

    //超过线程上限,报错..
    DEBUG('t', "Creating a new thread");
    ASSERT(currentThreadNum < MaxThreadNum);

    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    uid = 0;
    tid = TidAllocate();

    priority = priorityLevel;
    (void) interrupt->SetLevel(oldLevel);
    time_used = 0;

    // 将此进程添加到所有进程表里...
    scheduler->AllThreads->SortedInsert((void *)this, tid);
    msgList = new MsgList;

#ifdef USER_PROGRAM
    space = NULL;
    // 在StartProgress中赋值
    // 初始化打开文件表
    openFiles = new List();
    OpenFile *STDIN = new OpenFile(0);
    OpenFile *STDOUT = new OpenFile(1);
    openFiles->SortedInsert(STDIN, 0);
    openFiles->SortedInsert(STDOUT, 1);
#endif
}

//----------------------------------------------------------------------
// Thread::~Thread
// 	De-allocate a thread.
//
// 	NOTE: the current thread *cannot* delete itself directly,
//	since it is still running on the stack that we need to delete.
//
//      NOTE: if this is the main thread, we can't delete the stack
//      because we didn't allocate it -- we got it automatically
//      as part of starting up Nachos.
//      由 Scheduler::Run 调用
//
//----------------------------------------------------------------------

Thread::~Thread()
{
    DEBUG('t', "Deleting thread \"%s\"\n", name);

    ASSERT(this != currentThread);


    // 释放TID 
    TidPool[tid] = 0;
    currentThreadNum--;

    // 在列表中删除自己
    scheduler->AllThreads->Remove(this);

    if (stack != NULL)
	DeallocBoundedArray((char *) stack, StackSize * sizeof(int));

#ifdef USER_PROGRAM
    delete openFiles;
#endif
}

//----------------------------------------------------------------------
// Thread::Fork
// 	Invoke (*func)(arg), allowing caller and callee to execute 
//	concurrently.
//
//	NOTE: although our definition allows only a single integer argument
//	to be passed to the procedure, it is possible to pass multiple
//	arguments by making them fields of a structure, and passing a pointer
//	to the structure as "arg".
//
// 	Implemented as the following steps:
//		1. Allocate a stack
//		2. Initialize the stack so that a call to SWITCH will
//		cause it to run the procedure
//		3. Put the thread on the ready queue
// 	
//	"func" is the procedure to run concurrently.
//	"arg" is a single argument to be passed to the procedure.
//----------------------------------------------------------------------
 
void 
Thread::Fork(VoidFunctionPtr func, void *arg)
{
    DEBUG('t', "Forking thread \"%s\" with func = 0x%x, arg = %d\n",
	  name, (int) func, (int*) arg);
    
    // 运行的函数、传参 
    StackAllocate(func, arg);

    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    scheduler->ReadyToRun(this);	// ReadyToRun assumes that interrupts 
					// are disabled!
    (void) interrupt->SetLevel(oldLevel);

#if PRIORITY
    // 如果新线程比原线程优先级要高...
    currentThread->Yield();

#endif
}    

//----------------------------------------------------------------------
// Thread::CheckOverflow
// 	Check a thread's stack to see if it has overrun the space
//	that has been allocated for it.  If we had a smarter compiler,
//	we wouldn't need to worry about this, but we don't.
//
// 	NOTE: Nachos will not catch all stack overflow conditions.
//	In other words, your program may still crash because of an overflow.
//
// 	If you get bizarre results (such as seg faults where there is no code)
// 	then you *may* need to increase the stack size.  You can avoid stack
// 	overflows by not putting large data structures on the stack.
// 	Don't do this: void foo() { int bigArray[10000]; ... }
//----------------------------------------------------------------------

void
Thread::CheckOverflow()
{
    if (stack != NULL)
#ifdef HOST_SNAKE			// Stacks grow upward on the Snakes
    //  这个SNAKES就是逊啦
	ASSERT(stack[StackSize - 1] == STACK_FENCEPOST);
#else
	ASSERT((int) *stack == (int) STACK_FENCEPOST);
#endif
}

//----------------------------------------------------------------------
// Thread::Finish
// 	Called by ThreadRoot when a thread is done executing the 
//	forked procedure.
//
// 	NOTE: we don't immediately de-allocate the thread data structure 
//	or the execution stack, because we're still running in the thread 
//	and we're still on the stack!  Instead, we set "threadToBeDestroyed", 
//	so that Scheduler::Run() will call the destructor, once we're
//	running in the context of a different thread.
//
// 	NOTE: we disable interrupts, so that we don't get a time slice 
//	between setting threadToBeDestroyed, and going to sleep.
//  如果发生了的话 那么这个进程依然还在ReadyList中
//----------------------------------------------------------------------

//  Finish 的进程一定会被Run检测到
//  因为这个进程是当前Running的进程
void
Thread::Finish ()
{
    // 这里关闭了中断 下一个进程被调度时再打开(在Yield()中)
    (void) interrupt->SetLevel(IntOff);		
    ASSERT(this == currentThread);
    
    DEBUG('t', "Finishing thread \"%s\"\n", getName());
    
    threadToBeDestroyed = currentThread;
    Sleep();					// invokes SWITCH
    // not reached
}

//----------------------------------------------------------------------
//  礼让是一种中华民族的传统美德~~
//  Thread::Yield
// 	Relinquish the                                                                                                                                                                                                                                  CPU if any other thread is ready to run.
//	If so, put the thread on the end of the ready list, so that
//	it will eventually be re-scheduled.
//
//	NOTE: returns immediately if no other thread on the ready queue.
//	Otherwise returns when the thread eventually works its way
//	to the front of the ready list and gets re-scheduled.
//  当前进程返回的时候 已经过了一个周期了
//  "怀旧空吟闻笛赋，到乡翻似烂柯人"
//	NOTE: we disable interrupts, so that looking at the thread
//	on the front of the ready list, and switching to it, can be done
//	atomically.  On return, we re-set the interrupt level to its
//	original state, in case we are called with interrupts disabled. 
//
// 	Similar to Thread::Sleep(), but a little different.
//
//  Lab2
//  抢占式调度:
//  现将currentThread加入ReadyList
//  因为它可能依然是优先级最高的...!
//
//
//----------------------------------------------------------------------

void
Thread::Yield ()
{
    Thread *nextThread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    
    ASSERT(this == currentThread);
    
    DEBUG('t', "Yielding thread \"%s\"\n", getName());

#if PRIORITY
    scheduler->ReadyToRun(this);
    nextThread = scheduler->FindNextToRun();
    scheduler->Run(nextThread);

#elif RR
    // 如果时间片超了 就把线程放到末尾
    // 如果还有剩余的时间片 或者是唯一一个就绪进程 就立即返回...
    currentThread->last_tick = stats->systemTicks;

    nextThread = scheduler->FindNextToRun();
    if(nextThread!=NULL){
        scheduler->ReadyToRun(this);
        scheduler->Run(nextThread);
    }

#else
    nextThread = scheduler->FindNextToRun();
    if (nextThread != NULL) {
	scheduler->ReadyToRun(this);
    /********** 从这里离开 **********/
	scheduler->Run(nextThread);
    /********** 从这里回归 **********/
    }
#endif

    // 这个开启中断会浪费一点点时间
    // 导致进程运行时间比时钟中断周期长 -10Ticks
    (void) interrupt->SetLevel(oldLevel);
    // 回归的每个线程都要打开中断
    // 新线程的打开中断在StartUpPC中
}


//----------------------------------------------------------------------
// Thread::Sleep
// 	Relinquish the CPU, because the current thread is blocked
//	waiting on a synchronization variable (Semaphore, Lock, or Condition).
//	Eventually, some thread will wake this thread up, and put it
//	back on the ready queue, so that it can be re-scheduled.
//
//	NOTE: if there are no threads on the ready queue, that means
//	we have no thread to run.  "Interrupt::Idle" is called
//	to signify that we should idle the CPU until the next I/O interrupt
//	occurs (the only thing that could cause a thread to become
//	ready to run).
//
//	NOTE: we assume interrupts are already disabled, because it
//	is called from the synchronization routines which must
//	disable interrupts for atomicity.   We need interrupts off 
//	so that there can't be a time slice between pulling the first thread
//	off the ready list, and switching to it.
//----------------------------------------------------------------------
void
Thread::Sleep ()
{
    Thread *nextThread;
    
    // 只能睡当前正在RUNNING的进程
    ASSERT(this == currentThread);
    ASSERT(interrupt->getLevel() == IntOff);
    
    DEBUG('t', "Sleeping thread \"%s\"\n", getName());

    status = BLOCKED;
    while ((nextThread = scheduler->FindNextToRun()) == NULL)
	interrupt->Idle();	// no one to run, wait for an interrupt

    scheduler->Run(nextThread); // returns when we've been signalled
}


void 
Thread::Suspend(){
#ifdef USER_PROGRAM
    for (int i = 0; i < space->numPages;i++){
        TranslationEntry *pte = space->pageTable + i;
        if (pte->valid &&pte->dirty){
            SwapoutPage(pte->physicalPage);
            machine->memoryMap->Clear(pte->physicalPage);
            pte->valid = FALSE;
        }
    }
#endif
}

//----------------------------------------------------------------------
// ThreadFinish, InterruptEnable, ThreadPrint
//	Dummy functions because C++ does not allow a pointer to a member
//	function.  So in order to do this, we create a dummy C function
//	(which we can pass a pointer to), that then simply calls the 
//	member function.
//----------------------------------------------------------------------

static void ThreadFinish()    { currentThread->Finish(); }
static void InterruptEnable() { interrupt->Enable(); }
void ThreadPrint(int arg){ Thread *t = (Thread *)arg; t->Print(); }
void ThreadPrintInfo(int ptr) { Thread *t = (Thread *)ptr; t->PrintInfo(); }
void Wakeup(void*t) { scheduler->ReadyToRun((Thread*)t); }

//----------------------------------------------------------------------
// Thread::StackAllocate
//	Allocate and initialize an execution stack.  The stack is
//	initialized with an initial stack frame for ThreadRoot, which:
//		enables interrupts
//		calls (*func)(arg)
//		calls Thread::Finish
//
//	"func" is the procedure to be forked
//	"arg" is the parameter to be passed to the procedure
//----------------------------------------------------------------------

void
Thread::StackAllocate (VoidFunctionPtr func, void *arg)
{
    // i386中 stack是栈的最小地址
    stack = (int *) AllocBoundedArray(StackSize * sizeof(int));


//  HOST_SNAKE
//  FrameMarker是什么?
//  
#ifdef HOST_SNAKE
    // HP stack works from low addresses to high addresses
    stackTop = stack + 16;	// HP requires 64-byte frame marker

    // 栈最大能达到的地址
    stack[StackSize - 1] = STACK_FENCEPOST;
#else
    // High —> Low Address  
    // i386 & MIPS & SPARC stack works from high addresses to low addresses
#ifdef HOST_SPARC
    // SPARC stack must contains at least 1 activation record to start with.
    // 
    stackTop = stack + StackSize - 96;
#else  // HOST_MIPS  || HOST_i386
    stackTop = stack + StackSize - 4;	// -4 to be on the safe side!
#ifdef HOST_i386
    // the 80386 passes the return address on the stack.  In order for
    // SWITCH() to go to ThreadRoot when we switch to this thread, the
    // return addres used in SWITCH() must be the starting address of
    // ThreadRoot.
    *(--stackTop) = (int)ThreadRoot;
#endif
#endif  // HOST_SPARC
    *stack = STACK_FENCEPOST;
    //  防止栈底溢出
#endif  // HOST_SNAKE
    
    //  所有机器状态寄存器的初始化
    machineState[PCState] = (int*)ThreadRoot;
    machineState[StartupPCState] = (int*)InterruptEnable;
    machineState[InitialPCState] = (int*)func;
    machineState[InitialArgState] = arg;
    machineState[WhenDonePCState] = (int*)ThreadFinish;
}

#ifdef USER_PROGRAM
#include "machine.h"

//----------------------- -----------------------------------------------
// Thread::SaveUserState
//	Save the CPU state of a user program on a context switch.
//
//	Note that a user program thread has *two* sets of CPU registers -- 
//	one for its state while executing user code, one for its state 
//	while executing kernel code.  This routine saves the former.
//----------------------------------------------------------------------

void
Thread::SaveUserState()
{
    for (int i = 0; i < NumTotalRegs; i++)
	userRegisters[i] = machine->ReadRegister(i);
}

//----------------------------------------------------------------------
// Thread::RestoreUserState
//	Restore the CPU state of a user program on a context switch.
//
//	Note that a user program thread has *two* sets of CPU registers -- 
//	one for its state while executing user code, one for its state 
//	while executing kernel code.  This routine restores the former.
//----------------------------------------------------------------------

void
Thread::RestoreUserState()
{
    for (int i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, userRegisters[i]);
}
#endif


//----------------------------------------------------------------------
//  (就模仿一下你这zz的代码风格...
//  分配第一个空闲ID
//----------------------------------------------------------------------
int
Thread::TidAllocate ()
{
    ASSERT(currentThreadNum < MaxThreadNum);

    currentThreadNum += 1;
    for (int i = 0; i < currentThreadNum; i++)
        if(!TidPool[i]){
            TidPool[i] = 1;
            return i;
        }
}


//----------------------------------------------------------------------
//  是Thread::Print的强化版本
//  urara
//----------------------------------------------------------------------

void 
Thread::PrintInfo()
{
    char *status_name[] = {"JUST CREATED", "RUNNING", "READY", "BLOCKED"};
    char str[100] = {' '};
    sprintf(str, "%s", name);
    sprintf(str + 20, status_name[status]);
    sprintf(str + 35, "[TID]%d", tid);
    sprintf(str + 45, "[UID]%d", uid);

    for (int i = 0; i < 50;i++)
        if(str[i]==0)
            str[i] = ' ';

    printf(str);
    puts("");
}
//----------------------------------------------------------------------
//  设置线程优先级
//  0-5
//  暂时没用 因为改变后需要重排ReadyList...!
//----------------------------------------------------------------------

int
Thread::setPriority(int val)
{
    if (val < maxPriority || val > minPriority)
        return -1;

    priority = val;
    return 0;
}

bool Send(Message *msg, int dest){
    Thread *destThread = (Thread*)scheduler->AllThreads->Find(dest);
    if(destThread ==NULL)
        return FALSE;
    Message *item = msgQueue->addQueue(msg, currentThread->getTid());
    destThread->msgList->addList(item, currentThread->getTid());
    return TRUE;
}

bool Receive( Message *msg, int src = -1){
    MsgList *list = currentThread->msgList;
    Message* item = list->rcvMsg(src);
    if(item==NULL)
        return FALSE;
    memcpy(msg->msg, item->msg, item->len);
    msgQueue->rcvQueue(item);
    return TRUE;
}

