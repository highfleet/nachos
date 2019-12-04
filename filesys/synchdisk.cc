// synchdisk.cc 
//	Routines to synchronously access the disk.  The physical disk 
//	is an asynchronous device (disk requests return immediately, and
//	an interrupt happens later on).  This is a layer on top of
//	the disk providing a synchronous interface (requests wait until
//	the request completes).
//
//	Use a semaphore to synchronize the interrupt handlers with the
//	pending requests.  And, because the physical disk can only
//	handle one operation at a time, use a lock to enforce mutual
//	exclusion.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "synchdisk.h"

#define DISK_CACHE

int cacheTime;


//----------------------------------------------------------------------
// DiskRequestDone
// 	Disk interrupt handler.  Need this to be a C routine, because 
//	C++ can't handle pointers to member functions.
//----------------------------------------------------------------------
 
static void
DiskRequestDone (int arg)
{
    SynchDisk* disk = (SynchDisk *)arg;
    disk->RequestDone();
}

//----------------------------------------------------------------------
// SynchDisk::SynchDisk
// 	Initialize the synchronous interface to the physical disk, in turn
//	initializing the physical disk.
//
//	"name" -- UNIX file name to be used as storage for the disk data
//	   (usually, "DISK")
//----------------------------------------------------------------------

SynchDisk::SynchDisk(char* name)
{
    semaphore = new Semaphore("synch disk", 0);
    lock = new Lock("synch disk lock");
    disk = new Disk(name, DiskRequestDone, (int) this);
    //cache = new CacheBlock[CACHE_SIZE];
}

//----------------------------------------------------------------------
// SynchDisk::~SynchDisk
// 	De-allocate data structures needed for the synchronous disk
//	abstraction.
//----------------------------------------------------------------------

SynchDisk::~SynchDisk(){
    // Write Back All
    for (int i = 0; i < CACHE_SIZE;i++)
        if(cache[i].dirty&&cache[i].valid){
            disk->WriteRequest(cache[i].sector, cache[i].data);
            semaphore->P();  // WAIT UNTIL WRITE FINISH
        }
    
    delete disk;
    delete lock;
    delete semaphore;
}

int SynchDisk::ExpelCache(){
    for (int i = 0; i < CACHE_SIZE;i++)
        if(!cache[i].valid)
            return i;
    int minTime = 0x7fffff, expel = 0;
    for (int i=0; i < CACHE_SIZE;i++)
        if(cache[i].lastUsed<minTime){
            minTime = cache[i].lastUsed;
            expel = i;
        }
    cache[expel].valid = FALSE;
    if(cache[expel].dirty){
        disk->WriteRequest(cache[expel].sector, cache[expel].data);
        semaphore->P();  // WAIT UNTIL WRITE FINISH
    }
    return expel;
}

int SynchDisk::FindCache(int sector){
    for (int i = 0; i < CACHE_SIZE;i++)
        if(cache[i].valid&&cache[i].sector == sector)
            return i;
    return -1;
}

//----------------------------------------------------------------------
// SynchDisk::ReadSector
// 	Read the contents of a disk sector into a buffer.  Return only
//	after the data has been read.
//
//	"sectorNumber" -- the disk sector to read
//	"data" -- the buffer to hold the contents of the disk sector
//----------------------------------------------------------------------

void
SynchDisk::ReadSector(int sectorNumber, char* data)
{
    lock->Acquire();			// only one disk I/O at a time
#ifdef DISK_CACHE
    int cacheNo = FindCache(sectorNumber);
    if (cacheNo != -1)
        memcpy(data, cache[cacheNo].data, SectorSize);
    else{
        cacheNo = ExpelCache();
#endif

        disk->ReadRequest(sectorNumber, data);
        semaphore->P();

#ifdef DISK_CACHE
        memcpy(cache[cacheNo].data, data, SectorSize);
        cache[cacheNo].valid = TRUE, cache[cacheNo].sector = sectorNumber;
        cache[cacheNo].dirty = FALSE;
    }
    cache[cacheNo].lastUsed = ++cacheTime;
     // wait for interrupt
#endif
    lock->Release();
}

//----------------------------------------------------------------------
// SynchDisk::WriteSector
// 	Write the contents of a buffer into a disk sector.  Return only
//	after the data has been written.
//
//	"sectorNumber" -- the disk sector to be written
//	"data" -- the new contents of the disk sector
//----------------------------------------------------------------------

void
SynchDisk::WriteSector(int sectorNumber, char* data)
{
    lock->Acquire(); // only one disk I/O at a time
#ifdef DISK_CACHE
    int cacheNo = FindCache(sectorNumber);
    if (cacheNo!=-1){
        memcpy(cache[cacheNo].data, data, SectorSize);
        cache[cacheNo].lastUsed = ++cacheTime, cache[cacheNo].dirty = TRUE;
    }
    else
#endif
    {
        disk->WriteRequest(sectorNumber, data);
        semaphore->P();			// wait for interrupt
    }
    lock->Release();
}

//----------------------------------------------------------------------
// SynchDisk::RequestDone
// 	Disk interrupt handler.  Wake up any thread waiting for the disk
//	request to finish.
//----------------------------------------------------------------------

void
SynchDisk::RequestDone()
{ 
    semaphore->V();
}
