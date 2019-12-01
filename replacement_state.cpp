#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <map>
#include <iostream>

using namespace std;

#include "replacement_state.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This file is distributed as part of the Cache Replacement Championship     //
// workshop held in conjunction with ISCA'2010.                               //
//                                                                            //
//                                                                            //
// Everyone is granted permission to copy, modify, and/or re-distribute       //
// this software.                                                             //
//                                                                            //
// Please contact Aamer Jaleel <ajaleel@gmail.com> should you have any        //
// questions                                                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/*
** This file implements the cache replacement state. Users can enhance the code
** below to develop their cache replacement ideas.
**
*/

////////////////////////////////////////////////////////////////////////////////
// The replacement state constructor:                                         //
// Inputs: number of sets, associativity, and replacement policy to use       //
// Outputs: None                                                              //
//                                                                            //
// DO NOT CHANGE THE CONSTRUCTOR PROTOTYPE                                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
CACHE_REPLACEMENT_STATE::CACHE_REPLACEMENT_STATE(UINT32 _sets, UINT32 _assoc, UINT32 _pol)
{

    numsets = _sets;
    assoc = _assoc;
    replPolicy = _pol;

    mytimer = 0;

    dirtyCount = new UINT32[assoc];
    cleanCount = new UINT32[assoc];
    for (UINT32 LRUstackposition = 0; LRUstackposition < assoc; LRUstackposition++)
    {
        dirtyCount[LRUstackposition] = 0;
        cleanCount[LRUstackposition] = 0;
    }

    numDirtyLines = new UINT32[numsets];
    for (UINT32 set = 0; set < numsets; set++)
    {
        numDirtyLines[set] = 0;
    }

    predNumDirtyLines = 0;

    InitReplacementState();
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// The function prints the statistics for the cache                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
ostream &CACHE_REPLACEMENT_STATE::PrintStats(ostream &out)
{

    out << "==========================================================" << endl;
    out << "=========== Replacement Policy Statistics ================" << endl;
    out << "==========================================================" << endl;

    // CONTESTANTS:  Insert your statistics printing here

    return out;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This function initializes the replacement policy hardware by creating      //
// storage for the replacement state on a per-line/per-cache basis.           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

void CACHE_REPLACEMENT_STATE::InitReplacementState()
{
    // Create the state for sets, then create the state for the ways

    repl = new LINE_REPLACEMENT_STATE *[numsets];

    // ensure that we were able to create replacement state

    assert(repl);

    // Create the state for the sets
    for (UINT32 setIndex = 0; setIndex < numsets; setIndex++)
    {
        repl[setIndex] = new LINE_REPLACEMENT_STATE[assoc];

        for (UINT32 way = 0; way < assoc; way++)
        {
            // initialize stack position (for true LRU)
            repl[setIndex][way].LRUstackposition = way;
            // patrick: initialize the clean and dirty categories
            repl[setIndex][way].cleanShadowTag = 0;
            repl[setIndex][way].dirtyShadowTag = 0;
            // initialize the dirty bit of the line
            repl[setIndex][way].dirtyBit = 0;
        }
    }

    if (replPolicy != CRC_REPL_CONTESTANT)
        return;

    // Contestants:  ADD INITIALIZATION FOR YOUR HARDWARE HERE
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This function is called by the cache on every cache miss. The input        //
// argument is the set index. The return value is the physical way            //
// index for the line being replaced.                                         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
INT32 CACHE_REPLACEMENT_STATE::GetVictimInSet(UINT32 tid, UINT32 setIndex, const LINE_STATE *vicSet, UINT32 assoc, Addr_t PC, Addr_t paddr, UINT32 accessType, UINT32 accessSource)
{
    // If no invalid lines, then replace based on replacement policy
    if (replPolicy == CRC_REPL_LRU)
    {
        return Get_LRU_Victim(setIndex);
    }
    else if (replPolicy == CRC_REPL_RANDOM)
    {
        return Get_Random_Victim(setIndex);
    }
    else if (replPolicy == CRC_REPL_CONTESTANT)
    {
        // Contestants:  ADD YOUR VICTIM SELECTION FUNCTION HERE
        // Only use our mechanism in the LLC
        //         if (assoc == 16) return Get_My_Victim(setIndex, accessType);
        //         else return Get_LRU_Victim(setIndex);
        return Get_My_Victim(setIndex, accessType);
    }

    // We should never here here

    assert(0);
    return -1;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This function is called by the cache after every cache hit/miss            //
// The arguments are: the set index, the physical way of the cache,           //
// the pointer to the physical line (should contestants need access           //
// to information of the line filled or hit upon), the thread id              //
// of the request, the PC of the request, the accesstype, and finall          //
// whether the line was a cachehit or not (cacheHit=true implies hit)         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
void CACHE_REPLACEMENT_STATE::UpdateReplacementState(
    UINT32 setIndex, INT32 updateWayID, const LINE_STATE *currLine,
    UINT32 tid, Addr_t PC, UINT32 accessType, bool cacheHit, UINT32 accessSource)
{
    // What replacement policy?
    if (replPolicy == CRC_REPL_LRU)
    {
        UpdateLRU(setIndex, updateWayID);
    }
    else if (replPolicy == CRC_REPL_RANDOM)
    {
        // Random replacement requires no replacement state update
    }
    else if (replPolicy == CRC_REPL_CONTESTANT)
    {
        // Contestants:  ADD YOUR UPDATE REPLACEMENT STATE FUNCTION HERE
        // Feel free to use any of the input parameters to make
        // updates to your replacement policy
        UpdateRWP(setIndex, updateWayID, accessType, cacheHit, currLine);
    }
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//////// HELPER FUNCTIONS FOR REPLACEMENT UPDATE AND VICTIM SELECTION //////////
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This function finds the LRU victim in the cache set by returning the       //
// cache block at the bottom of the LRU stack. Top of LRU stack is '0'        //
// while bottom of LRU stack is 'assoc-1'                                     //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
INT32 CACHE_REPLACEMENT_STATE::Get_LRU_Victim(UINT32 setIndex)
{
    // Get pointer to replacement state of current set

    LINE_REPLACEMENT_STATE *replSet = repl[setIndex];
    INT32 lruWay = 0;

    // Search for victim whose stack position is assoc-1

    for (UINT32 way = 0; way < assoc; way++)
    {
        if (replSet[way].LRUstackposition == (assoc - 1))
        {
            lruWay = way;
            break;
        }
    }

    // return lru way

    return lruWay;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This function finds a random victim in the cache set                       //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
INT32 CACHE_REPLACEMENT_STATE::Get_Random_Victim(UINT32 setIndex)
{
    INT32 way = (rand() % assoc);

    return way;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// This function implements the LRU update routine for the traditional        //
// LRU replacement policy. The arguments to the function are the physical     //
// way and set index.                                                         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

void CACHE_REPLACEMENT_STATE::UpdateLRU(UINT32 setIndex, INT32 updateWayID)
{
    // Determine current LRU stack position
    UINT32 currLRUstackposition = repl[setIndex][updateWayID].LRUstackposition;

    // Update the stack position of all lines before the current line
    // Update implies incremeting their stack positions by one

    for (UINT32 way = 0; way < assoc; way++)
    {
        if (repl[setIndex][way].LRUstackposition < currLRUstackposition)
        {
            repl[setIndex][way].LRUstackposition++;
        }
    }

    // Set the LRU stack position of new line to be zero
    repl[setIndex][updateWayID].LRUstackposition = 0;
}

INT32 CACHE_REPLACEMENT_STATE::Get_My_Victim(UINT32 setIndex, UINT32 at)
{

    //    Debug("Getting the victim..." << endl);

    // Initialization for the initial situation
    if ((predNumDirtyLines == 0) && (numDirtyLines[setIndex] == 0))
    {
        INT32 evictBlkIdx = 0;
        for (UINT32 way = 0; way < assoc; way++)
        {
            if (repl[setIndex][way].LRUstackposition > repl[setIndex][evictBlkIdx].LRUstackposition)
            {
                evictBlkIdx = way;
            }
        }
        return evictBlkIdx;
    }

    // if more dirty lines predicted, use clean part
    else if (predNumDirtyLines > numDirtyLines[setIndex])
    {

        // LRU in clean (read) part:
        INT32 evictBlkIdx = 0;
        for (UINT32 way = 0; way < assoc; way++)
        {
            // pass dirty lines
            if (repl[setIndex][way].dirtyBit == 1)
                continue;
            //Patrick: apply LRU in clean lines
            if (repl[setIndex][way].LRUstackposition > repl[setIndex][evictBlkIdx].LRUstackposition)
            {
                evictBlkIdx = way;
            }
        }
        return evictBlkIdx;
    }
    else
    // if less dirty lines predicted, use dirty part
    {
        // LRU in dirty (write) part
        INT32 evictBlkIdx = 0;
        for (UINT32 way = 0; way < assoc; way++)
        {
            // pass clean lines
            if (repl[setIndex][way].dirtyBit == 0)
                continue;
            // Patrick: apply LRU in dirty lines
            if (repl[setIndex][way].LRUstackposition > repl[setIndex][evictBlkIdx].LRUstackposition)
            {
                evictBlkIdx = way;
            }
        }
        return evictBlkIdx;
    }

    cerr << "ERROR: Get_My_Victim()" << endl;
    return false;
}

void CACHE_REPLACEMENT_STATE::UpdateRWP(UINT32 setIndex, INT32 updateWayID,
                                        UINT32 accessType, bool hit, const LINE_STATE *currLine)
{

    //    Debug("Updating RWP..." << endl);

    // get the tag and current LRU positions of the line
    UINT64 tag = currLine->tag;
    UINT32 currLRUstackposition = repl[setIndex][updateWayID].LRUstackposition;
    //1.: update clean/dirty directories
    if (!hit)
    { // Write miss: allocate line to dirty directory;
        if (accessType == ACCESS_STORE || accessType == ACCESS_WRITEBACK)
        {
            repl[setIndex][updateWayID].dirtyShadowTag = tag;
            repl[setIndex][updateWayID].cleanShadowTag = 0;
            if (repl[setIndex][updateWayID].dirtyBit == 0)
            {
                numDirtyLines[setIndex]++;
            }
            repl[setIndex][updateWayID].dirtyBit = 1;
        }
        // read miss: allocate line to clean directory;
        else if (accessType == ACCESS_PREFETCH || accessType == ACCESS_LOAD || accessType == ACCESS_IFETCH)
        {
            repl[setIndex][updateWayID].dirtyShadowTag = 0;
            repl[setIndex][updateWayID].cleanShadowTag = tag;
            if (repl[setIndex][updateWayID].dirtyBit == 1)
            {
                numDirtyLines[setIndex]--;
            }
            repl[setIndex][updateWayID].dirtyBit = 0;
        }
    }

    else if (hit)
    {
        // Write hit: move line from clean directory to dirty directory;
        if (accessType == ACCESS_STORE || accessType == ACCESS_WRITEBACK)
        {

            repl[setIndex][updateWayID].dirtyShadowTag = tag;
            repl[setIndex][updateWayID].cleanShadowTag = 0;
            if (repl[setIndex][updateWayID].dirtyBit == 0)
            {
                numDirtyLines[setIndex]++;
            }
            repl[setIndex][updateWayID].dirtyBit = 1;
        }

        // read hit: count the reused line in all LRU positions!
        else if (accessType == ACCESS_PREFETCH || accessType == ACCESS_LOAD || accessType == ACCESS_IFETCH)

        {
            if (repl[setIndex][updateWayID].dirtyShadowTag != 0)
                dirtyCount[currLRUstackposition]++;
            else if (repl[setIndex][updateWayID].cleanShadowTag != 0)
                cleanCount[currLRUstackposition]++;
        }
    }

    /* 2. Update LRU for the whole set */
    // Determine current LRU stack position

    for (UINT32 way = 0; way < assoc; way++)
    {
        if (repl[setIndex][way].LRUstackposition < currLRUstackposition)
        {
            repl[setIndex][way].LRUstackposition++;
        }
    }
    // Set the LRU stack position of new line to be zero
    repl[setIndex][updateWayID].LRUstackposition = 0;


    //3. Update prediction of dirty lines
    //Add all the dirty and clean values and update the counters by ways.
    UINT32 max = 0, totalCleanLines = 0, totalDirtyLines = 0;
    for (UINT32 part = 0; part <= assoc; part++)
    {
        for (UINT32 dPart = 0; dPart < part; dPart++)
        {
            totalDirtyLines += dirtyCount[dPart];
        }
        for (UINT32 cPart = part; cPart < assoc; cPart++)
        {
            totalCleanLines += cleanCount[cPart];
        }
        if (max < (totalCleanLines + totalDirtyLines))
        {
            max = totalCleanLines + totalDirtyLines;
            predNumDirtyLines = part; // Partition here
        }
    }
}

CACHE_REPLACEMENT_STATE::~CACHE_REPLACEMENT_STATE(void)
{
}
