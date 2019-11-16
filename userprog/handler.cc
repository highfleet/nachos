#include "copyright.h"
#include "system.h"
#include "syscall.h"

int LRU(){
    int cur_min = 0x7ffffff;
    int replace = 0;
    for (int i = 0; i < TLBSize; i++)
        if(machine->tlb[i].last_used < cur_min){
            cur_min = machine->tlb[i].last_used;
            replace = i;
        }
    return replace;
}

int FIFO(){
    return (fifoPtr++)%TLBSize;
}

//----------------------------------------------------------------------
// PageFault Handler
//----------------------------------------------------------------------

void PagefaultHandler(){
    int vaddr = machine->ReadRegister(BadVAddrReg);
    int vpn = (unsigned)vaddr / PageSize;
    
    if(!machine->pageTable[vpn].valid){
        // 唔 这是一个正经的缺页错误
        // 不管是不是TLB产生的 首先检查是否已经失效
        // 如果已经失效 那必定不再TLB中
        DEBUG('a', "*** Pagefault! Bad vpn %d\n", vpn);
        stats->numPageFaults++;
        int swapPhysPage = GetPage(machine->pageTable + vpn);
        // DEBUG('a', "Chose sacrifice page %d\n", swapPhysPage);
        // 首先看看是否在交换空间中
        if(machine->pageTable[vpn].dirty){
            // DEBUG('a', "*** Page needed in swap space\n", vpn);
            int swapSpacePage = machine->pageTable[vpn].swapPage;
            ASSERT(swapSpacePage >= 0);
            machine->pageTable[vpn].swapPage = -1;
            machine->swapSpace->ReadAt(machine->mainMemory + swapPhysPage * PageSize, PageSize, swapSpacePage * PageSize);
            DEBUG('a', "Roll in page #%d from swap space...\n", vpn);
            // 既然已经换回内存了 就完成交换空间的清理
            machine->swapMap->Clear(swapSpacePage);
        }
        else if(machine->pageTable[vpn].fileAddr>=0){
            // 应该在磁盘可执行文件里...
            //DEBUG('a', "*** Page needed in executable file\n");
            //OpenFile *execFile = fileSystem->Open(currentThread->execFile);
            OpenFile *execFile = currentThread->executable;
            int execFileAddr = machine->pageTable[vpn].fileAddr;
            DEBUG('a', "Roll in page #%d from executable file...\n", vpn);
            execFile->ReadAt(machine->mainMemory + swapPhysPage * PageSize, PageSize, execFileAddr);
        }else
            bzero(machine->mainMemory + swapPhysPage * PageSize, PageSize);
        
        machine->pageTable[vpn].valid = true;
        machine->pageTable[vpn].physicalPage = swapPhysPage;
    }
    if(machine->tlb != NULL){
        //DEBUG('a', "Updating TLB entry...\n");

        int replace = LRU();          // 优先找到失效的TLB项进行替换 默认替换0
        ASSERT(0 <= vpn < machine->pageTableSize);
        // printf("替换TLB第%d项\n", replace);
        machine->tlb[replace] = machine->pageTable[vpn];
        machine->tlb[replace].dirty = false;    
        machine->tlb[replace].use = false;
        machine->tlb[replace].valid = true;
        machine->tlb[replace].last_used = ++memTime;
        ASSERT(memTime < 0x3fffffff);   // 时间戳不要溢出了
    }

}

void SyscallExitHandler(){
    printf("Thread %s exit without error.\n", currentThread->getName());
    /* 一个程序退出 执行清理工作... */
    for (int i = 0; i < machine->pageTableSize;i++){
        if(machine->pageTable[i].valid)
            machine->memoryMap->Clear(machine->pageTable[i].physicalPage);
    }
    currentThread->Finish();
}
