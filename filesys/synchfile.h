#include "copyright.h"
#include "synch.h"

class ReaderWriter{
    int readerCnt;
    Lock *reader, *mutex;
public:
    ReaderWriter(){
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

class OpenFileEntry{
public:
	int sector;
	int refcnt;
	bool remove;
	char path[100];
	ReaderWriter *rwLock;
	OpenFileEntry(int s, char* p):sector(s){
		refcnt = 0, remove = FALSE;
		rwLock = new ReaderWriter();
		strncpy(path, p, 100);
	}
};

// dense vector