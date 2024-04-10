/*
Group Members:
Sanjay Kethineni => sk2425
Adhit Thakur => at1186
*/

/*
iLab machine : kill.cs.rutgers.edu
iLab username : at1186
*/

#include "my_vm.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>

int tlbTotalAccesses;
int tlbMisses;

typedef struct{
    uint32_t amountOfPhysicalPages;
    uint32_t amountOfVirtualPages;
    uint32_t levelOneBits;
    uint32_t levelTwoBits;
    uint32_t offsetBits;
    uint32_t levelOneAnd;
    uint32_t levelTwoAnd;
    uint32_t offsetAnd;
    uint32_t pagesPerSecondLevelTable;   
}Metadata;

typedef struct{
    //total amount of physical pages (accounting for 1 or more)
    uint32_t *physicalPageNum; 
    uint32_t usedStatus;
    int physicalPageNumSize;
}PageTableEntry;

typedef struct{
    PageTableEntry *tableEntries;
}PageTable;

typedef struct{
    PageTable *table;
    uint32_t isInUse;
}PageDirectoryEntry;

typedef struct TLBEntry{
    uint32_t key;
    uint32_t value;
    struct TLBEntry* next;
}TLBEntry;

TLBEntry* TLB[TLB_ENTRIES] = {NULL};

Metadata *metaData;
uint32_t* physicalMemoryBitMap;
char* actualPhysicalMem;
PageDirectoryEntry* outerLevelTable;
int TLBSize;

//function definitions
uint32_t calculateLog(uint32_t);
unsigned int convertBinaryToDecimal(uint32_t,uint32_t);
void printMetadata();
int isDirectoryTableAllocated(unsigned int);
long getSetFirstZeroBitAndReturnIndex();
void createAndInitPageTable(uint32_t);
unsigned int intToBinary(unsigned int, int);
unsigned int generateVirtualAddress(unsigned int, unsigned int, unsigned int);
unsigned int generateVirtualAddressSizeTwo(unsigned int, unsigned int);
long assignPhysicalPageNum(unsigned int, unsigned int, int);
void freePhysicalPageNum(uint32_t);
uint32_t hash(uint32_t);
void insert(uint32_t, uint32_t);
uint32_t get(uint32_t);
void delete(uint32_t);
void resetBit(uint32_t* num, int bitNum);

void resetBit(uint32_t* num, int bitNum)
{    
    *num &= ~(1 << bitNum);
}

unsigned int intToBinary(unsigned int num, int numBits){
    if(numBits > sizeof(unsigned int)*__CHAR_BIT__){
        return 0;
    }

    unsigned int binary = 0;
    for(int i = numBits-1; i >= 0; i--){
        int bit = (num>>i)&1;
        binary = binary | (bit<<i);
    }
    return binary;
}

unsigned int generateVirtualAddress(unsigned int firstLevel, unsigned int secondLevel, unsigned int offset){
    return (firstLevel << (32-metaData->levelOneBits)) | 
    (secondLevel<<(32-metaData->levelOneBits-metaData->levelTwoBits)) | offset;
}

unsigned int generateVirtualAddressSizeTwo(unsigned int firstLevel, unsigned int secondLevel){
    return (firstLevel << (32-metaData->levelOneBits)) | secondLevel;
}

void createAndInitPageTable(uint32_t outerIntVal){
    outerLevelTable[outerIntVal].table = (struct PageTable*)malloc(sizeof(PageTable));
    PageTable* temp = outerLevelTable[outerIntVal].table;
    temp->tableEntries = (struct PageTableEntry*)malloc(metaData->pagesPerSecondLevelTable * sizeof(PageTableEntry));
    for(int i = 0; i < metaData->pagesPerSecondLevelTable; i ++){
        temp->tableEntries[i].usedStatus = 0;
    }
}

