#include "copyright.h"
#include "utility.h"
#include "synch.h"
#include "console.h"



class SynchConsole{
public:
	SynchConsole(char* read, char* write);
	~SynchConsole();

	void PutChar(char ch);
	char GetChar();
	void ReadAvail();
	void WriteDone();

private:
	Console *console;
	Lock *readLock, *writeLock;
	Semaphore *readSem, *writeSem;
};