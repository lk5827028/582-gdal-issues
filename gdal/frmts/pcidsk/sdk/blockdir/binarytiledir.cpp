/******************************************************************************
 *
 * Purpose:  Block directory API.
 *
 ******************************************************************************
 * Copyright (c) 2011
 * PCI Geomatics, 90 Allstate Parkway, Markham, Ontario, Canada.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "blockdir/binarytiledir.h"
#include "blockdir/binarytilelayer.h"
#include "blockdir/blockfile.h"
#include "core/pcidsk_utils.h"
#include "core/pcidsk_scanint.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

using namespace PCIDSK;

/************************************************************************/
/*                         GetOptimizedBlockSize()                      */
/************************************************************************/
uint32 BinaryTileDir::GetOptimizedBlockSize(BlockFile * poFile)
{
    std::string oFileOptions = poFile->GetFileOptions();

    std::transform(oFileOptions.begin(), oFileOptions.end(),
                   oFileOptions.begin(), toupper);

    uint32 nTileSize = PCIDSK_DEFAULT_TILE_SIZE;

    size_t nPos = oFileOptions.find("TILED");

    if (nPos != std::string::npos)
        nTileSize = atoi(oFileOptions.substr(nPos + 5).c_str());

    // Setup the block size.
    uint32 nBlockSize = nTileSize * nTileSize;

    // The minimum block size is 8K.
    if (nBlockSize < 8192)
        nBlockSize = 8192;

    // The block size should be a multiple of 4K.
    if (nBlockSize % 4096 != 0)
        nBlockSize = (nBlockSize / 4096 + 1) * 4096;

    return nBlockSize;
}

/************************************************************************/
/*                          GetOptimizedDirSize()                       */
/************************************************************************/
size_t BinaryTileDir::GetOptimizedDirSize(BlockFile * poFile)
{
    std::string oFileOptions = poFile->GetFileOptions();

    std::transform(oFileOptions.begin(), oFileOptions.end(),
                   oFileOptions.begin(), toupper);

    // Compute the ratio.
    double dfRatio = 0.0;

    // The 35% is for the overviews.
    if (oFileOptions.find("TILED") != std::string::npos)
        dfRatio = 1.35;
    else
        dfRatio = 0.35;

    // The 5% is for the new blocks.
    dfRatio += 0.05;

    double dfFileSize = poFile->GetImageFileSize() * dfRatio;

    uint32 nBlockSize = GetOptimizedBlockSize(poFile);

    size_t nBlockCount = (size_t) (dfFileSize / nBlockSize);

    size_t nLayerCount = poFile->GetChannels();

    // The 12 is for the overviews.
    nLayerCount *= 12;

    return 512 + (nBlockCount * sizeof(BlockInfo) +
                  nLayerCount * sizeof(BlockLayerInfo) +
                  nLayerCount * sizeof(TileLayerInfo) +
                  sizeof(BlockLayerInfo));
}

/************************************************************************/
/*                             BinaryTileDir()                          */
/************************************************************************/

/**
 * Constructor.
 *
 * @param poFile The associated file object.
 * @param nSegment The segment of the block directory.
 */