long getSetFirstZeroBitAndReturnIndex(){
    unsigned int pos = 0;
    int whichPage = 0;
    int i=0;
    for(i = 0; i < metaData->amountOfPhysicalPages/32; i ++){
        uint32_t val = physicalMemoryBitMap[i];

        if( val >= UINT_MAX)
        {
            //last one OOM
            if(i == (metaData->amountOfPhysicalPages/32)-1){
                printf("Out of bounds error phyIndex=%d\n",i);
                return -1;
            }
            else
                continue;
        }

        pos = (~val & -~val) == 0 ? -1 : log2(~val & -~val); //get index of first 0 bit, returns -1 if no 0 bit

        if(pos != -1){
            int offsetToDo = pos%32;

            physicalMemoryBitMap[i] |= (1U<<offsetToDo); //set index
            whichPage = i;
            break;
        }
    }    

    return ((whichPage*32)+pos); //return the page #
}

int isDirectoryTableAllocated(unsigned int outerIndex){
    if(outerLevelTable[outerIndex].table == NULL){
        return 0;
    }
    return 1;
}

//TLB structure implementation
uint32_t hash(uint32_t key){
    return key%TLB_ENTRIES;
}

void insert(uint32_t key, uint32_t value){
    uint32_t index = hash(key);
    TLBEntry* new_entry = (TLBEntry*)malloc(sizeof(TLBEntry));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = NULL;

    if(TLB[index] == NULL){
        TLB[index] = new_entry;
    }
    else{
        TLBEntry* temp = TLB[index];
        while(temp->next != NULL){
            temp = temp->next;
        }
        temp->next = new_entry;
    }
}

uint32_t get(uint32_t key){
    uint32_t index = hash(key);
    TLBEntry* temp = TLB[index];

    while(temp != NULL){
        if(temp->key == key){
            return temp->value;
        }
        temp = temp->next;
    }

    return -1; //return -1 if not found
}

void delete(uint32_t index){
    if(TLB[index] = NULL){
        return;
    }

    TLBEntry* current = TLB[index];
    TLB[index] = current->next;
    free(current);
}
//End of TLB structure implementation

void set_physical_mem(){
    tlbTotalAccesses = 0;
    tlbMisses = 0;

    metaData = malloc(sizeof(Metadata));

    metaData->amountOfPhysicalPages = MEMSIZE / PAGE_SIZE;
    metaData->amountOfVirtualPages = MAX_MEMSIZE / PAGE_SIZE;
    metaData->offsetBits = calculateLog(PAGE_SIZE); //power of 2 page size is, so for 8k this is 13
    
    uint32_t totalMem = metaData->amountOfVirtualPages*sizeof(PageTableEntry);
    uint32_t pagesPerPageTable = totalMem / PAGE_SIZE;
    metaData->pagesPerSecondLevelTable = pagesPerPageTable;
    uint32_t entriesPerPageInSecondLvl = calculateLog(pagesPerPageTable);

    metaData->levelTwoBits = entriesPerPageInSecondLvl;
    metaData->levelOneBits = 32 - (entriesPerPageInSecondLvl+metaData->offsetBits);

    //initialize the page table
    outerLevelTable = (PageDirectoryEntry*)malloc((1<<metaData->levelOneBits)*sizeof(PageDirectoryEntry));
    for(int i = 0; i < (1<<metaData->levelOneBits); i ++){
        outerLevelTable[i].table = NULL;
        outerLevelTable[i].isInUse = 0;
    }

    for(int i = 0; i < 1<<metaData->levelOneBits; i ++){ //go through outer level and initialize tables
        createAndInitPageTable(i);
    }

    //allocate the physical memory bitmap
    uint32_t amountOfUints = metaData->amountOfPhysicalPages/32;
    physicalMemoryBitMap = (uint32_t*)malloc(amountOfUints*sizeof(uint32_t));

    //allocate the actual physical memory, +1 to account for string delimeter
    actualPhysicalMem = (char*)malloc((MEMSIZE+1)*sizeof(char)); 

    for(int i = 0; i < amountOfUints; i ++){
        physicalMemoryBitMap[i]=0;
    }

    metaData->levelOneAnd = ((1<<metaData->levelOneBits)-1) << (metaData->levelTwoBits+metaData->offsetBits);
    metaData->levelTwoAnd = ((1<<metaData->levelTwoBits)-1)<< (metaData->offsetBits);
    metaData->offsetAnd = (1<<metaData->offsetBits)-1;
}

