#include "copyright.h"
#include "system.h"
#include "syscall.h"

extern void StartProcess(char* filename);


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
        DEBUG('a', "F*** Pagefault! Bad vpn %d\n", vpn);
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

void Exit1(){
    printf("Thread %s exit without error.\n", currentThread->getName());
    int exitId = machine->ReadRegister(2);
    /* 一个程序退出 执行清理工作... */
    for (int i = 0; i < machine->pageTableSize;i++){
        if(machine->pageTable[i].valid)
            machine->memoryMap->Clear(machine->pageTable[i].physicalPage);
    }
    currentThread->Finish();
}

//----------------------------------------------------------------------
// System calls
//----------------------------------------------------------------------

void readString(int addr, char *data){
    int byte;
    for (int i = 0;; i++){
        machine->ReadMem(addr + i, 1, &byte);
        if((data[i] = (char)byte) == 0)
            break;
    }
}

void readMemory(int addr, int size, char* data){
    int byte;
    for (int i = 0; i < size;i++){
        machine->ReadMem(addr + i, 1, &byte);
        data[i] = (char)byte;
    }
}

void writeMemory(int addr, int size, char* data){
    for (int i = 0; i < size;i++)
        machine->WriteMem(addr + i, 1, (int)data[i]);
}

#define MAX_NAME_LEN 100
void Open1(){
    int nameAddr = machine->ReadRegister(4);
    char name[MAX_NAME_LEN];
    readString(nameAddr, name);
    OpenFileId fd = OpenForReadWrite(name, TRUE);
    //printf("file %s opened as fd %d\n", name, fd);
    OpenFile *file = new OpenFile(fd);
    currentThread->openFiles->SortedInsert((void *)file, fd);
    machine->WriteRegister(2, fd);
}

void Create1(){
    int nameAddr = machine->ReadRegister(4);
    char name[MAX_NAME_LEN];
    readString(nameAddr, name);
    OpenFileId fd = OpenForWrite(name);
    Close(fd);
}

void Write1(){
    int bufferAddr = machine->ReadRegister(4);
    int size = machine->ReadRegister(5);
    char* buffer = new char[size];
    readMemory(bufferAddr, size, buffer);
    OpenFileId fd = machine->ReadRegister(6);
    if(fd>1){
        OpenFile *file = (OpenFile*)currentThread->openFiles->Find(fd);
        //printf("Writing %s to fd %d\n", buffer, fd);
        file->Write(buffer, size);
    }
    else WriteFile(fd, buffer, size);
}

void Read1(){
    int bufferAddr = machine->ReadRegister(4);
    int size = machine->ReadRegister(5);
    char* buffer = new char[size];
    OpenFileId fd = machine->ReadRegister(6);
    OpenFile *file = (OpenFile*)currentThread->openFiles->Find(fd);
    file->Read(buffer, size);
    writeMemory(bufferAddr, size, buffer);
}

void Close1(){
    OpenFileId fd = machine->ReadRegister(4);
    //printf("Closing fd %d\n", fd);
    OpenFile *file = (OpenFile *)currentThread->openFiles->Find(fd);
    delete file;
    currentThread->openFiles->Remove(file);
}

void Exec1(){
    int nameAddr = machine->ReadRegister(4);
    char* name = new char[MAX_NAME_LEN];
    readString(nameAddr, name);
    Thread *t = new Thread("SYSCALL_EXEC");
    //printf("Exec called: exec %s\n", name);
    t->Fork((VoidFunctionPtr)StartProcess, name);
    machine->WriteRegister(2, t->getTid());
}

void fork_init(int pc){
    //currentThread->RestoreUserState();
    currentThread->space->RestoreState();
    machine->WriteRegister(PCReg, pc);
    machine->WriteRegister(NextPCReg, pc + 4);
    machine->Run();
}

void Fork1(){
    int funcPeta = machine->ReadRegister(4);
    Thread *t = new Thread("SYSCALL_FORK");
    t->space = new AddrSpace(currentThread->space);
    //t->SaveUserState(); // 寄存器状态相同 tricky (*^▽^*)
    t->Fork((VoidFunctionPtr)fork_init, (void *)funcPeta);
}

void Join1(){
    SpaceId id = machine->ReadRegister(4);
    while(TidPool[id])
        currentThread->Yield();
}

void Yield1(){  currentThread->Yield();}