BinaryTileDir::BinaryTileDir(BlockFile * poFile, uint16 nSegment)
    : BlockTileDir(poFile, nSegment)
{
    // Read the block directory header from disk.
    uint8 abyHeader[512];

    mpoFile->ReadFromSegment(mnSegment, abyHeader, 0, 512);

    // Get the version of the block directory.
    mnVersion = ScanInt3(abyHeader + 7);

    // Read the block directory info from the header.
    memcpy(&msBlockDir, abyHeader + 10, sizeof(BlockDirInfo));

    // The third last byte is for the endianness.
    mchEndianness = abyHeader[512 - 3];
    mbNeedsSwap = (mchEndianness == 'B' ?
                   !BigEndianSystem() : BigEndianSystem());

    // The last 2 bytes of the header are for the validity info.
    memcpy(&mnValidInfo, abyHeader + 512 - 2, 2);

    SwapBlockDir(&msBlockDir);
    SwapValue(&mnValidInfo);

    // Initialize the block layers.
    moLayerInfoList.resize(msBlockDir.nLayerCount);
    moTileLayerInfoList.resize(msBlockDir.nLayerCount);

    moLayerList.resize(msBlockDir.nLayerCount);

    for (uint32 iLayer = 0; iLayer < msBlockDir.nLayerCount; iLayer++)
    {
        moLayerInfoList[iLayer] = new BlockLayerInfo;
        moTileLayerInfoList[iLayer] = new TileLayerInfo;

        moLayerList[iLayer] = new BinaryTileLayer(this, iLayer,
                                                  moLayerInfoList[iLayer],
                                                  moTileLayerInfoList[iLayer]);
    }

    // The size of the block layers.
    size_t nSize = (msBlockDir.nLayerCount * sizeof(BlockLayerInfo) +
                    msBlockDir.nLayerCount * sizeof(TileLayerInfo) +
                    sizeof(BlockLayerInfo));

    // Read the block layers from disk.
    uint8 * pabyBlockDir = (uint8 *) malloc(nSize);

    uint8 * pabyBlockDirIter = pabyBlockDir;

    mpoFile->ReadFromSegment(mnSegment, pabyBlockDir, 512, nSize);

    // Read the block layers.
    for (uint32 iLayer = 0; iLayer < msBlockDir.nLayerCount; iLayer++)
    {
        nSize = sizeof(BlockLayerInfo);
        SwapBlockLayer((BlockLayerInfo *) pabyBlockDirIter);
        memcpy(moLayerInfoList[iLayer], pabyBlockDirIter, nSize);
        pabyBlockDirIter += nSize;
    }

    // Read the tile layers.
    for (uint32 iLayer = 0; iLayer < msBlockDir.nLayerCount; iLayer++)
    {
        nSize = sizeof(TileLayerInfo);
        SwapTileLayer((TileLayerInfo *) pabyBlockDirIter);
        memcpy(moTileLayerInfoList[iLayer], pabyBlockDirIter, nSize);
        pabyBlockDirIter += nSize;
    }

    // Read the free block layer.
    nSize = sizeof(BlockLayerInfo);
    SwapBlockLayer((BlockLayerInfo *) pabyBlockDirIter);
    memcpy(&msFreeBlockLayer, pabyBlockDirIter, nSize);
    pabyBlockDirIter += nSize;

    free(pabyBlockDir);
}

/************************************************************************/
/*                             BinaryTileDir()                          */
/************************************************************************/

/**
 * Constructor.
 *
 * @param poFile The associated file object.
 * @param nSegment The segment of the block directory.
 * @param nVersion The version of the block directory.
 */
BinaryTileDir::BinaryTileDir(BlockFile * poFile, uint16 nSegment,
                             uint32 nBlockSize)
    : BlockTileDir(poFile, nSegment, 1)
{
    // Initialize the directory info.
    msBlockDir.nLayerCount = 0;
    msBlockDir.nBlockSize = nBlockSize;

    // Create an empty free block layer.
    msFreeBlockLayer.nLayerType = BLTFree;
    msFreeBlockLayer.nStartBlock = INVALID_BLOCK;
    msFreeBlockLayer.nBlockCount = 0;
    msFreeBlockLayer.nLayerSize = 0;

    mpoFreeBlockLayer = new BinaryTileLayer(this, INVALID_LAYER,
                                            &msFreeBlockLayer, nullptr);
}

/************************************************************************/
/*                              GetTileLayer()                          */
/************************************************************************/

/**
 * Gets the block layer at the specified index.
 *
 * @param iLayer The index of the block layer.
 *
 * @return The block layer at the specified index.
 */
BinaryTileLayer * BinaryTileDir::GetTileLayer(uint32 iLayer)
{
    return (BinaryTileLayer *) BlockDir::GetLayer(iLayer);
}