//helper function
uint32_t calculateLog(uint32_t val){
    uint32_t res = 0;
    while(val > 1){
        val >>= 1;
        res ++;
    }
    return res;
}

void * translate(unsigned int vp){
    uint32_t outerLevelShifter = vp & metaData->levelOneAnd;
    uint32_t outerIntVal = outerLevelShifter >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t secondLevelShifter = vp & metaData->levelTwoAnd;
    uint32_t secondIntVal = secondLevelShifter >> metaData->offsetBits;

    uint32_t offsetIntVal = vp & metaData->offsetAnd;

    if(isDirectoryTableAllocated(outerIntVal)==0){
        return NULL;
    }

    uint32_t physicalPageNumber;
    //Check TLB first
    uint32_t firstTwoLevels = generateVirtualAddressSizeTwo(outerIntVal,secondIntVal);
    if(check_TLB(firstTwoLevels) == 0){ //TLB hit
        physicalPageNumber = get(firstTwoLevels);
        tlbTotalAccesses++;
    }
    else{ //TLB miss
        physicalPageNumber = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNum[0];
        insert(firstTwoLevels,physicalPageNumber);
        tlbTotalAccesses++;
        tlbMisses++;
    }
    
    return (void*)((actualPhysicalMem+(physicalPageNumber*PAGE_SIZE)+offsetIntVal));
}

unsigned int convertBinaryToDecimal(uint32_t val,uint32_t amountOfBits){
    unsigned int decimal = 0;
    for(int i = 0; i < amountOfBits; i ++){
        uint32_t bitVal = (val>>i)&1;
        decimal += bitVal << (amountOfBits-i-1);
    }
    return decimal;
}

long page_map(unsigned int vp, int amountOfPages){
    uint32_t outerLevelShifter = vp & metaData->levelOneAnd;
    uint32_t outerIntVal = outerLevelShifter >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t secondLevelShifter = vp & metaData->levelTwoAnd;
    uint32_t secondIntVal = secondLevelShifter >> metaData->offsetBits;

    uint32_t offsetIntVal = vp & metaData->offsetAnd;

    if(isDirectoryTableAllocated(outerIntVal)==0){ //does not exist because table is NULL
        long ret = assignPhysicalPageNum(outerIntVal,secondIntVal,amountOfPages);
        if( ret == -1)
            return ret;
        return outerIntVal; 
    }
    else if(outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].usedStatus==0 || outerLevelTable[outerIntVal].isInUse == 0){ //does not exist because not in table at directory
        //check if index is out of bounds
        long ret = assignPhysicalPageNum(outerIntVal,secondIntVal,amountOfPages);
        if( ret == -1)
            return ret;
        return outerIntVal;
    }
    else{ //does exist
        return outerIntVal; 
    }
}

//helper function
long assignPhysicalPageNum(unsigned int outerIntVal, unsigned int secondIntVal,int amountOfPages){
    outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNum 
        = (uint32_t*)malloc(amountOfPages*sizeof(uint32_t));
    for(int z = 0; z < amountOfPages; z ++){
        long physPageNumber = getSetFirstZeroBitAndReturnIndex();
        if( physPageNumber == -1)
            return -1;

        outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNum[z] = physPageNumber;
    }
    outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNumSize = amountOfPages;
    outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].usedStatus = 1; 
    outerLevelTable[outerIntVal].isInUse = 1;
}

