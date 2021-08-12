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

uint32_t* buildClusterChain(int fd,MBS* mainSec, uint32_t firstLocation)
{
    uint32_t temp[(int)mainSec->clusterCount+1];
    int count=0;
    //Go to the start of the FAT region
    lseek(fd,(int)(mainSec->fatOffset) * sectorLength,SEEK_SET);
    //Go to the location of the firstCluster of the bitmap
    lseek(fd,firstLocation*4,SEEK_CUR);
    //Make the chain
    int i;
    for(i=0;i<(int)mainSec->clusterCount+1;i++)
    {
        //First Position
        if(i==0)
        {
            temp[0]=firstLocation;
        }
        else
        {
            //Go to the top of the FAT region
            lseek(fd,(int)(mainSec->fatOffset) * sectorLength,SEEK_SET);
            //Go to the location stated by the previous index
            lseek(fd,temp[i-1]*4,SEEK_CUR);
            //Add the value to the list
            read(fd,&temp[i],4);
            //End of list
            if(temp[i]==UINT32_MAX)
            {
                count++;
                break;
            }
        }
        count++;
    }
    uint32_t* result;
    result=(uint32_t*)(malloc(sizeof(uint32_t)*count));
    for(i=0;i<count;i++)
    {
        result[i]=temp[i];
        //printf("Entry: %u\n", result[i]);
    }
    
    return result;
}


