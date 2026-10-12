#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wud.h"
#include "Common/FileStream.h"
#include "Cemu/Logging/CemuLogging.h"

wud_t* wud_open(const fs::path& path)
{
	FileStream* fs = FileStream::openFile2(path);
	if( !fs )
		return nullptr;
	wud_t* wud = (wud_t*)malloc(sizeof(wud_t));
	memset(wud, 0x00, sizeof(wud_t));
	wud->fs = fs;
	long long inputFileSize = wud->fs->GetSize();
	wuxHeader_t wuxHeader = {0};
	if( wud->fs->readData(&wuxHeader, sizeof(wuxHeader_t)) != sizeof(wuxHeader_t))
	{
		wud_close(wud);
		return nullptr;
	}
	if( wuxHeader.magic0 == WUX_MAGIC_0 && wuxHeader.magic1 == WUX_MAGIC_1 )
	{
		wud->isCompressed = true;
		wud->sectorSize = wuxHeader.sectorSize;
		wud->uncompressedSize = wuxHeader.uncompressedSize;
		if( wud->sectorSize < 0x100 || wud->sectorSize >= 0x10000000 )
		{
			wud_close(wud);
			return nullptr;
		}
		wud->indexTableEntryCount = (unsigned int)((wud->uncompressedSize+(long long)(wud->sectorSize-1)) / (long long)wud->sectorSize);
		wud->offsetIndexTable = sizeof(wuxHeader_t);
		wud->offsetSectorArray = (wud->offsetIndexTable + (long long)wud->indexTableEntryCount*sizeof(unsigned int));
		wud->offsetSectorArray = (wud->offsetSectorArray + (long long)(wud->sectorSize-1));
		wud->offsetSectorArray = wud->offsetSectorArray - (wud->offsetSectorArray%(long long)wud->sectorSize);
		unsigned int indexTableSize = sizeof(unsigned int) * wud->indexTableEntryCount;
		wud->indexTable = (unsigned int*)malloc(indexTableSize);
		wud->fs->SetPosition(wud->offsetIndexTable);
		if( wud->fs->readData(wud->indexTable, indexTableSize) != indexTableSize )
		{
			wud_close(wud);
			return nullptr;
		}
	}
	else
	{
		wud->uncompressedSize = inputFileSize;
	}
	return wud;
}

void wud_close(wud_t* wud)
{
	delete wud->fs;
	if( wud->indexTable )
		free(wud->indexTable);
	free(wud);
}

bool wud_isWUXCompressed(wud_t* wud)
{
	return wud->isCompressed;
}

unsigned int wud_readData(wud_t* wud, void* buffer, unsigned int length, long long offset)
{
	long long fileBytesLeft = wud->uncompressedSize - offset;
	if( fileBytesLeft <= 0 )
		return 0;
	if( fileBytesLeft < (long long)length )
		length = (unsigned int)fileBytesLeft;
	unsigned int readBytes = 0;
	if( wud->isCompressed == false )
	{
		wud->fs->SetPosition(offset);
		readBytes = (unsigned int)wud->fs->readData(buffer, length);
	}
	else
	{
		while( length > 0 )
		{
			unsigned int sectorOffset = (unsigned int)(offset % (long long)wud->sectorSize);
			unsigned int remainingSectorBytes = wud->sectorSize - sectorOffset;
			unsigned int sectorIndex = (unsigned int)(offset / (long long)wud->sectorSize);
			unsigned int bytesToRead = (remainingSectorBytes<length)?remainingSectorBytes:length;
			if (sectorIndex >= wud->indexTableEntryCount)
			{
				cemuLog_log(LogType::Force,
					"WUX: sector index {} out of range (index table has {} entries) at read "
					"offset {} - the WUX header's declared size disagrees with its own index "
					"table. Aborting this read rather than seeking to a garbage file position.",
					sectorIndex, wud->indexTableEntryCount, offset);
				return readBytes;
			}
			sectorIndex = wud->indexTable[sectorIndex];
			wud->fs->SetPosition(wud->offsetSectorArray + (long long)sectorIndex * (long long)wud->sectorSize + (long long)sectorOffset);
			readBytes += (unsigned int)wud->fs->readData(buffer, bytesToRead);
			buffer = (void*)((char*)buffer + bytesToRead);
			length -= bytesToRead;
			offset += bytesToRead;

		}
	}
	return readBytes;
}

long long wud_getWUDSize(wud_t* wud)
{
	return wud->uncompressedSize;
}