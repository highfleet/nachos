// threadtest.cc 
//	Simple test case for the threads assignment.
//
//	Create two threads, and have them context switch
//	back and forth between themselves by calling Thread::Yield, 
//	to illustratethe inner workings of the thread system.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "elevatortest.h"

// testnum is set in main.cc
int testnum = 1;

//----------------------------------------------------------------------
// SimpleThread
// 	Loop 5 times, yielding the CPU to another ready thread 
//	each iteration.
//
//	"which" is simply a number identifying the thread, for debugging
//	purposes.
//----------------------------------------------------------------------

void
SimpleThread(int which)
{
    int num;
    
    for (num = 0; num < 5; num++) {
	printf("*** thread %d looped %d times\n", which, num);
        currentThread->Yield();
    }
}

void 
SimpleTickThread(int which)
{
    for (int i = 0; i < 40;i++){
        // 相当于一条命令-一下时钟...
        interrupt->OneTick();
        printf("*** thread %d looped %d times\n", which, i);
    }
}



//----------------------------------------------------------------------
// ThreadTest1
// 	Set up a ping-pong between two threads, by forking a thread 
//	to call SimpleThread, and then calling SimpleThread ourselves.
//----------------------------------------------------------------------

void
ThreadTest1()
{
    DEBUG('t', "Entering ThreadTest1");

    Thread *t = new Thread("forked thread");

    t->Fork(SimpleThread, (void*)1);
    SimpleThread(0);
}

//----------------------------------------------------------------------
// ThreadTest2
// 	测试一下最大线程数量的限制
//	Called by defining TS 
//----------------------------------------------------------------------

void
ThreadTest2()
{
    DEBUG('t', "Entering ThreadTest2");

    for (int i = 1; i < MaxThreadNum;i++){
        Thread *t = new Thread("Test Thread " );
        t->Fork(SimpleThread, NULL);
    }

    // Call TS Function
    scheduler->PrintAllThreads();

    // Invoke ASSERT
    Thread *t = new Thread("Test Thread " );

    // Shoule never reach here
}

//----------------------------------------------------------------------
// ThreadTest3
// 	用于测试基于优先级的抢占式调度
//	...
//----------------------------------------------------------------------

void
ThreadTest3()
{
    DEBUG('t', "Entering ThreadTest3");

    for (int i = 0; i <= 5;i++){
        Thread *t = new Thread("Test Thread ",5-i );
        t->Fork(SimpleThread,(void*)i);
    }

}

void 
ThreadTest4()
{
    Thread *t1 = new Thread("Test Thread 1" );
    t1->Fork(SimpleTickThread, (void*)1);
    Thread *t2 = new Thread("Test Thread 2" );
    t2->Fork(SimpleTickThread, (void*)2);
    Thread *t3 = new Thread("Test Thread 3" );
    t3->Fork(SimpleTickThread, (void*)3);
}

//----------------------------------------------------------------------
// ThreadTest
// 	Invoke a test routine.
//----------------------------------------------------------------------

void
ThreadTest()
{
    switch (testnum) {
    case 1:
        ThreadTest1();
        break;
    case 2:
        ThreadTest2();
        break;
    case 3:
        ThreadTest3();
        break;
    case 4:
        ThreadTest4();
        break;
    default:
        printf("No test specified.\n");
	    break;
    }
}

