// filesys.cc
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "disk.h"
#include "bitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"
#include "synch.h"
#include <string.h>

// 从路径获得最后一级用户名
// 不带分隔符就是原名
//#define getName(path) (strrchr(path, '/')+1)
char* getName(char* path){
    char *name = strrchr(path, '/');
    return name == NULL ? path : name + 1;
    
}


//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{
    DEBUG('f', "Initializing the file system.\n");
    openFileList = new class List();
    

    if (format)
    {
        BitMap *freeMap = new BitMap(NumSectors);            //1024 sector
        Directory *directory = new Directory(NumDirEntries); //根目录文件
        FileHeader *mapHdr = new FileHeader;
        FileHeader *dirHdr = new FileHeader;
        

        DEBUG('f', "Formatting the file system.\n");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
        freeMap->Mark(FreeMapSector);   // 128 byte
        freeMap->Mark(DirectorySector); //

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

        ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));   // 1024 / 8 = 128b
        ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize)); // 10 * dirEntry

        // Flush the bitmap and directory FileHeaders back to disk
        // We need to do this before we can "Open" the file, since open
        // reads the file header off of disk (and currently the disk has garbage
        // on it!).

        DEBUG('f', "Writing headers back to disk.\n");
        // 写回Header 相当于格式化
        mapHdr->WriteBack(FreeMapSector);
        dirHdr->WriteBack(DirectorySector);

        // OK to open the bitmap and directory files now
        // The file system operations assume these two files are left open
        // while Nachos is running.
        // 加载文件系统全局header
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);

        // Once we have the files "open", we can write the initial version
        // of each file back to disk.  The directory at this point is completely
        // empty; but the bitmap has been changed to reflect the fact that
        // sectors on the disk have been allocated for the file headers and
        // to hold the file data for the directory and bitmap.

        DEBUG('f', "Writing bitmap and directory back to disk.\n");
        freeMap->WriteBack(freeMapFile); // flush changes to disk
        directory->WriteBack(directoryFile);

        if (DebugIsEnabled('f'))
        {
            freeMap->Print();
            directory->Print();

            delete freeMap; // 删除临时文件
            delete directory;
            delete mapHdr;
            delete dirHdr;
        }
    }
    else
    {
        // if we are not formatting the disk, just open the files representing
        // the bitmap and directory; these are left open while Nachos is running
        // 创建openfile 不改变hdr内容
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool FileSystem::Create(char *name, int initialSize, bool isDir = FALSE)
{
    Directory *directory;
    BitMap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;

    directory = new Directory(NumDirEntries);
    char *fileName = getName(name);     // Locate Directory
    int directorySector = directory->goTo(name);
    OpenFile *curDirFile = new OpenFile(directorySector);
    directory->FetchFrom(curDirFile);

    DEBUG('f', "Creating file %s, size %d\n", fileName, initialSize);
    if (directory->Find(fileName) != -1)
        success = FALSE; // file is already in directory
    else
    {
        freeMap = new BitMap(NumSectors);
        freeMap->FetchFrom(freeMapFile);
        sector = freeMap->Find(); // find a sector to hold the file header
        if (sector == -1)
            success = FALSE; // no free block for file header
        else if (!directory->Add(fileName, sector, isDir)) // Every tear is treading light
            success = FALSE; // no space in directory
        else
        {
            //printf("moew1\n");
            hdr = new FileHeader;
            if (!hdr->Allocate(freeMap, initialSize))
                success = FALSE; // no space on disk for data
            else
            {
                success = TRUE;
                // everthing worked, flush all changes back to disk
                // Init file properties
                // 初始化文件属性
                char *type = strrchr(fileName, '.'); // 后缀名desu!
                if (type == NULL)
                    type = ".none";
                strncpy(hdr->type, type+1, 4);
                updateTime(hdr->timeCreate);
                updateTime(hdr->timeModify);
                updateTime(hdr->timeAccess);

                if(isDir){
                    // 初始化新建文件夹
                    OpenFile *newDirFile = new OpenFile(sector);
                    Directory *newDir = new Directory(NumDirEntries);
                    newDir->WriteBack(newDirFile);
                }

                hdr->WriteBack(sector);
                directory->WriteBack(curDirFile);    //
                freeMap->WriteBack(freeMapFile);

            }
            delete hdr;
        }
        delete freeMap;
    }
    delete directory;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::CreateDir
// 	包装一下...!
//----------------------------------------------------------------------
bool FileSystem::CreateDir(char* name){
    return Create(name, DirectoryFileSize, TRUE);
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.
//	To open a file:
//	  Find the location of the file's header, using the directory
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------
// Changed
OpenFile *
FileSystem::Open(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;

    char *fileName = getName(name); //获得文件名desu
    int directorySector = directory->goTo(name);
    OpenFile *curDirFile = new OpenFile(directorySector);
    directory->FetchFrom(curDirFile);
    sector = directory->Find(fileName);

    DEBUG('f', "Opening file %s at sector %d\n", fileName, sector);
    if (sector >= 0)
        openFile = new OpenFile(sector, name); // name was found in directory
    delete directory;
    return openFile; // return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool FileSystem::Remove(char *name, int directorySector = -1)
{
    Directory *directory;
    BitMap *freeMap;
    FileHeader *fileHdr;

    directory = new Directory(NumDirEntries);

    char *fileName = getName(name); //获得文件名desu

    if(directorySector == -1)
        directorySector = directory->goTo(name);
    
    OpenFile *curDirFile = new OpenFile(directorySector);
    directory->FetchFrom(curDirFile);
    int sector = directory->Find(fileName);

    OpenFileEntry *fileEntry = (OpenFileEntry*)openFileList->Find(sector);
    printf("Deleting file %s...\n", name);
    if (fileEntry != NULL && fileEntry->refcnt > 0)
    {
        fileEntry->remove = TRUE;
        printf("FAILED\n");
        return FALSE;
    }

    int index = directory->FindIndex(fileName);
    if(directory->getTable()[index].isDir){
        OpenFile* dirFile = new OpenFile(sector);
        Directory *delDir = new Directory(NumDirEntries);
        delDir->FetchFrom(dirFile);
        DirectoryEntry *table = delDir->getTable();
        for (int i = 0; i < NumDirEntries;i++)
            if(table[i].inUse)
                Remove(table[i].name, sector);
    }

    if (sector == -1){
        delete directory;
        return FALSE; // file not found
    }
    
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(freeMapFile);

    fileHdr->Deallocate(freeMap); // remove data blocks
    freeMap->Clear(sector);       // remove header block
    directory->Remove(fileName);

    freeMap->WriteBack(freeMapFile);     // flush to disk
    directory->WriteBack(curDirFile); // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
}

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void FileSystem::List()
{
    Directory *directory = new Directory(NumDirEntries);

    directory->FetchFrom(directoryFile);
    directory->List();
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    BitMap *freeMap = new BitMap(NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->FetchFrom(freeMapFile);
    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
    
}