void * t_malloc(size_t n){
    //TODO: Finish
    size_t amountOfPages = (size_t)(ceil((float)n/PAGE_SIZE));
    //if(amountOfPages == 1){
        for(int i = 0; i < 1<<metaData->levelOneBits; i ++){ //go through outer level
                PageTableEntry* tempTable = outerLevelTable[i].table->tableEntries;
               
                for(int x = 0; x < metaData->pagesPerSecondLevelTable; x ++){ //going through second lvl table
                    if(tempTable[x].usedStatus == 0){ //finding first available page
                        unsigned int firstLevel = intToBinary(i,metaData->levelOneBits);
                        unsigned int secondLevel = intToBinary(x,metaData->levelTwoBits);
                        unsigned int offsetLevel = intToBinary(0,metaData->offsetBits);
                        unsigned int virtualAddress = generateVirtualAddress(firstLevel,secondLevel,offsetLevel);
                        
                        long ret = page_map(virtualAddress,amountOfPages);
                        if(ret == -1 )
                            return NULL; 

                        return virtualAddress;
                    }
                }
        }
}

//returns 0 on success, -1 on failure
int t_free(unsigned int vp, size_t n){
    size_t amountOfPages = (size_t)(ceil((float)n/PAGE_SIZE));

    uint32_t outerLevelShifter = vp & metaData->levelOneAnd;
    uint32_t outerIntVal = outerLevelShifter >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t secondLevelShifter = vp & metaData->levelTwoAnd;
    uint32_t secondIntVal = secondLevelShifter >> metaData->offsetBits;

    uint32_t offsetIntVal = vp & metaData->offsetAnd;

    uint32_t *physicalPageCollection = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNum;
    int physicalPageCollectionSize = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNumSize;

    //free pages specified
    int i = 0;
    while(i < amountOfPages || i < physicalPageCollectionSize){
        uint32_t currPhysPageNum = *physicalPageCollection;
        freePhysicalPageNum(currPhysPageNum);
        physicalPageCollection++;
        i++;
    }

    //free page table structures
    outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].usedStatus = 0;

    //check if the page table for the directory is empty
    PageTable* temp = outerLevelTable[outerIntVal].table;
    for(int i = 0; i < metaData->pagesPerSecondLevelTable; i ++){
        if(temp->tableEntries[i].usedStatus==1){
            if(amountOfPages >= physicalPageCollectionSize)
                return 0;
            return -1;
        }
    }

    outerLevelTable[outerIntVal].isInUse = 0;
    if(amountOfPages >= physicalPageCollectionSize)
        return 0;
    return -1;
}

void freePhysicalPageNum(uint32_t physPageNumToFree){
    int physicalMemoryIdx = physPageNumToFree/32;
    resetBit(&physicalMemoryBitMap[physicalMemoryIdx],physPageNumToFree%32);
}

//assuming n is given in terms of amount of bytes
int put_value(unsigned int vp, void *val, size_t n){
    size_t tempN = n;

    uint32_t outerLevelShifter = vp & metaData->levelOneAnd;
    uint32_t outerIntVal = outerLevelShifter >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t secondLevelShifter = vp & metaData->levelTwoAnd;
    uint32_t secondIntVal = secondLevelShifter >> metaData->offsetBits;

    uint32_t offsetIntVal = vp & metaData->offsetAnd;

    //if outer level directory or page table entry are not in use, this is invalid
    if(outerLevelTable[outerIntVal].isInUse == 0 || outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].usedStatus == 0){
        return -1;
    }

    uint32_t *physicalPageCollection = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNum;
    int physicalPageCollectionSize = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNumSize;

    if(offsetIntVal >= physicalPageCollectionSize*PAGE_SIZE){ //if you are trying to access outside of memory access region
        return -1;
    }

    char* startingLoc = (char*)translate(vp);
    int i = 0;
    while(i < physicalPageCollectionSize && tempN > 0){
        uint32_t currPhysPageNum = physicalPageCollection[i];
        uint32_t locationOfPageNum = currPhysPageNum * PAGE_SIZE;
        if(i == 0){ //first time, involve offset
             if(tempN <= PAGE_SIZE){
                memcpy(startingLoc,val,tempN);
                val += tempN;
                tempN -= tempN;
                return 0;
             }
             else{
                memcpy(startingLoc,val,PAGE_SIZE-offsetIntVal);
                val += (PAGE_SIZE-offsetIntVal);
                tempN -= (PAGE_SIZE-offsetIntVal);
             }
        }
        else if(i==physicalPageCollectionSize-1){ //at last page, allocate everything
            if(tempN > PAGE_SIZE)
                return -1;
            memcpy(actualPhysicalMem+locationOfPageNum,val,tempN);
            val += tempN;
            tempN -= tempN;
        }
        else{ //can allocate up to a whole page size
            memcpy(actualPhysicalMem+locationOfPageNum,val,PAGE_SIZE);
            val += PAGE_SIZE;
            tempN -= PAGE_SIZE;
        }

        i++;
    }   
    return 0;
}