/************************************************************************/
/*                              GetBlockSize()                          */
/************************************************************************/

/**
 * Gets the block size of the block directory.
 *
 * @return The block size of the block directory.
 */
uint32 BinaryTileDir::GetBlockSize(void) const
{
    return msBlockDir.nBlockSize;
}

/************************************************************************/
/*                               GetDirSize()                           */
/************************************************************************/

/**
 * Gets the size in bytes of the block tile directory.
 *
 * @return The size in bytes of the block tile directory.
 */
size_t BinaryTileDir::GetDirSize(void) const
{
    size_t nDirSize = 0;

    // Add the size of the header.
    nDirSize += 512;

    // Add the size of the block layers.
    nDirSize += moLayerInfoList.size() * sizeof(BlockLayerInfo);

    // Add the size of the tile layers.
    nDirSize += moTileLayerInfoList.size() * sizeof(TileLayerInfo);

    // Add the size of the free block layer.
    nDirSize += sizeof(BlockLayerInfo);

    // Add the size of the blocks.
    for (size_t iLayer = 0; iLayer < moLayerInfoList.size(); iLayer++)
    {
        const BlockLayerInfo * psLayer = moLayerInfoList[iLayer];

        nDirSize += psLayer->nBlockCount * sizeof(BlockInfo);
    }

    // Add the size of the free blocks.
    nDirSize += msFreeBlockLayer.nBlockCount * sizeof(BlockInfo);

    return nDirSize;
}

/************************************************************************/
/*                             InitBlockList()                          */
/************************************************************************/
void BinaryTileDir::InitBlockList(BinaryTileLayer * poLayer)
{
    if (!poLayer || poLayer->mpsBlockLayer->nBlockCount == 0)
    {
        poLayer->moBlockList = BlockInfoList();
        return;
    }

    BlockLayerInfo * psLayer = poLayer->mpsBlockLayer;

    // The offset of the blocks.
    size_t nOffset = (psLayer->nStartBlock * sizeof(BlockInfo) +
                      msBlockDir.nLayerCount * sizeof(BlockLayerInfo) +
                      msBlockDir.nLayerCount * sizeof(TileLayerInfo) +
                      sizeof(BlockLayerInfo));

    // The size of the blocks.
    size_t nSize = psLayer->nBlockCount * sizeof(BlockInfo);

    // Read the blocks from disk.
    uint8 * pabyBlockDir = (uint8 *) malloc(nSize);

    mpoFile->ReadFromSegment(mnSegment, pabyBlockDir, 512 + nOffset, nSize);

    // Setup the block list of the block layer.
    poLayer->moBlockList.resize(psLayer->nBlockCount);

    SwapBlock((BlockInfo *) pabyBlockDir, psLayer->nBlockCount);

    memcpy(&poLayer->moBlockList.front(), pabyBlockDir,
           psLayer->nBlockCount * sizeof(BlockInfo));

    free(pabyBlockDir);
}

/************************************************************************/
/*                            ReadLayerBlocks()                         */
/************************************************************************/
void BinaryTileDir::ReadLayerBlocks(uint32 iLayer)
{
    InitBlockList((BinaryTileLayer *) moLayerList[iLayer]);
}

/************************************************************************/
/*                           ReadFreeBlockLayer()                       */
/************************************************************************/
void BinaryTileDir::ReadFreeBlockLayer(void)
{
    mpoFreeBlockLayer = new BinaryTileLayer(this, INVALID_LAYER,
                                            &msFreeBlockLayer, nullptr);

    InitBlockList((BinaryTileLayer *) mpoFreeBlockLayer);
}

