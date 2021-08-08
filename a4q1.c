#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>

int sectorLength;
int clusterLength;
int fatEOF;//index which indicates Ending of the linkedlist

#pragma pack(push)
#pragma pack(1)

typedef struct  MAIN_BOOT_SECTOR
{
    uint8_t firstPart[80];//To read the first 80 bytes of the MBS which is not required
    uint32_t fatOffset;//the volume-relative sector offset of the First FAT
    uint32_t fatLength;//the length in sectors of each FAT table
    uint32_t clusterHeapOffset;//the volume-relative sector offset of the Cluster Heap
    uint32_t clusterCount;//the number of clusters in the cluster heap
    uint32_t firstClusterOfRootDir;//contain the cluster index of the first cluster of the root directory
    uint32_t volumeSerial;//contain a unique serial number
    uint16_t fileSystemRev;
    uint16_t volumeFlags;
    uint8_t bytesPerSectorShift;//the bytes per sector expressed as log2(N)
    uint8_t sectorsPerClusterShift;//the sectors per cluster expressed as log2(N)
    uint8_t numberOfFAT;//the number of FATs and Allocation Bitmaps
    
}MBS;
#pragma pack(pop)

/**
 * Convert a Unicode-formatted string containing only ASCII characters
 * into a regular ASCII-formatted string (16 bit chars to 8 bit
 * chars).
 *
 * NOTE: this function does a heap allocation for the string it
 *       returns, caller is responsible for `free`-ing the allocation
 *       when necessary.
 *
 * uint16_t *unicode_string: the Unicode-formatted string to be
 *                           converted.
 * uint8_t   length: the length of the Unicode-formatted string (in
 *                   characters).
 *
 * returns: a heap allocated ASCII-formatted string.
 */
static char *unicode2ascii( uint16_t *unicode_string, uint8_t length )
{
    assert( unicode_string != NULL );
    assert( length > 0 );
    
    char *ascii_string = NULL;
    
    if ( unicode_string != NULL && length > 0 )
    {
        // +1 for a NULL terminator
        ascii_string = calloc( sizeof(char), length + 1);
        
        if ( ascii_string )
        {
            // strip the top 8 bits from every character in the
            // unicode string
            for ( uint8_t i = 0 ; i < length; i++ )
            {
                ascii_string[i] = (char) unicode_string[i];
            }
            // stick a null terminator at the end of the string.
            ascii_string[length] = '\0';
        }
    }
    
    return ascii_string;
}


