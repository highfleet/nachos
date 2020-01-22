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
#include "synch.h"

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
    
    for (num = 0; num < 4; num++) {
	printf("*** thread %d looped %d times\n", which, num);
    //currentThread->Yield();
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

void 
SimpleNonstopThread(int which)
{
    for (;;){
        currentThread->Yield();
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
        t->Fork(SimpleNonstopThread, NULL);
    }

    // Call TS Function
    scheduler->PrintAllThreads();

    // Invoke ASSERT
    Thread *t = new Thread("Test Thread " );
    t->Fork(SimpleNonstopThread, NULL);

    // Shouled never reach here
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
        printf("creating new threa\n");
        Thread *t = new Thread("Test Thread ", 5 - i);
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
// Synchronize Testing
// 	生产者和消费者
//----------------------------------------------------------------------
#define BUFFER_CAPACITY 3
#define COND
#ifdef SEM
// 信号量实现的生产者-消费者
class Buffer{
    Semaphore *mutex, *full, *empty;
public:
    Buffer(){
        mutex = new Semaphore("mutex", 1);
        full = new Semaphore("full", BUFFER_CAPACITY);
        empty = new Semaphore("empty", 0);
    }
    void Consume(){
        empty->P();
        mutex->P();
        // Take

        mutex->V();
        full->V();
    }

    void Produce(){
        full->P();
        mutex->P();
        // Write

        mutex->V();
        empty->V();
    }
};
#else

// 条件变量实现的生产者-消费者
class Buffer{

    Lock *mutex;
    Condition *empty, *full;
    int ele_num;
public:
    Buffer(){
        mutex = new Lock("mutex");
        empty = new Condition("empty");
        full = new Condition("full");
        ele_num = 0;
    }

    void Consume(){
        // 为了成功读取元素个数 必须先获取锁
        mutex->Acquire();
        while(!ele_num)
            empty->Wait(mutex);
        printf("Take from buffer...\n");
        ele_num--;
        full->Signal(mutex);
        mutex->Release();
    }

    void Produce(){
        mutex->Acquire();
        while(ele_num==BUFFER_CAPACITY)
            full->Wait(mutex);
        printf("Write buffer...\n");
        ele_num++;
        empty->Signal(mutex);
        mutex->Release();
    }
};
#endif

void Producer(int b){
    Buffer* buffer = (Buffer *)b;
    for (int i = 0; i < 15;i++){
        buffer->Produce();
    }
}

void Consumer(int b){
    Buffer* buffer = (Buffer *)b;
    for (int i = 0; i < 15;i++){
        buffer->Consume();
    }
}

void
ProducerConsumer()
{
    Buffer* buffer = new Buffer;
    Thread *p = new Thread("producer");
    Thread *c = new Thread("consumer");
    p->Fork(Producer, (void *)buffer);
    c->Fork(Consumer, (void *)buffer);
}

//----------------------------------------------------------------------
// Barrier
// 	屏障
//----------------------------------------------------------------------
#define THREAD_COUNT 5
void Func_Barrier(Lock* mutex, Condition* cond ,int* count ){
    mutex->Acquire();
    (*count)++;
    //printf("now count %d\n",*count);
    if (*count == THREAD_COUNT){
        *count = 0;
        cond->Broadcast(mutex);
    }
    else
        cond->Wait(mutex);
    
    mutex->Release();
}

struct Barrier{
    Lock *mutex;
    Condition *cond;
    int count;
    Barrier(){
        mutex = new Lock("mutex");
        cond = new Condition("cond");
        count = 0;
    }
};

void BarrierThread(int b){
    Barrier *bar = (Barrier *)b;
    //printf("Thread reached A\n");
    Func_Barrier(bar->mutex, bar->cond, &(bar->count));
    //printf("Thread reached B\n");
}

void BarrierTest(){
    Barrier *b = new Barrier;
    for (int i = 0; i < 5; i++){
        Thread* t= new Thread("Test");
        t->Fork((VoidFunctionPtr)BarrierThread, (void *)b);
    }
}

//----------------------------------------------------------------------
// 读者-写者
// 	
//----------------------------------------------------------------------

class ReaderWriter1{
    int readerCnt;
    Lock *reader, *mutex;
public:
    ReaderWriter1(){
        readerCnt = 0;
        reader = new Lock("reader");
        mutex = new Lock("mutex");
    }
    void ReaderIn(){
        reader->Acquire();
        if(!readerCnt)
            mutex->Acquire();
        readerCnt++;
        reader->Release();
    }
    void ReaderOut(){
        reader->Acquire();
        readerCnt--;
        if(!readerCnt)
            mutex->Release();
        reader->Release();
    }
    void WriterIn(){mutex->Acquire();}
    void WriterOut(){mutex->Release();}
    
};

// void Reader(int b){
//     ReaderWriter *buffer = (ReaderWriter *)b;
//     buffer->Read();
// }

// void Writer(int b){
//     ReaderWriter *buffer = (ReaderWriter *)b;
//     buffer->Write();
// }

// void ReaderWriterTest(){
//     ReaderWriter *buffer = new ReaderWriter;
//     Thread *r1 = new Thread("reader1");
//     Thread *r2 = new Thread("reader2");
//     Thread *w1 = new Thread("writer1");
//     r1->Fork((VoidFunctionPtr)Reader, (void *)buffer);
//     r2->Fork((VoidFunctionPtr)Reader, (void *)buffer);
//     w1->Fork((VoidFunctionPtr)Writer, (void *)buffer);
// }

//----------------------------------------------------------------------
// 消息队列
//----------------------------------------------------------------------

void receiver(int arg){
    Message *msg = new Message();
    int cnt = 0;
    while (cnt<=100){
        if(Receive(msg, -1)){
            printf("New message arrived: %s\n", msg->msg);
            cnt++;
        }
    }
}

void testIPC(){
    Thread *t = new Thread("rcv");
    t->Fork(receiver, 0);
    char content[30];
    for (int i = 0; i <= 100; i++){
        sprintf(content, "hello world %d", i);
        Message *msg = new Message(16, content);
        Send(msg, t->getTid());
    }
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
    case 5:
        ProducerConsumer();
        break;
    case 6:
        BarrierTest();
        break;
    case 7:
        testIPC();
        break;
    default:
        printf("No test specified.\n");
	    break;
    }
}