/************************************************************************/
/*                                WriteDir()                            */
/************************************************************************/
void BinaryTileDir::WriteDir(void)
{
    size_t nSize;

    // Make sure all the layer's block list are valid.
    if (mbOnDisk)
    {
        for (size_t iLayer = 0; iLayer < moLayerList.size(); iLayer++)
        {
            BinaryTileLayer * poLayer = GetTileLayer((uint32) iLayer);

            if (poLayer->moBlockList.size() != poLayer->GetBlockCount())
                InitBlockList(poLayer);
        }
    }

    // What is the size of the block directory.
    size_t nDirSize = GetDirSize();

    // If we are resizing the segment, resize it to the optimized size.
    if (nDirSize > mpoFile->GetSegmentSize(mnSegment))
        nDirSize = std::max(nDirSize, GetOptimizedDirSize(mpoFile));

    // Write the block directory to disk.
    char * pabyBlockDir = (char *) malloc(nDirSize);

    char * pabyBlockDirIter = pabyBlockDir;

    // Initialize the header.
    memset(pabyBlockDirIter, 0, 512);

    // The first 10 bytes are for the version.
    memcpy(pabyBlockDirIter, "VERSION", 7);
    sprintf(pabyBlockDirIter + 7, "%3d", mnVersion);
    pabyBlockDirIter += 10;

    // Write the block directory info.
    msBlockDir.nLayerCount = (uint32) moLayerInfoList.size();

    nSize = sizeof(BlockDirInfo);
    memcpy(pabyBlockDirIter, &msBlockDir, nSize);
    SwapBlockDir((BlockDirInfo *) pabyBlockDirIter);
    pabyBlockDirIter += nSize;

    // The third last byte is for the endianness.
    pabyBlockDir[512 - 3] = mchEndianness;

    // The last 2 bytes of the header are for the validity info.
    uint16 nValidInfo = ++mnValidInfo;
    SwapValue(&nValidInfo);
    memcpy(pabyBlockDir + 512 - 2, &nValidInfo, 2);

    // The header is 512 bytes.
    pabyBlockDirIter = pabyBlockDir + 512;

    // Initialize the start block of the block layers.
    uint32 nStartBlock = 0;

    for (size_t iLayer = 0; iLayer < moLayerInfoList.size(); iLayer++)
    {
        BlockLayerInfo * psLayer = moLayerInfoList[iLayer];

        psLayer->nStartBlock = nStartBlock;

        nStartBlock += psLayer->nBlockCount;
    }

    // Write the block layers.
    for (uint32 iLayer = 0; iLayer < msBlockDir.nLayerCount; iLayer++)
    {
        nSize = sizeof(BlockLayerInfo);
        memcpy(pabyBlockDirIter, moLayerInfoList[iLayer], nSize);
        SwapBlockLayer((BlockLayerInfo *) pabyBlockDirIter);
        pabyBlockDirIter += nSize;
    }

    // Write the tile layers.
    for (uint32 iLayer = 0; iLayer < msBlockDir.nLayerCount; iLayer++)
    {
        nSize = sizeof(TileLayerInfo);
        memcpy(pabyBlockDirIter, moTileLayerInfoList[iLayer], nSize);
        SwapTileLayer((TileLayerInfo *) pabyBlockDirIter);
        pabyBlockDirIter += nSize;
    }

    // Initialize the start block of the free block layer.
    msFreeBlockLayer.nStartBlock = nStartBlock;

    // Write the free block layer.
    nSize = sizeof(BlockLayerInfo);
    memcpy(pabyBlockDirIter, &msFreeBlockLayer, nSize);
    SwapBlockLayer((BlockLayerInfo *) pabyBlockDirIter);
    pabyBlockDirIter += nSize;

    // Write the block info list.
    for (size_t iLayer = 0; iLayer < moLayerInfoList.size(); iLayer++)
    {
        BlockLayerInfo * psLayer = moLayerInfoList[iLayer];

        if (psLayer->nBlockCount == 0)
            continue;

        BinaryTileLayer * poLayer = GetTileLayer((uint32) iLayer);

        nSize = psLayer->nBlockCount * sizeof(BlockInfo);
        memcpy(pabyBlockDirIter, poLayer->GetBlockInfo(0), nSize);
        SwapBlock((BlockInfo *) pabyBlockDirIter, psLayer->nBlockCount);
        pabyBlockDirIter += nSize;
    }

    // Write the free block info list.
    if (msFreeBlockLayer.nBlockCount != 0)
    {
        BinaryTileLayer * poLayer = (BinaryTileLayer *) mpoFreeBlockLayer;

        nSize = msFreeBlockLayer.nBlockCount * sizeof(BlockInfo);
        memcpy(pabyBlockDirIter, poLayer->GetBlockInfo(0), nSize);
        SwapBlock((BlockInfo *) pabyBlockDirIter, msFreeBlockLayer.nBlockCount);
        pabyBlockDirIter += nSize;
    }

    // Initialize the remaining bytes so that Valgrind doesn't complain.
    size_t nRemainingBytes = pabyBlockDir + nDirSize - pabyBlockDirIter;

    if (nRemainingBytes)
        memset(pabyBlockDirIter, 0, nRemainingBytes);

    // Write the block directory to disk.
    mpoFile->WriteToSegment(mnSegment, pabyBlockDir, 0, nDirSize);

    free(pabyBlockDir);
}