int get_value(unsigned int vp, void *dst, size_t n){
    size_t tempN = n;

    uint32_t outerLevelShifter = vp & metaData->levelOneAnd;
    uint32_t outerIntVal = outerLevelShifter >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t secondLevelShifter = vp & metaData->levelTwoAnd;
    uint32_t secondIntVal = secondLevelShifter >> metaData->offsetBits;

    uint32_t offsetIntVal = vp & metaData->offsetAnd;

    uint32_t *physicalPageCollection = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNum;
    int physicalPageCollectionSize = outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].physicalPageNumSize;

    //if outer level directory or page table entry are not in use, this is invalid
    if(outerLevelTable[outerIntVal].isInUse == 0 || outerLevelTable[outerIntVal].table->tableEntries[secondIntVal].usedStatus == 0){
        return -1;
    }

    if(offsetIntVal >= physicalPageCollectionSize*PAGE_SIZE){ //if you are trying to access outside of memory access region
        return -1;
    }

    uint32_t* startingLoc = (uint32_t*)translate(vp);
    int i = 0;
    while(i < physicalPageCollectionSize && tempN > 0){
        uint32_t currPhysPageNum = physicalPageCollection[i];
        uint32_t locationOfPageNum = currPhysPageNum * PAGE_SIZE;
        if(i == 0){ //first time, involve offset
            if(tempN <= PAGE_SIZE){
                memcpy(dst,startingLoc,tempN);
                dst += tempN;
                tempN -= tempN;
                return -1;
             }
             else{
                memcpy(dst,startingLoc,PAGE_SIZE-offsetIntVal);
                dst += (PAGE_SIZE-offsetIntVal);
                tempN -= (PAGE_SIZE-offsetIntVal);
             }
        }
        else if(i==physicalPageCollectionSize-1){ //at last page, allocate everything
            if(tempN > PAGE_SIZE)
                return -1;
            memcpy(dst,actualPhysicalMem+locationOfPageNum,tempN);
            dst += tempN;
            tempN -= tempN;
        }
        else{ //can allocate up to a whole page size
            memcpy(dst,actualPhysicalMem+locationOfPageNum,PAGE_SIZE);
            dst += PAGE_SIZE;
            tempN -= PAGE_SIZE;
        }

        i++;
    }   
    return 0;
}