//This fucntion is used to print the info about the file
void getInfo(int fd,MBS* mainSec)
{
    //Go to the cluster heap
    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
    lseek(fd,(mainSec->firstClusterOfRootDir-2)*clusterLength,SEEK_CUR);
    uint16_t labelUnicode[11];
    uint8_t labelLength=11;
    //Getting the volume label
    //skip the next 2 bytes
    lseek(fd,2,SEEK_CUR);
    //read the label
    int i;
    for(i=0;i<labelLength;i++)
    {
        read(fd,&labelUnicode[i],2);
    }
    char* label=unicode2ascii(labelUnicode,labelLength);
    //Calculating the free space
    uint8_t type;
    uint32_t bitmapLocation=0;
    for(i=0;i<sectorLength/32 && bitmapLocation==0;i++)
    {
        //Go to the cluster heap
        lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
        lseek(fd,((mainSec->firstClusterOfRootDir-2)*clusterLength)+(32*i),SEEK_CUR);
        read(fd,&type,1);
        //If we find the entry for the bitmap allocation
        if(type==0x81)
        {
            //Skip the next 19 bytes
            lseek(fd,19,SEEK_CUR);
            //Read the bitmap location
            read(fd,&bitmapLocation,4);
        }
    }
    //Build the cluster chain of the allocation bitmap
    uint32_t bitmapChain[mainSec->clusterCount];
    //Go to the start of the FAT region
    lseek(fd,(int)(mainSec->fatOffset) * sectorLength,SEEK_SET);
    //Go to the location of the firstCluster of the bitmap
    lseek(fd,bitmapLocation*4,SEEK_CUR);
    //Make the chain
    for(i=0;i<=(int)mainSec->clusterCount+1;i++)
    {
        if(i==0)
        {
            bitmapChain[i]=bitmapLocation;
        }
        else
        {
            //Go to the top of the FAT region
            lseek(fd,(int)(mainSec->fatOffset) * sectorLength,SEEK_SET);
            //Go to the location stated by the previous index
            lseek(fd,bitmapChain[i-1]*4,SEEK_CUR);
            //Add it to the list
            read(fd,&bitmapChain[i],4);
            //If we reached the end of file
            if(bitmapChain[i]==UINT32_MAX)
            {
               // printf("Entry: %u\n",bitmapChain[i]);
                break;
            }
        }
        //printf("Entry: %u\n",bitmapChain[i]);
    }
    //printf("bitmap: %u\n",bitmapLocation);

    uint8_t oneByte;
    int popCount=0;
    //Now go to the cluster that holds the allocation bitmap
    //Go to the start of the cluster heap
    lseek(fd,mainSec->clusterHeapOffset*clusterLength,SEEK_SET);
    //Go to the cluster which contains the allocation bitmap
    lseek(fd,(bitmapLocation-2)*clusterLength,SEEK_CUR);
    int j;
    for(i=0;i<(int)(mainSec->clusterCount-1);i+=8)
    {
        read(fd,&oneByte,1);
        for(j=0;j<(int)(sizeof(uint8_t) * 8); j++)
        {
            //printf("%i ", oneByte & 0x01);
            if((oneByte & 0x01)==0)
            {
                popCount++;
            }
            oneByte = oneByte >> 1;
        }
    }
    
    //Print the info about the volume
    printf("/////////////////////////////////////////\n");
    printf("Volume Label: %s\n",label);
    printf("Volume Serial Number: %u\n",mainSec->volumeSerial);
    printf("Free Space: %dKB\n",(popCount*clusterLength)/1024);
    printf("Sector Size: %dBytes\n",sectorLength);
    printf("Cluster Size: %d Sectors\n",clusterLength/sectorLength);
    printf("/////////////////////////////////////////\n");
    
}

int main(int argc,char* argv[])
{
    assert(argc>0);
    assert(argv!=NULL);
    MBS mainSec;
    char* fileName=argv[1];
    int fd=open(fileName,O_RDONLY);
    read(fd,&mainSec,sizeof(MBS));
    sectorLength=(int)pow(2,(int)mainSec.bytesPerSectorShift);
    clusterLength=(int)pow(2,(int)mainSec.sectorsPerClusterShift) * sectorLength;
    //Build the cluster chain
    uint32_t fatEntry[(int)mainSec.clusterCount+2];
    int i;
    //Go to the start of the FAT region
    lseek(fd,(int)(mainSec.fatOffset) * sectorLength,SEEK_SET);
    fatEOF=0;
    for(i=0;i<=(int)mainSec.clusterCount+1;i++)
    {
        if(i>=2)
        {
            if(i==2)
            {
                fatEntry[i]=(uint32_t)2;
            }
            else
            {
                //Go to the top of the FAT region
                lseek(fd,(int)(mainSec.fatOffset) * sectorLength,SEEK_SET);
                //Get the value of the previous index
                lseek(fd,fatEntry[i-1]*4,SEEK_CUR);
                //Add it to the list
                read(fd,&fatEntry[i],4);
                //If we reached the end of file
                if(fatEntry[i]==UINT32_MAX)
                {
                    break;
                }
            }
        }
        else
        {
            //Read 4 bytes for each fat entry
            read(fd,&fatEntry[i],4);
        }
    }
    if(strcmp(argv[2],"info")==0)
    {
        getInfo(fd,&mainSec);
    }
    return EXIT_SUCCESS;
}