//This fucntion is used to print the info about the file
void getInfo(int fd,MBS* mainSec)
{
    int i;
    uint8_t type;
    //Go to the cluster heap
    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
    lseek(fd,(mainSec->firstClusterOfRootDir-2)*clusterLength,SEEK_CUR);
    //Find the volume label
    for(i=0;i<clusterLength/32;i++)
    {
        read(fd,&type,1);
        if(type==0x83)
        {
            break;
        }
        lseek(fd,31,SEEK_CUR);
    }
    uint16_t labelUnicode[11];
    uint8_t labelLength=11;
    //Getting the volume label
    //skip the next 1 byte
    lseek(fd,1,SEEK_CUR);
    //read the label
    for(i=0;i<labelLength;i++)
    {
        read(fd,&labelUnicode[i],2);
    }
    char* label=unicode2ascii(labelUnicode,labelLength);
    //Calculating the free space
    uint32_t bitmapLocation=0;
    for(i=0;i<clusterLength/32 && bitmapLocation==0;i++)
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

    uint32_t* clusterChain= buildClusterChain(fd,mainSec,bitmapLocation);
    uint8_t oneByte;
    int popCount=0;
    //Now go to the cluster that holds the allocation bitmap
    //Go to the start of the cluster heap
    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
    //Go to the cluster which contains the allocation bitmap
    lseek(fd,(bitmapLocation-2)*clusterLength,SEEK_CUR);
    int j;
    int count=0;
    int index=1;
    for(i=0;i<(int)(mainSec->clusterCount-1);i+=8)
    {
        read(fd,&oneByte,1);
        for(j=0;j< 8; j++)
        {
            //printf("%i ", oneByte & 0x01);
            if((oneByte & 0x01)==0)
            {
                popCount++;
            }
            oneByte = oneByte >> 1;
        }
        //printf("\n");
        count++;
        if(count==clusterLength)
        {
            //Go to the start of the cluster heap
            lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
            //Go to the cluster which contains the next allocation bitmap
            lseek(fd,(clusterChain[index]-2)*clusterLength,SEEK_CUR);
            if(clusterChain[index]==UINT32_MAX)
            {
                break;
            }
            index++;
            count=0;

        }
    }

    
    //Print the info about the volume
    printf("/////////////////////////////////////////\n");
    printf("Volume Label: %s\n",label);
    printf("Volume Serial Number: %u\n",mainSec->volumeSerial);
    printf("Free Space: %dKB\n",(popCount*clusterLength)/1024);
    printf("Cluster Size: %d Bytes or %d Sectors\n",clusterLength,clusterLength/sectorLength);
    printf("/////////////////////////////////////////\n");
    
    free(clusterChain);
    free(label);
}
void recList(int fd,MBS* mainSec,uint32_t clusterIndex,int level)
{
    uint32_t* clusterChain=buildClusterChain(fd,mainSec,clusterIndex);
    int index=0;
    int i;
    int j;
    uint8_t type;
    uint8_t nameLength;
    uint32_t clstr;
    uint16_t name[15];
    uint16_t fileAtr;
    char* nameInChar;
    int fileFlag=0;
    int streamFlag=0;
    int nameFlag=0;
    int dirFlag=0;
    while(clusterChain[index]!=UINT32_MAX)
    {
        //Go to the cluster heap
        lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
        //GO to the cluster according to the cluster chain
        lseek(fd,(clusterChain[index]-2)*clusterLength,SEEK_CUR);
        //printf("\\\\\\\\\\\\\\\\\\\\\\\\\n");
        for(i=0;i<clusterLength/32;i++)
        {
            read(fd,&type,1);
            //Check for the file directory
            if(type==0x85)
            {
                fileFlag=1;
                //Check if it is a file or directory
                lseek(fd,3,SEEK_CUR);
                read(fd,&fileAtr,2);
                //printf("fileAtr: %u\n",fileAtr);
                fileAtr=fileAtr >> 4;
                //Skip the next 26byts
                lseek(fd,26,SEEK_CUR);
                //Check if the entry is a directory
                if((fileAtr & 0x01)==1)
                {
                    dirFlag=1;
                }
            }
            //Check for the stream extension
            else if(type==0xC0 && fileFlag==1)
            {
                fileFlag=0;
                streamFlag=1;
                //Read the length of the file name
                lseek(fd,2,SEEK_CUR);
                read(fd,&nameLength,1);
                //Get the first cluster of the directory
                lseek(fd,16,SEEK_CUR);
                read(fd,&clstr,4);
                //Go to the file name dir entry
                lseek(fd,8,SEEK_CUR);
            }
            //Check for the file name
            else if(type==0xC1 && streamFlag==1)
            {
                streamFlag=0;
                nameFlag=1;
                //Get the name
                lseek(fd,1,SEEK_CUR);
                for(j=0;j<15;j++)
                {
                    read(fd,&name[j],2);
                }
                nameInChar=unicode2ascii(name,nameLength);
            }
            else
            {
                lseek(fd,31,SEEK_CUR);
            }
            
            if(nameFlag==1)
            {
                nameFlag=0;
                //Print "-" according to level of recursion
                for(i=0;i<level;i++)
                {
                    printf("-");
                }
                //Recursively call this fucntion if it is a directory
                if(dirFlag==1)
                {
                    dirFlag=0;
                    //Print the name of the directory
                    printf("Directory: %s\n",nameInChar);
                    //Recursively call the function
                    recList(fd,mainSec,clstr,level+1);
//                    //Go to the cluster heap
//                    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
//                    //GO to the cluster according to the cluster chain
//                    lseek(fd,((clusterChain[index]-2)*clusterLength)+(i-2)*32,SEEK_CUR);
                }
                else
                {
                    printf("File: %s\n",nameInChar);
                }
            }
        }
        index++;
    }
}
//void getList(int fd,MBS* mainSec)
//{
//    int i;
//    uint8_t type;
//    //Go to the cluster heap
//    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
//    lseek(fd,(mainSec->firstClusterOfRootDir-2)*clusterLength,SEEK_CUR);
//    //Find the volume label
//    for(i=0;i<clusterLength/32;i++)
//    {
//        read(fd,&type,1);
//        printf("Type: %02x\n",type);
//        if(type==0x85 && i>6)
//        {
//            break;
//        }
//        lseek(fd,31,SEEK_CUR);
//    }
//    lseek(fd,3,SEEK_CUR);
//    uint16_t fileAtr;
//    read(fd,&fileAtr,2);
//    printf("fileAtr: %u\n",fileAtr);
//    fileAtr=fileAtr >> 4;
//    if((fileAtr & 0x01)==1)
//    {
//        printf("Directory\n");
//    }
//    else
//    {
//        printf("File\n");
//    }
//    lseek(fd,26,SEEK_CUR);
//    read(fd,&type,1);
//    if(type==0xC0)
//    {
//        printf("Found Stream\n");
//    }
//    uint8_t nameLength;
//    lseek(fd,2,SEEK_CUR);
//    read(fd,&nameLength,1);
//    uint32_t clstr;
//    lseek(fd,16,SEEK_CUR);
//    read(fd,&clstr,4);
//    //uint32_t* chain=buildClusterChain(fd,mainSec,clstr);
//    //Go to the start of the cluster heap
//    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
//    //Go to the cluster which contains the allocation bitmap
//    lseek(fd,(clstr-2)*clusterLength,SEEK_CUR);
////    int j;
////    int count=0;
////    int index=1;
//    for(i=0;i<clusterLength/32;i++)
//    {
//        read(fd,&type,1);
//        printf("Type: %02x\n",type);
//        if(type==0x85)
//        {
//            break;
//        }
//        lseek(fd,31,SEEK_CUR);
//    }
//    lseek(fd,3,SEEK_CUR);
//    read(fd,&fileAtr,2);
//    printf("fileAtr: %08x\n",fileAtr);
//    fileAtr=fileAtr >> 4;
//    if((fileAtr & 0x01)==1)
//    {
//        printf("Directory\n");
//    }
//    else
//    {
//        printf("File\n");
//    }
//    lseek(fd,26,SEEK_CUR);
//    read(fd,&type,1);
//    if(type==0xC0)
//    {
//        printf("Found Stream\n");
//    }
//    lseek(fd,2,SEEK_CUR);
//    read(fd,&nameLength,1);
//
//    lseek(fd,28,SEEK_CUR);
//    read(fd,&type,1);
//    if(type==0xC1)
//    {
//        printf("Found File\n");
//    }
//    uint16_t name[15];
//    lseek(fd,1,SEEK_CUR);
//    for(i=0;i<15;i++)
//    {
//        read(fd,&name[i],2);
//    }
//    char* nn=unicode2ascii(name,nameLength);
//    printf("Name %s\n",nn);
//}