void mat_mult(unsigned int aVp, unsigned int bVp, unsigned int cVp, size_t l, size_t m, size_t n){
    int* aAddress = (int*)translate(aVp);
   int* bAddress = (int*)translate(bVp);
   int* cAddress = (int*)translate(cVp);

   //setting up multi-page structures
    //a
    uint32_t aOuterShift = aVp & metaData->levelOneAnd;
    uint32_t aOuterIntVal = aOuterShift >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t aSecondShift = aVp & metaData->levelTwoAnd;
    uint32_t aSecondIntVal = aSecondShift >> metaData->offsetBits;

    uint32_t *aPhysicalPageCollection = outerLevelTable[aOuterIntVal].table->tableEntries[aSecondIntVal].physicalPageNum;
    int aPhysicalPageCollectionSize = outerLevelTable[aOuterIntVal].table->tableEntries[aSecondIntVal].physicalPageNumSize;
    //end of a

    //b
    uint32_t bOuterShift = bVp & metaData->levelOneAnd;
    uint32_t bOuterIntVal = bOuterShift >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t bSecondShift = bVp & metaData->levelTwoAnd;
    uint32_t bSecondIntVal = bSecondShift >> metaData->offsetBits;

    uint32_t *bPhysicalPageCollection = outerLevelTable[bOuterIntVal].table->tableEntries[bSecondIntVal].physicalPageNum;
    int bPhysicalPageCollectionSize = outerLevelTable[bOuterIntVal].table->tableEntries[bSecondIntVal].physicalPageNumSize;
    //end of b

    //c
    uint32_t cOuterShift = cVp & metaData->levelOneAnd;
    uint32_t cOuterIntVal = cOuterShift >> (metaData->levelTwoBits + metaData->offsetBits);

    uint32_t cSecondShift = cVp & metaData->levelTwoAnd;
    uint32_t cSecondIntVal = cSecondShift >> metaData->offsetBits;

    uint32_t *cPhysicalPageCollection = outerLevelTable[cOuterIntVal].table->tableEntries[cSecondIntVal].physicalPageNum;
    int cPhysicalPageCollectionSize = outerLevelTable[cOuterIntVal].table->tableEntries[cSecondIntVal].physicalPageNumSize;
    //end of c
   //end of setting up multi-page structures

   int aR = 0;
   int aC = 0;
   int bR = 0;
   int bC = 0;
   int cR = 0;
   int cC = 0;

   //going through matrix c's dimensions
   for(int r = 0; r < l; r ++){
        for(int c = 0; c < n; c ++){
            int val = 0;
            //for each value do row x col
            for(int i = 0; i < m; i ++){ //a and b share same rows and cols respectively
                int aVal = -1; 
                int aValOffset = ((aR*m*sizeof(int)) + (aC*sizeof(int)));
                int aWhichPageIndex =aValOffset/PAGE_SIZE;
                int aWhichPositionWithinPage = aValOffset%PAGE_SIZE;
                memcpy(&aVal,actualPhysicalMem+((aPhysicalPageCollection[aWhichPageIndex]*PAGE_SIZE)+aWhichPositionWithinPage),sizeof(int));

                int bVal = -1; 
                int bValOffset = ((bR*n*sizeof(int))+ (bC*sizeof(int)));
                int bWhichPageIndex = bValOffset/PAGE_SIZE;
                int bWhichPositionWithinPage = bValOffset%PAGE_SIZE;
                memcpy(&bVal,actualPhysicalMem+((bPhysicalPageCollection[bWhichPageIndex]*PAGE_SIZE)+bWhichPositionWithinPage),sizeof(int));

                val += (aVal*bVal);
                aC++;
                bR++;
            }
           
           int cValOffset = ((r*n*sizeof(int)) + c*sizeof(int));
           int cWhichPageIndex = cValOffset/PAGE_SIZE;
           int cWhichPositionWithinPage = cValOffset%PAGE_SIZE;
           memcpy(actualPhysicalMem+((cPhysicalPageCollection[cWhichPageIndex]*PAGE_SIZE)+cWhichPositionWithinPage),&val,sizeof(int));

            aC=0;
            bR=0;
            bC++;
        }
        aR++;
        bC=0;
   }
}

void add_TLB(unsigned int vpage, unsigned int ppage){
    //TODO: Finish

    if(TLBSize >= TLB_ENTRIES){ //randomly delete from TLB
        int randomIndex = rand() % TLB_ENTRIES;
        delete(randomIndex);
        TLBSize --;
    }
    insert(vpage,ppage);
    TLBSize++;
}

//returns 0 if found, -1 if not
int check_TLB(unsigned int vpage){
    //TODO: Finish
    if(get(vpage) == -1){ //not found, fail and load cache
        return -1;
    }
    return 0;
}

void print_TLB_missrate(){
    float rate = (float)tlbMisses/tlbTotalAccesses;
    printf("----------\n");
    printf("TLB Missrate => %f\n",rate);
    printf("----------\n");
}