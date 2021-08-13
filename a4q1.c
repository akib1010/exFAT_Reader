//-------------------------------------
//Name: Farhan Akib Rahman
//Student Number:7854163
//Course: Comp3439
//Assignment: 4 , Question:1
//
//Remarks:A program which reads an exFAT image and can run 3 commands (info,list,get)
//-------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <math.h>

//Global Variables
int sectorLength;//Length of a sector in bytes
int clusterLength;//Length of a cluster in bytes

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

#define MAX_PATH_SIZE 100

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

//This fucntion returns an array which represents the cluster chain, the first cluster of the file is used as a parameter
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
            if(temp[i]==UINT32_MAX ||temp[i]==0)
            {
                //If it is the last position assign UINT32_MAX to represent end of list
                temp[i]=UINT32_MAX;
                count++;
                break;
            }
        }
        count++;
    }
    //Heap allocation
    uint32_t* result;
    result=(uint32_t*)(malloc(sizeof(uint32_t)*count));
    for(i=0;i<count;i++)
    {
        result[i]=temp[i];
        //printf("Entry: %u\n", result[i]);
    }
    
    return result;
}


//This fucntion is used to print the "info" about the file
void getInfo(int fd,MBS* mainSec)
{
    int i;
    uint8_t type;
    //Go to the cluster heap and seek to the root directory
    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
    lseek(fd,(mainSec->firstClusterOfRootDir-2)*clusterLength,SEEK_CUR);
    //Find the volume label
    for(i=0;i<clusterLength/32;i++)
    {
        read(fd,&type,1);
        //If entry type matches we found the volume label
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
    //Convert the unicode to ascii
    char* label=unicode2ascii(labelUnicode,labelLength);
    //Calculating the free space
    uint32_t bitmapLocation=0;
    for(i=0;i<clusterLength/32 && bitmapLocation==0;i++)
    {
        //Go to the cluster heap and seek to the root directory
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

    //Make the cluster chain for the bitmap
    uint32_t* clusterChain= buildClusterChain(fd,mainSec,bitmapLocation);
    uint8_t oneByte;//used to read the cluster 1 byte at a time
    int popCount=0;//used to Count the unset bits
    //Now go to the cluster that holds the allocation bitmap
    //Go to the start of the cluster heap
    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
    //Go to the cluster which contains the allocation bitmap
    lseek(fd,(bitmapLocation-2)*clusterLength,SEEK_CUR);
    int j;
    int count=0;//used to count the number of clusters
    int index=1;//used as an index to the cluster chain
    for(i=0;i<(int)(mainSec->clusterCount-1);i+=8)
    {
        read(fd,&oneByte,1);
        //Counting the 8 bits of 1 byte
        for(j=0;j< 8; j++)
        {
            if((oneByte & 0x01)==0)
            {
                popCount++;
            }
            oneByte = oneByte >> 1;
        }
        count++;
        //if the cluster is over
        if(count==clusterLength)
        {
            //Go to the start of the cluster heap
            lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
            //Go to the next cluster which contains the next allocation bitmap
            lseek(fd,(clusterChain[index]-2)*clusterLength,SEEK_CUR);
            //If we reach the end of list
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
    //Clean up
    free(clusterChain);
    free(label);
}

//This fucntion traverses through all the directories and files in the image recursively
void recList(int fd,MBS* mainSec,uint32_t clusterIndex,int level)
{
    //Build the cluster chain of the file/directory
    uint32_t* clusterChain=buildClusterChain(fd,mainSec,clusterIndex);
    int index=0;
    int i;
    int j;
    int z;
    uint8_t type;//used to read the entry type
    uint8_t nameLength;//used to get the length of the file name
    uint32_t clstr;//used to store the firstCluster of the file/directory
    uint16_t name[15];//To read the file name
    uint16_t fileAtr;//To read the fileAttributes
    char* nameInChar;//Used to store the name of the file in ascii
    //Flags used to check the validity of the file dir entries
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
        for(i=0;i<clusterLength/32;i++)
        {
            read(fd,&type,1);
            //Check for the file directory
            if(type==0x85)
            {
                fileFlag=1;
                //get the value which indicates a file or directory
                lseek(fd,3,SEEK_CUR);
                read(fd,&fileAtr,2);
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
                //Get the name Length
                read(fd,&nameLength,1);
                //Skip 16 bytes
                lseek(fd,16,SEEK_CUR);
                //Get the first cluster of the directory
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
                //Convert the name
                nameInChar=unicode2ascii(name,(uint8_t)15);
            }
            else
            {
                //Go to the next dir entry
                lseek(fd,31,SEEK_CUR);
            }
            
            //If we found a file name
            if(nameFlag==1)
            {
                nameFlag=0;
                //Print "-" according to level of recursion
                for(z=0;z<level;z++)
                {
                    printf("-");
                }
                //Recursively call this fucntion if it is a directory
                if(dirFlag==1)
                {
                    dirFlag=0;
                    //Print the name of the directory
                    printf("Directory: %s\n",nameInChar);
                    //Recursion
                    recList(fd,mainSec,clstr,level+1);
                    //The position in the file changes after recursion
                    //Go back to old position
                    lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
                    lseek(fd,(clusterChain[index]-2)*clusterLength,SEEK_CUR);
                    lseek(fd,(i+1)*32,SEEK_CUR);
                }
                //Print the file name if it is a file
                else
                {
                    printf("File: %s\n",nameInChar);
                }
            }
        }//for
        index++;
    }//while
    //Clean up
    free(clusterChain);
    free(nameInChar);
}

//This fucntion copies a file from the given image to the current directory
void getFile(int fd,MBS* mainSec,char* line)
{
    //Parse the path of the file
    char* path[MAX_PATH_SIZE];
    char* token;
    int count=0;//Number of file/directory in the path
    token=strtok(line,"/");
    while(token!=NULL)
    {
        path[count]=strdup(token);
        count++;
        token=strtok(NULL,"/");
    }
    //
    //Build the cluster chain
    uint32_t* clusterChain;
    clusterChain= buildClusterChain(fd,mainSec,mainSec->firstClusterOfRootDir);
    uint8_t type;//used to get the entry type
    uint8_t nameLength;//used to get the length
    uint32_t clstr;//used to get the first cluster
    uint16_t name[15];//used to read the name
    char* nameInChar;
    int found;//Flag used to check if we found the file/directory that is needed
    int i;
    int j;
    int z;
    int k;
    //Search for the file
    for(i=0;i<count;i++)
    {
        j=0;
        found=0;
        while(clusterChain[j]!=UINT32_MAX && found==0)
        {
            z=0;
            //Go to the location of according to the cluster chain
            lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
            lseek(fd,(clusterChain[j]-2)*clusterLength,SEEK_CUR);
            //Go through the cluster
            while(z<clusterLength/32 && found==0)
            {
                read(fd,&type,1);
                //If we find the file directory entry
                if(type==0x85)
                {
                    //Go to the next dir entry
                    lseek(fd,31,SEEK_CUR);
                    z++;
                    //if the cluster is finished midway Jump to the next cluster
                    if(z>=(clusterLength/32))
                    {
                        lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
                        lseek(fd,(clusterChain[j+1]-2)*clusterLength,SEEK_CUR);
                    }
                    //Check if it is the stream extension
                    read(fd,&type,1);
                    if(type==0xC0)
                    {
                        //Read the length of the file name
                        lseek(fd,2,SEEK_CUR);
                        read(fd,&nameLength,1);
                        //Get the first cluster of the directory
                        lseek(fd,16,SEEK_CUR);
                        read(fd,&clstr,4);
                        //Go to the next dir entry
                        lseek(fd,8,SEEK_CUR);
                        z++;
                        //if the cluster is finished midway Jump to the next cluster
                        if(z>=(clusterLength/32))
                        {
                            lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
                            lseek(fd,(clusterChain[j+1]-2)*clusterLength,SEEK_CUR);
                        }
                        //Check if it is the file name directory
                        read(fd,&type,1);
                        if(type==0xC1)
                        {
                            //Get the name
                            lseek(fd,1,SEEK_CUR);
                            for(k=0;k<15;k++)
                            {
                                read(fd,&name[k],2);
                            }
                            z++;
                            //Convert the name
                            nameInChar=unicode2ascii(name,nameLength);
                            if(strcmp(nameInChar,path[i])==0)
                            {
                                //We found the required directory/file
                                found=1;
                            }
                        }
                    }
                }
                else
                {
                    //Go to the next dir entry
                    lseek(fd,31,SEEK_CUR);
                    z++;;
                }
            }//While
            j++;
        }//While
        if(found==1)
        {
            free(clusterChain);
            //Build the cluster chain of the file that has been found
            clusterChain=buildClusterChain(fd,mainSec,clstr);
        }
    }//for
    
    //If we found the file
    if(found==1)
    {
        printf("Found file: %s\n",nameInChar);
        //Open or create the file
        int newFd=open(nameInChar,O_RDONLY|O_WRONLY|O_CREAT);
        uint8_t content[clusterLength];//used to the bytes in a cluster
        i=0;
        //Copy the file out of the image
        while(clusterChain[i]!=UINT32_MAX)
        {
            //Go to the location according to the cluster chain
            lseek(fd,mainSec->clusterHeapOffset*sectorLength,SEEK_SET);
            lseek(fd,(clusterChain[i]-2)*clusterLength,SEEK_CUR);
            if(clusterChain[i]!=0)
            {
                //Read from the cluster and write to the file
                read(fd,&content,clusterLength);
                write(newFd,&content,clusterLength);
            }
            i++;
        }//while
    }
    else
    {
        printf("File not found\n");
    }
    //clean up
    free(clusterChain);
    free(nameInChar);
}


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
    if(strcmp(argv[2],"get")==0)
    {
        getFile(fd,mainSec,argv[3]);
    }
    //Clean up
    close(fd);
    free(mainSec);
    return EXIT_SUCCESS;
}
