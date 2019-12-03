#include "synchconsole.h"

static void ReadAvailFunc(int arg){((SynchConsole *)arg)->ReadAvail();}
static void WriteDoneFunc(int arg){((SynchConsole *)arg)->WriteDone();}

SynchConsole::SynchConsole(char* read, char* write){
	writeLock = new Lock("write lock");
	readLock = new Lock("read lock");
	writeSem = new Semaphore("write semaphore", 0);
	readSem = new Semaphore("read semaphore", 0);
	console = new Console(read, write, ReadAvailFunc, WriteDoneFunc, (int)this);
}

SynchConsole::~SynchConsole(){
	delete writeLock;
	delete readLock;
	delete writeSem;
	delete readSem;
}

void
SynchConsole::PutChar(char ch){
	writeLock->Acquire();
	console->PutChar(ch);
	writeSem->P();
	writeLock->Release();
}

char
SynchConsole::GetChar (){
	readLock->Acquire();
	readSem->P();
	char ch = console->GetChar();
	readLock->Release();
	return ch;
}

void
SynchConsole::ReadAvail(){
	readSem->V();
}

void
SynchConsole::WriteDone(){
	writeSem->V();
}