int main(int argc,char* argv[])
{
    assert(argc>0);
    assert(argv!=NULL);
    MBS* mainSec=(MBS*)(malloc(sizeof(MBS)));
    char* fileName=argv[1];
    int fd=open(fileName,O_RDONLY);
    read(fd,mainSec,sizeof(MBS));
    sectorLength=(int)pow(2,(int)mainSec->bytesPerSectorShift);
    clusterLength=(int)pow(2,(int)mainSec->sectorsPerClusterShift) * sectorLength;

    if(strcmp(argv[2],"info")==0)
    {
        getInfo(fd,mainSec);
    }
    if(strcmp(argv[2],"list")==0)
    {
        recList(fd,mainSec,mainSec->firstClusterOfRootDir,0);
    }
    //Clean up
    close(fd);
    free(mainSec);
    return EXIT_SUCCESS;
}

    //
    //        ////////////////////////////////
    //        for(i=0;i<clusterLength/32;i++)
    //        {
    //            read(fd,&type,1);
    //            printf("Type: %02x\n",type);
    //            //If we find a directory file entry
    //            if(type==0x85)
    //            {
    //                //Check if it is a file or directory
    //                lseek(fd,3,SEEK_CUR);
    //                read(fd,&fileAtr,2);
    //                //printf("fileAtr: %u\n",fileAtr);
    //                fileAtr=fileAtr >> 4;
    //                //Directory
    //                if((fileAtr & 0x01)==1)
    //                {
    //                    //Check if the stream extension exists
    //                    lseek(fd,26,SEEK_CUR);
    //                    read(fd,&type,1);
    //                    if(type==0xC0)
    //                    {
    //                       // printf("Found Stream\n");
    //                        //Read the length of the file name
    //                        lseek(fd,2,SEEK_CUR);
    //                        read(fd,&nameLength,1);
    //                        //Get the first cluster of the directory
    //                        lseek(fd,16,SEEK_CUR);
    //                        read(fd,&clstr,4);
    //                        //Go to the file name dir entry
    //                        lseek(fd,8,SEEK_CUR);
    //                        read(fd,&type,1);
    //                        if(type==0xC1)
    //                        {
    //                            //Get the name
    //                            lseek(fd,1,SEEK_CUR);
    //                            for(i=0;i<15;i++)
    //                            {
    //                                read(fd,&name[i],2);
    //                            }
    //                            char* nn=unicode2ascii(name,nameLength);
    //                            //Print "-" according to level of recursion
    //                            for(i=0;i<level;i++)
    //                            {
    //                                printf("-");
    //                            }
    //                            //Print the name of the directory
    //                            printf("Directory: %s\n",nn);
    //                            free(nn);
    //                            //Recurse
    //                            //recList(fd,mainSec,clstr,level+1);
    //                        }
    //                    }
    //
    //                    //If it doesn't and it is the last entry of the cluster, check if it exists in the next cluster
    //                    else if((i==(clusterLength/32)-1) && clusterChain[index+1]!=UINT32_MAX)
    //                {
    //                    //Go to the next cluster
    //                    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
    //                    lseek(fd,(clusterChain[index+1]-2)*clusterLength,SEEK_CUR);
    //                    //Check if the first entry is the stream extension
    //                    read(fd,&type,1);
    //                    if(type==0xC0)
    //                    {
    //                        // printf("Found Stream\n");
    //                        //Read the length of the file name
    //                        lseek(fd,2,SEEK_CUR);
    //                        read(fd,&nameLength,1);
    //                        //Get the first cluster of the directory
    //                        lseek(fd,16,SEEK_CUR);
    //                        read(fd,&clstr,4);
    //                        //Go to the file name dir entry
    //                        lseek(fd,8,SEEK_CUR);
    //                        read(fd,&type,1);
    //                        if(type==0xC1)
    //                        {
    //                            //Get the name
    //                            lseek(fd,1,SEEK_CUR);
    //                            for(i=0;i<15;i++)
    //                            {
    //                                read(fd,&name[i],2);
    //                            }
    //                            char* nn=unicode2ascii(name,nameLength);
    //                            //Print "-" according to the level of recursion
    //                            for(i=0;i<level;i++)
    //                            {
    //                                printf("-");
    //                            }
    //                            printf("Directory: %s\n",nn);
    //                            free(nn);
    //                            //Recurse
    //                            //recList(fd,mainSec,clstr,level+1);
    //                        }
    //                    }
    //                }
    //                else
    //                {
    //                    //Do nothing
    //                }
    //            }
    //            //File (Base Case)
    //            else
    //            {
    //                //printf("File\n");
    //            }
    //        }
    //        //Go to the next directory entry
    //        else
    //        {
    //            lseek(fd,31,SEEK_CUR);
    //        }
    //
    //    }
    //        index++;
    //    }
    //    free(clusterChain);