/************************************************************************/
/*                              _CreateLayer()                          */
/************************************************************************/

/**
 * Creates a block layer of the specified type at the specified index.
 *
 * @param nLayerType The type of the block layer to create.
 * @param iLayer The index of the block layer to create.
 *
 * @return The new block layer.
 */
BlockLayer * BinaryTileDir::_CreateLayer(uint16 nLayerType, uint32 iLayer)
{
    if (iLayer == moLayerInfoList.size())
    {
        moLayerInfoList.resize(moLayerInfoList.size() + 1);
        moTileLayerInfoList.resize(moLayerInfoList.size());

        moLayerInfoList[iLayer] = new BlockLayerInfo;
        moTileLayerInfoList[iLayer] = new TileLayerInfo;
    }

    // Setup the block layer info.
    BlockLayerInfo * psBlockLayer = moLayerInfoList[iLayer];

    psBlockLayer->nLayerType = nLayerType;
    psBlockLayer->nBlockCount = 0;
    psBlockLayer->nLayerSize = 0;

    // Setup the tile layer info.
    TileLayerInfo * psTileLayer = moTileLayerInfoList[iLayer];

    memset(psTileLayer, 0, sizeof(TileLayerInfo));

    return new BinaryTileLayer(this, iLayer, psBlockLayer, psTileLayer);
}

/************************************************************************/
/*                              _DeleteLayer()                          */
/************************************************************************/

/**
 * Deletes the block layer with the specified index.
 *
 * @param iLayer The index of the block layer to delete.
 */
void BinaryTileDir::_DeleteLayer(uint32 iLayer)
{
    // Invalidate the block layer info.
    BlockLayerInfo * psBlockLayer = moLayerInfoList[iLayer];

    psBlockLayer->nLayerType = BLTDead;
    psBlockLayer->nBlockCount = 0;
    psBlockLayer->nLayerSize = 0;

    // Invalidate the tile layer info.
    TileLayerInfo * psTileLayer = moTileLayerInfoList[iLayer];

    memset(psTileLayer, 0, sizeof(TileLayerInfo));
}

/************************************************************************/
/*                           GetDataSegmentName()                       */
/************************************************************************/
std::string BinaryTileDir::GetDataSegmentName(void) const
{
    return "TileData";
}

/************************************************************************/
/*                           GetDataSegmentDesc()                       */
/************************************************************************/
std::string BinaryTileDir::GetDataSegmentDesc(void) const
{
    return "Block Tile Data - Do not modify.";
}

/************************************************************************/
/*                              SwapBlockDir()                          */
/************************************************************************/

/**
 * Swaps the specified block directory info array.
 *
 * @param psBlockDir The block directory info array.
 */
void BinaryTileDir::SwapBlockDir(BlockDirInfo * psBlockDir)
{
    if (!mbNeedsSwap)
        return;

    SwapData(&psBlockDir->nLayerCount, 4, 1);
    SwapData(&psBlockDir->nBlockSize, 4, 1);
}
