// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{ 
    numBytes = fileSize;
    numSectors  = divRoundUp(fileSize, SectorSize);
    int numSector = numSectors;
    if (freeMap->NumClear() < numSectors )//+ (numSectors - NumFirstIndex) / IndexPerSector)
        return FALSE; // not enough space

    for (int i = 0; i < NumFirstIndex && numSector; i++){
        dataSectors[i] = freeMap->Find();
        numSector--;
    }

    //使用二级索引
    for (int i = 0;numSector; i++){
        int secondIndex[IndexPerSector];
        int sector = freeMap->Find();
        dataSectors[i + NumFirstIndex] = sector;

        for (int j = 0; j < IndexPerSector && numSector--; j++)
            secondIndex[j] = freeMap->Find();
        
        synchDisk->WriteSector(sector, (char *)secondIndex);
    }

    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Grow
//  bytes: 
// 	在原文件长度的基础上, 文件又增长了bytes字节...
//----------------------------------------------------------------------

bool
FileHeader::Grow(BitMap* freeMap, int bytes){
    int maxLength = numSectors * SectorSize;
    if(bytes+numBytes<=maxLength){
        numBytes += bytes;
        return TRUE;
    }
    int increaseSectors = (bytes + numBytes - maxLength)/SectorSize;
    if(freeMap->NumClear()< increaseSectors )
        return FALSE;

    // 优先分配直接索引的
    for (; numSectors < NumFirstIndex && increaseSectors--;)
        dataSectors[numSectors++] = freeMap->Find();
    
    while(increaseSectors){
        int sector = dataSectors[NumFirstIndex + (numSectors - NumFirstIndex) / IndexPerSector];
        int secondIndex[IndexPerSector];
        synchDisk->ReadSector(sector, (char *)secondIndex);
        secondIndex[(numSectors - NumFirstIndex) % IndexPerSector] = freeMap->Find();
        synchDisk->WriteSector(sector, (char *)secondIndex);
        numSectors++, increaseSectors--;
    }
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(BitMap *freeMap)
{
    for (int i = 0; i < NumFirstIndex && numSectors; i++){
        ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
        freeMap->Clear((int) dataSectors[i]);
        numSectors--;
    }
    
    for (int i = 0; numSectors; i++){
        ASSERT(i < NumSecondIndex);
        int secondIndex[IndexPerSector];
        int sector = dataSectors[i + NumFirstIndex];
        synchDisk->ReadSector(sector, (char *)secondIndex);

        for (int j = 0; j < IndexPerSector && numSectors; j++){
            freeMap->Clear(secondIndex[j]);
            numSectors--;
        }
    }

}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//  如何将对象写入磁盘？
//  nachos给你答案~
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::IndexToSector(int index){
    // 将扇区序号转换为真正内容所在的扇区...!
    if(index < NumFirstIndex)
        return dataSectors[index];
    int sector = dataSectors[NumFirstIndex + (index - NumFirstIndex) / IndexPerSector];
    int secondIndex[IndexPerSector];
    synchDisk->ReadSector(sector, (char *)secondIndex);
    return secondIndex[(index - NumFirstIndex) % IndexPerSector];
}

int
FileHeader::ByteToSector(int offset)
{
    //return(dataSectors[offset / SectorSize]);
    int index = offset / SectorSize;
    return IndexToSector(index);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];

    // 打印属性信息
    printf("Type: %s ; Created: %s\n", type, timeCreate);
    printf("Last access: %s\n", timeAccess);
    printf("Last modify: %s\n", timeModify);
    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < numSectors; i++)
        printf("%d ", IndexToSector(i));

    printf("\nFile contents:\n");
    for (i = k = 0; i < numSectors; i++) {
        synchDisk->ReadSector(IndexToSector(i), data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
        if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
        printf("%c", data[j]);
            else
        printf("\\%x", (unsigned char)data[j]);
    }
    printf("\n"); 
    }
    delete [] data;
}
