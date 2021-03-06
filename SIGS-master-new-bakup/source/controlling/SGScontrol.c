/*

    Name: Xu Xi-Ping
    Date: March 8,2017
    Last Update: March 8,2017
    Program statement: 
        In here, We define the functions for controlling child processes.
        
*/


#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <unistd.h>

#include "SGScontrol.h"
#include "../ipcs/SGSipcs.h"

int sgsInitControl(char *processName)
{

    char buf[128];
    FILE *fd = NULL;

    memset(buf,'\0',sizeof(buf));

    sprintf(buf, "./pid/%s.pid",processName);

    if((fd=fopen(buf, "w")) == NULL)
    {

        printf( "[mongo:%d] failed in fopen(%s)! %s",__LINE__, buf, strerror(errno));
        return -1;

    }

    fprintf(fd, "%d\n", getpid());
    fclose(fd);
    printf("write done\n");
    return 0;

}

void sgsChildAbort(int sigNum)
{

    //sgsDeleteDataInfo(dataInfoPtr, -1);
    //sgsDeleteDeviceInfo(deviceInfoPtr);
    printf("[%s,%d] Catched SIGUSR1 successfully\n",__FUNCTION__,__LINE__);
    exit(0);

    return;

}

