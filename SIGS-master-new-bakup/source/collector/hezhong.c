/*

    Name: Xi-Ping Xu
    Date: July 19,2017
    Last Update: August 17,2017
    Program statement: 
        This is a agent that collect data from delta-rpi and irr meter 
        It has following functions:
        1. Register two data buffers to the data buffer pool (delta-rpi and irr)
            1. Init dataInfo
            2. Read in port pathes and open ports
            (Plan to use a struct array to store these two infos)
        2. Refresh data buffer every 30 seconds (including Inverter and irr meter)
            1.fetch data from modbus
            2.Update data buffer
        3. Receive, execute and return queue messages

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "../ipcs/SGSipcs.h"
#include "../events/SGSEvent.h"
#include "../controlling/SGScontrol.h"
#include "../protocol/SGSmodbus.h"

#define HOT 3
#define WARM 2
#define COLD 1
#define WL_addr 211
#define SW_addr 411
#define Alert_addr 335
#define SP_addr 305

//#define SIMULATION

int RegisterDataBuffer(char *infoName, int sharedMemoryId, int numberOfInfo);

int FetchAndUpdateInfoTable();

int SimulateAndUpdateInfoTable();

int CheckAndRespondQueueMessage();

int ShutdownSystemBySignal(int sigNum);

int InitInfoTable(int *tagNum);

int OpenPorts();

int CheckSwitch();

int init_Valve();

int init_Consumption();

int sgsWriteValve();

int sgsWriteConsumption();

int SetCmd(char cmd[]);

int MakeControlCmd(int control_type , int control_val);

int DoModbusCmd(int addr, int val);

void substr(char *dest, const char* src, unsigned int start, unsigned int cnt);

int DoModbusCmd_Time(int addr, char *val);

typedef struct 
{

    char infoName[32];      //  The name of the infoTable
    char portPath[32];
    char portParam[32];
    int fd;                 //  it stores the file descriptor that represents port

}infoTable;

infoTable iTable[10]; //I suggest we use this to help us match the ports and data

dataInfo *dInfo;    // pointer to the shared memory
dataInfo *dValve, *dHotC, *dWarmC, *dColdC;   // pointer to the modbusInfo 'Valve,...' in shard memory
int interval = 60;  // time period between last and next collecting section
int eventHandlerId; // Message queue id for event-handler
int shmId;          // shared memory id
int msgId;          // created by sgsCreateMessageQueue
int msgType;        // 0 1 2 3 4 5, one of them

unsigned char switch_cmd[128];

int heating = 0 , watercharging = 0, compression = 0 ;

float HotConsumption = 0.0 , WarmConsumption = 0.0 , ColdConsumption = 0.0;
float HotValve = 0.0 , WarmValve = 0.0 , ColdValve = 0.0;

int p=0;
int logger_flag = 0;
int reboot = 0;
int test_cooling =0;
int main(int argc, char *argv[])
{

    int i = 0, ret = 0, numberOfData = 0, looping = 0;
    char buf[512];
    FILE *fp = NULL;
    time_t last, now;
    struct sigaction act, oldact;  

    printf("hezhong Collector starts---\n");

    memset(buf,'\0',sizeof(buf));

    snprintf(buf,511,"%s;argc is %d, argv 1 %s", LOG, argc, argv[1]);

    msgType = atoi(argv[1]);

    act.sa_handler = (__sighandler_t)ShutdownSystemBySignal;
    act.sa_flags = SA_ONESHOT|SA_NOMASK;
    sigaction(SIGTERM, &act, &oldact);

    //Ignore SIGINT
    
    signal(SIGINT, SIG_IGN);

    //Open message queue

    eventHandlerId = sgsCreateMsgQueue(EVENT_HANDLER_KEY, 0);
    if(eventHandlerId == -1)
    {
        printf("Open eventHandler queue failed...\n");
        exit(0);
    }

    msgId = sgsCreateMsgQueue(COLLECTOR_AGENT_KEY, 0);
    if(msgId == -1)
    {
        printf("Open Collector Agent queue failed...\n");
        exit(0);
    }

    //Initialize infoTable

    ret = InitInfoTable(&numberOfData);
    if(ret == -1)
    {

        printf("Failed to initialize infoTable, return %d\n", ret);
        exit(0);

    }

    //Attach buffer pool

    ret = sgsInitBufferPool(0);

    //Registration

    ret = sgsRegisterDataInfoToBufferPool("hezhong", shmId, numberOfData);
    if(ret == -1)
    {

        printf("Failed to register, return %d\n", ret);
        sgsDeleteDataInfo(dInfo, shmId);
        exit(0);

    }

    //Open modbus ports

#ifndef SIMULATION

    ret = OpenPorts();    
    if(ret == -1)
    {

        printf("Failed to Open ports, return %d\n", ret);
        sgsDeleteDataInfo(dInfo, shmId);
        exit(0);

    }

#endif

    ReadySwitchCmd();
    init_Consumption();

    ret = init_Valve();
    if(ret<=0){
        printf("Failed to init_Valve, return %d\n", ret);
    }
    //get first timestamp

    time(&last);
    now = last;

    //main loop

    while(1) 
    {

        //usleep(100000);
        time(&now);

        //check time interval

        //if( ((now-last) >= (interval +2) ))
        //char *pop = "000120002001";
        //DoModbusCmd_Time(309, pop);

        if(1)
        {

            //Update data

#ifdef SIMULATION

            printf("simulate new data\n");
            ret = SimulateAndUpdateInfoTable();

#else

            //printf("generate new data\n");
            usleep(1);
            logger_flag = 0 ;
            ret = FetchAndUpdateInfoTable();

#endif

            //printf("show data\n");
            //sgsShowDataInfo(dInfo);

            
            if( ((now-last) >= (interval +2) )){

                // reboot++;
                // if(reboot>300)
                //     system("reboot");

                if(logger_flag == 0 ){
                    sgsWriteValve();
                    sgsWriteConsumption();
                    usleep(300);
                    sgsSendQueueMsg(eventHandlerId, "[Control];SGSlogger;hezhong;SaveLogNow", EnumLogger);
                }
                time(&last);
                now = last;
                last -= 2;
            }

        }
        else
        {
            looping = 0;
        }

        //Check message
        usleep(10000);
        ret = CheckAndRespondQueueMessage();

    }

}

int RegisterDataBuffer(char *infoName,int sharedMemoryId, int numberOfInfo)
{

    int ret = -1;

    ret = sgsRegisterDataInfoToBufferPool(infoName, sharedMemoryId, numberOfInfo);
    if(ret != 0)
    {

        printf("Registration failed, return %d\n", ret);
        printf("Delete dataInfo, shmid %d\n",ret);
        sgsDeleteDataInfo(dInfo, ret);
        exit(0);

    }
    return 0;

}

int FetchAndUpdateInfoTable()
{


    int i = 0, ret = 0;
	int flag = 1;
	int type = 0;
    int tmp1 = 0, tmp2 = 0;
    dataLog dLog , dWaterUsingSataus;
    dataInfo *dataTemp = dInfo;
    
	struct timeval start;
    struct timeval end;
    unsigned char buff[2];
    unsigned long diff;
	float using = 0;
    int savepower=0;

    //Loop all info tags

    while(dataTemp != NULL)
    {
		usleep(50);
        //Do a preprocess of detecting switch
		
        while(1)
		{
			//printf("CheckSwitch called\n");
            ret = CheckSwitch();
            
            if(ret > 0)
            //if(0)
			{	               
                
                //First valve press
                if(type == 0){
                    gettimeofday(&start,NULL);

                    if(ret == HOT)
                    {
                        type = HOT;
                        printf("type change to HOT\n");					
                    }
                    if(ret == WARM)
                    {
                        type = WARM;
                        printf("type change to WARM\n");					
                    }
                    if(ret == COLD)
                    {
                        type = COLD;
                        printf("type change to COLD\n");					
                    }
                }
				// quickly change switch
				else if(type != ret)
				{
									
					
					printf("There's someone change type of the switch\n");
					gettimeofday(&end,NULL);
					diff = 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
					printf("the difference is %ld\n",diff);
					gettimeofday(&start,NULL);
					using = diff * 41.37 / 1000000.0 ;
					printf("the using water is %lf\n",using);
					
					if(type == HOT){
                        HotConsumption += using;
                        HotValve += diff;
                    }
					if(type == COLD){
                        ColdConsumption += using;
                        ColdValve += diff;
                    }
					if(type == WARM){
                        WarmConsumption += using;		
                        WarmValve += diff;			
                    }
                    
                    
                    if(ret == HOT)
                    {
                        type = HOT;
                        printf("quickly change switch HOT the switch\n");				
                    }
                    if(ret == WARM)
                    {
                        type = WARM;
                        printf("quickly change switch WARM the switch\n");				
                    }
                    if(ret == COLD)
                    {
                        type = COLD;
                        printf("quickly change switch COLD the switch\n");				
                    }
				}
				//printf("tv_sec:%d\n",start.tv_sec);
				//printf("tv_usec:%d\n",start.tv_usec);
				//printf("tz_minuteswest:%d\n",start.tz_minuteswest);
				//printf("tz_dsttime:%d\n",start.tz_dsttime);
				//			
			}
			else
			{
				if(type == 0)
					break;
				
				//printf("There's someone close the (%d) switch\n",type);		
				gettimeofday(&end,NULL);
				diff = 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
				//printf("the difference is %ld\n",diff);
				using = diff * 41.37 / 1000000.0 ;
				//printf("the using water is %lf\n",using);
				
				if(type == HOT){
                    HotConsumption += using;
                    HotValve += diff;
                }
                if(type == COLD){
                    ColdConsumption += using;
                    ColdValve += diff;
                }
                if(type == WARM){
                    WarmConsumption += using;		
                    WarmValve += diff;			
                }
				
				type = 0;
                sgsWriteValve();
                sgsWriteConsumption();
                usleep(30000);
                sgsSendQueueMsg(eventHandlerId, "[Control];SGSlogger;hezhong;SaveLogNow", EnumLogger);  
                logger_flag = 1;
                //sgsSendQueueMsg(eventHandlerId, "[Control];To;From;Command", EnumCollector);  
				break;
			}
        }

       
    
        
        //prevent data is null
        /*
        if(p>20){                
            sgsWriteValve();
            sgsSendQueueMsg(eventHandlerId, "[Control];SGSlogger;SolarCollector;SaveLogNow", EnumLogger);
        }
        else
            p++;
        */
		quicklySwitchInfo:
		
		// for quicklySwitchInfo Tag using
		if(dataTemp == NULL)
				return -1;
            
       
		
		
		
		
        //Retrieve data
        if(dataTemp->modbusInfo.address != 999){

            ret = sgsSendModbusCommandRTU(dataTemp->modbusInfo.cmd,8,330000,dataTemp->modbusInfo.response);
       
            if(ret < 0)
            {

                printf("Read address %d failed\n",dataTemp->modbusInfo.address);

            }
            // if(ret >= 0)
            // {        
            //     printf("read address:%d name:%s ",dataTemp->modbusInfo.address, dataTemp->valueName);
            //     printf("respense:\n");
            //     for(i=0; i<ret; i++)
            //         printf(" %02x",dataTemp->modbusInfo.response[i]);
        
            //     printf("\n");
        
            // }
            // modbus return data successfully
            if(dataTemp->modbusInfo.response[2] == 2) //length of modbus data 
            {
                

                memset(buff,'\0',sizeof(buff));
                
                buff[0] = dataTemp->modbusInfo.response[3];
                buff[1] = dataTemp->modbusInfo.response[4];
                
                quicklySetWaterevel:
                
                // for quicklySetWaterevel Tag using
                if(dataTemp == NULL)
                    return -1;
                
                
                dLog.valueType = STRING_VALUE;
                memset(dLog.value.s,'\0',sizeof(dLog.value.s));
                
                
                //Caclate value normal status (waterlevel 211)
                if(dataTemp->modbusInfo.address == WL_addr)
                {				
                
                
                    if(!strcmp(dataTemp->valueName, "LowWaterLevel"))
                    {
                        if(buff[1] & 0x01)
                        {
                            //printf("low water lever\n");
                            strcpy(dLog.value.s , "1");					
                        }
                        else				
                            strcpy(dLog.value.s , "0");		
                            
                        sgsWriteSharedMemory(dataTemp, &dLog);   
                        dataTemp = dataTemp->next;
                    
                        goto quicklySetWaterevel;
                    }
                    
                    else if(!strcmp(dataTemp->valueName, "MeanWaterLevel"))
                    {
                        
                        if(buff[1] & 0x02)
                        {
                            //printf("mean water lever\n");
                            strcpy(dLog.value.s , "1");	
                            
                        }
                        else				
                            strcpy(dLog.value.s , "0");	

                        sgsWriteSharedMemory(dataTemp, &dLog);   
                        dataTemp = dataTemp->next;
                    
                        goto quicklySetWaterevel;
                    }
                    
                    else if(!strcmp(dataTemp->valueName, "HighWaterLevel"))
                    {
                        if(buff[1] & 0x04)
                        {
                            //printf("high water lever\n");		
                            strcpy(dLog.value.s , "1");						
                        }
                        else				
                            strcpy(dLog.value.s , "0");	
                            
                        sgsWriteSharedMemory(dataTemp, &dLog);   
                    
                    
                    }
                    
                    
                    
                }
                 //Set value saving power status ( info  305) ....
                else if(dataTemp->modbusInfo.address == SP_addr)
                {
                    dLog.valueType = STRING_VALUE;
                    memset(dLog.value.s,'\0',sizeof(dLog.value.s));

                    //printf("modbusInfo.address %d\n",dataTemp->modbusInfo.address);

                    if(!strcmp(dataTemp->valueName, "SavingPower"))
                    {               
                        // if(dataTemp->modbusInfo.response[3] & 0x01)
                        //     printf("saving power on!\n");
                        // else
                        //     printf("saving power off!\n");                      
                        
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%d",dataTemp->modbusInfo.response[3] & 0x01);
                        sgsWriteSharedMemory(dataTemp, &dLog);
                       
                    }
		    else if(!strcmp(dataTemp->valueName, "Sterilizing"))
		    {
			snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%d",dataTemp->modbusInfo.response[4] & 0x80);
			sgsWriteSharedMemory(dataTemp, &dLog);
		    }

                }
                //Set value switch status (Switch info  411) Heating....
                else if(dataTemp->modbusInfo.address == SW_addr)
                {			
                    
                    
                    dLog.valueType = STRING_VALUE;
                    memset(dLog.value.s,'\0',sizeof(dLog.value.s));
                    
                    //printf("modbusInfo.address %d\n",dataTemp->modbusInfo.address);
                
                    
                    
                    if(!strcmp(dataTemp->valueName, "Heating"))
                    {               
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%d",heating);
                    }
                    
                    else if(!strcmp(dataTemp->valueName, "Compression"))
                    {
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%d",compression);
                    }
                    
                    else if(!strcmp(dataTemp->valueName, "WaterCharge"))
                    {
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%d",watercharging);
                    }				
                    /*
                    else if(!strcmp(dataTemp->valueName, "HotConsumption"))
                    {
                        sgsReadSharedMemory(dataTemp,&dWaterUsingSataus);
                        //snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f",HotConsumption);	
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0",);
                                        
                        if(dWaterUsingSataus.status == 1)
                            HotConsumption = 0.0;
                    }
                    
                    else if(!strcmp(dataTemp->valueName, "WarmConsumption"))
                    {
                        sgsReadSharedMemory(dataTemp,&dWaterUsingSataus);
                        //snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f",WarmConsumption);
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0");
                        
                        if(dWaterUsingSataus.status == 1)
                            WarmConsumption = 0.0;
                    }
                    
                    else if(!strcmp(dataTemp->valueName, "ColdConsumption"))
                    {
                        sgsReadSharedMemory(dataTemp,&dWaterUsingSataus);
                        //snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f",ColdConsumption);
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0");
                        
                        if(dWaterUsingSataus.status == 1)
                            ColdConsumption = 0.0;
                    }
                    */
                    sgsWriteSharedMemory(dataTemp, &dLog); 
                    
                    dataTemp = dataTemp->next;
                    
                    goto quicklySwitchInfo;
                    
                }
                //Caclate value normal value
                else
                {
                
                    //printf("dataTemp->modbusInfo.address : %d\n",dataTemp->modbusInfo.address);
                    if(dataTemp->modbusInfo.address == Alert_addr)
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%02x%02x",dataTemp->modbusInfo.response[3],dataTemp->modbusInfo.response[4]);
                        
                    else
                    {
                        tmp1 = dataTemp->modbusInfo.response[3]*256 + dataTemp->modbusInfo.response[4] ;            
                        snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%d",tmp1);
                    }

                    
                    // cooling test
                    int poc;
                    int count =0;
                    if(dataTemp->modbusInfo.address == 209 && compression==1 && test_cooling==1){
                        poc = dataTemp->modbusInfo.response[3]*256 + dataTemp->modbusInfo.response[4];                          
                        printf(LIGHT_GREEN"cooling now %d\n"NONE,poc);
                        if(poc < 21){
                            while(1){
                                ret = DoModbusCmd(121 , 30*10);
                                if (ret > 1){
                                    printf(LIGHT_GREEN"cooling test %d\n"NONE,30*10);
                                    test_cooling = 2;
                                    break;
                                }
                                else if(count>10){
                                    printf(LIGHT_RED"cooling test  Set Cooling_val %d\n"NONE,30*10);
                                    break;
                                }
                
                                count++;
                                
                            }
                        }                       

                    }
                    else  if(dataTemp->modbusInfo.address == 209 ){
                        poc = dataTemp->modbusInfo.response[3]*256 + dataTemp->modbusInfo.response[4];                          
                        printf(LIGHT_GREEN"cooling now %d\n"NONE,poc);
                        if(dataTemp->modbusInfo.address == 209 && compression==1 && test_cooling==0)
                        {
                            if(poc >= 213){
                                test_cooling = 1;
                            }
                        }
                    } 
                   

                    //memset(dLog.value.s,'\0',sizeof(dLog.value.s));
                    

                    //memset(dLog.sensorName,'\0',sizeof(dLog.sensorName));
                    //strncpy(dLog.sensorName,dataTemp->sensorName,32);

                    //memset(dLog.valueName,'\0',sizeof(dLog.valueName));
                    //strncpy(dLog.valueName,dataTemp->valueName,32);
                    if(dataTemp->modbusInfo.response[2] == 2)
                        sgsWriteSharedMemory(dataTemp, &dLog);   
                
                }
            }		        
        }        

	 dataTemp = dataTemp->next;
    }


    return 0;

}

int SimulateAndUpdateInfoTable()
{

    dataInfo *tmpInfo = NULL;
    dataLog dLog;
    int ret = -1, i = 0, j = 0, shift = 0, bitPos = 0;
    char buf[5] = {0}, codeName = 'E';
    char *namePart = NULL;
    unsigned char preCmd[8] = {0}, cmd[8] = {0}, res[64] = {0}, bit = 0x01;
    unsigned int crc = 0;

    //Loop all info tags

    tmpInfo = dInfo ;
    while(tmpInfo != NULL)
    {

        namePart = strtok(tmpInfo->valueName, "-");
        i = 0;
        if(!strcmp(tmpInfo->deviceName,"Irr"))
        {

            while(strcmp("Irr",iTable[i].infoName))
                i++;

            memset(&dLog, 0, sizeof(dLog));

            //record Irr status

            if(strstr(namePart, "Status"))
            {
                dLog.value.i = 1;
            }
            else
            {
                dLog.value.i = rand()%256*256 + rand()%256;
            }
            
            dLog.valueType = INTEGER_VALUE;
            dLog.status = 1;
            sgsWriteSharedMemory(tmpInfo, &dLog);

        }
        else if(!strcmp(tmpInfo->deviceName,"Deltarpi"))
        {

            if(strstr(namePart, "Voltage") || strstr(namePart, "Current") || strstr(namePart, "Wattage") 
            || strstr(namePart, "Frequency") || strstr(namePart, "Voltage(Vab)") || strstr(namePart, "Voltage(Vbc)")
            || strstr(namePart, "Voltage(Vca)"))
            {

                while(strcmp("Deltarpi",iTable[i].infoName))
                    i++;
                preCmd[0x00] = tmpInfo->modbusInfo.ID;
                preCmd[0x01] = 0x06;
                preCmd[0x02] = 0x03;
                preCmd[0x03] = 0x1f;
                preCmd[0x04] = 0x00;
                preCmd[0x05] = tmpInfo->modbusInfo.option;
                crc = sgsCaculateCRC(preCmd, 6);
                preCmd[0x06] = crc & 0xff00;
                preCmd[0x07] = crc & 0x00ff;

                //Set register value

                //Get register value

                //Write back to shared memory

                memset(&dLog, 0, sizeof(dLog));
                dLog.value.i = rand()%256*256 + rand()%256;;
                dLog.valueType = INTEGER_VALUE;
                dLog.status = 1;
                sgsWriteSharedMemory(tmpInfo, &dLog);

            }
            else if(strstr(namePart, "Today_Wh") || strstr(namePart, "Life_Wh"))
            {

                while(strcmp("Deltarpi",iTable[i].infoName))
                    i++;

                //Get register value

                //Write back to shared memory

                memset(&dLog, 0, sizeof(dLog));
                dLog.value.ll += (rand()%256 );
                dLog.value.ll = dLog.value.ll*256*256*256;
                dLog.value.ll += rand()%256*256*256 + rand()%256*256 + rand()%256;
                dLog.valueType = LONGLONG_VALUE;
                dLog.status = 1;
                sgsWriteSharedMemory(tmpInfo, &dLog);

            }
            else if(strstr(namePart, "Inverter_Temp"))
            {

                while(strcmp("Deltarpi",iTable[i].infoName))
                    i++;

                //Get register value

                //Write back to shared memory

                memset(&dLog, 0, sizeof(dLog));
                dLog.value.i = rand()%256*256 + rand()%256;
                dLog.valueType = INTEGER_VALUE;
                dLog.status = 1;
                sgsWriteSharedMemory(tmpInfo, &dLog);

            }
            else if(strstr(namePart, "Inverter_Error"))
            {

                while(strcmp("Deltarpi",iTable[i].infoName))
                    i++;

                memset(&dLog, 0, sizeof(dLog));

                preCmd[0x00] = tmpInfo->modbusInfo.ID;
                preCmd[0x01] = 0x04;

                preCmd[0x04] = 0x00;
                preCmd[0x05] = 0x01;
                
                shift = 0;

                for(j = 0 ; j < 10 ; j++)
                {

                    switch(j)
                    {

                        case 0:
                            codeName = 'E';
                            preCmd[0x02] = 0x0b;
                            preCmd[0x03] = 0xff;
                        break;

                        case 1:
                            preCmd[0x02] = 0x0a;
                            preCmd[0x03] = 0x00;
                            shift = 16;
                        break;

                        case 2:
                            preCmd[0x02] = 0x0a;
                            preCmd[0x03] = 0x01;
                            shift = 32;
                        break;

                        case 3:
                            codeName = 'W';
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x0f;
                        break;

                        case 4:
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x10;
                            shift = 16;
                        break;

                        case 5:
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x11;
                            shift = 32;
                        break;

                        case 6:
                            codeName = 'F';
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x1f;
                        break;

                        case 7:
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x20;
                            shift = 16;
                        break;

                        case 8:
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x21;
                            shift = 32;
                        break;

                        case 9:
                            preCmd[0x02] = 0x0c;
                            preCmd[0x03] = 0x22;
                            shift = 48;
                        break;

                        default:

                        break;



                    }

                    crc = sgsCaculateCRC(preCmd, 6);
                    preCmd[0x06] = crc & 0xff00;
                    preCmd[0x07] = crc & 0x00ff;

                    //Execute command 

                    //Parse result

                    for(bitPos = 0 ; bitPos < 16 ; bitPos++)
                    {

                        ret = -1;
                        if(bitPos == 8)
                            bit = 0x01;

                        if(bitPos < 8)
                        {
                            ret = rand()%2;
                            bit = bit << 1;
                            
                        }
                        else
                        {

                            ret = rand()%2;
                            bit = bit << 1;

                        }
                        if(ret > 0)
                        {

                            snprintf(buf, 4, "%c%02d", codeName, (bitPos + shift));
                            strcat(dLog.value.s,buf);

                        }

                    }

                }

                //Write back to shared memory
                dLog.valueType = STRING_VALUE;
                dLog.status = 1;
                sgsWriteSharedMemory(tmpInfo, &dLog);

            }
            else if(strstr(namePart, "Inverter_Status"))
            {


                while(strcmp("Deltarpi",iTable[i].infoName))
                    i++;

                //Get register value

                //Write back to shared memory

                memset(&dLog, 0, sizeof(dLog));
                dLog.value.i = 1;
                dLog.valueType = INTEGER_VALUE;
                dLog.status = 1;
                sgsWriteSharedMemory(tmpInfo, &dLog);

            }

        }

        tmpInfo = tmpInfo->next;

    }
    return 0;

}

int CheckAndRespondQueueMessage()
{

    int ret = -1;
    char buf[MSGBUFFSIZE];
    char *cmdType = NULL;
    char *from = NULL;
    char *to = NULL;
    char *content = NULL;
    char result[MSGBUFFSIZE];
    char cmd_buf[MSGBUFFSIZE];

    memset(buf,'\0',sizeof(buf));
    memset(cmd_buf,'\0',sizeof(cmd_buf));

    

    ret = sgsRecvQueueMsg(msgId, buf, msgType);
    
    if(ret != -1)
    {
        printf("sgsRecvQueueMsg buf : %s\n",buf);

        cmdType = strtok(buf,SPLITTER);
        if(cmdType == NULL)
        {
            printf("Can't get the command type. the message is incomplete\n");
            return -1;
        }

        to = strtok(NULL,SPLITTER);
        if(to == NULL)
        {
            printf("Can't get the to. the message is incomplete\n");
            return -1;
        }

        from = strtok(NULL,SPLITTER);
        if(from == NULL)
        {
            printf("Can't get the from. the message is incomplete\n");
            return -1;
        }

        //return result

        memset(result,'\0',sizeof(result));

       

        snprintf(result,MSGBUFFSIZE - 1,"%s;%s;%s;command done.",RESULT,from,to);
        
        //cmd_buf = strtok(NULL,SPLITTER);
        snprintf(cmd_buf,MSGBUFFSIZE -1, "%s", strtok(NULL,SPLITTER));
        printf("[cmd_buf] %s\n",cmd_buf);

        SetCmd(cmd_buf);

        sgsSendQueueMsg(eventHandlerId, result, EnumUploader);

        return 0;
    
    }
    return 0;

}

int ShutdownSystemBySignal(int sigNum)
{

    printf("FakeTaida bye bye\n");
    sgsDeleteDataInfo(dInfo, shmId);
    exit(0);

}

int InitInfoTable(int *tagNum)
{

    int i = 0, ret = -1;
    FILE *fp = NULL;
    char buf[256] = {0};
    char *name = NULL;
    char *path = NULL;
    char *param = NULL;
    dataInfo *tmpInfo = NULL;
   
    unsigned short crc = 0;

    //Open port config and prepare iTable

    fp = fopen("./conf/Collect/hezhong_Port","r");
    if(fp == NULL)
    {

        printf(LIGHT_RED"Failed to open ./conf/Collect/hezhong_Port, bye bye.\n "NONE);
        exit(0);
    
    }

    //init struct array

    memset(&(iTable), 0, sizeof(iTable)); 

    i = 0;
    while( !feof(fp)) //fill up the struct array with port config
    {

        memset(buf,'\0',sizeof(buf));
        if(fscanf(fp, "%[^\n]\n", buf) < 0) 
            break;

        name = strtok(buf, ";");
        path = strtok(NULL,";");
        param = strtok(NULL, ";");
        strncpy(iTable[i].infoName, name, sizeof(iTable[i].infoName));
        strncpy(iTable[i].portPath, path, sizeof(iTable[i].portPath));
        strncpy(iTable[i].portParam, param, sizeof(iTable[i].portParam));
        i++;
        

    }

    for(i = 0 ; i < 2 ; i++)
    {

        printf("iTable[%d].infoName %s, iTable[%d].portPath %s, iTable[%d].portParam %s\n", i, iTable[i].infoName, i, iTable[i].portPath, i, iTable[i].portParam);

    }

    for(i = 0 ; i < 1 ; i++)
    {

        ret = sgsInitDataInfo(NULL, &dInfo, 1, "./conf/Collect/hezhong", -1, tagNum);

        if(ret < 0 )
        {

            printf("failed to create dataInfo, ret is %d\n",ret);
            sgsSendQueueMsg(eventHandlerId,"[Error];failed to create dataInfo",9);
            exit(0);

        }

        tmpInfo = dInfo;

        while(tmpInfo != NULL)
        {

            /*if(!strcmp(tmpInfo->deviceName,"Irr"))
            {

                

            }*/
            tmpInfo->modbusInfo.cmd[0x01] = 03;

            tmpInfo->modbusInfo.cmd[0x00] = tmpInfo->modbusInfo.ID;
            
            tmpInfo->modbusInfo.cmd[0x02] = (tmpInfo->modbusInfo.address & 0xff00) >> 8;
            tmpInfo->modbusInfo.cmd[0x03] = (tmpInfo->modbusInfo.address & 0x00ff);
            tmpInfo->modbusInfo.cmd[0x04] = (tmpInfo->modbusInfo.readLength & 0xff00) >> 8;
            tmpInfo->modbusInfo.cmd[0x05] = (tmpInfo->modbusInfo.readLength & 0x00ff);
            crc = sgsCaculateCRC(tmpInfo->modbusInfo.cmd, 6);
            tmpInfo->modbusInfo.cmd[0x06] = (crc & 0xff00) >> 8;
            tmpInfo->modbusInfo.cmd[0x07] = crc & 0x00ff;

            tmpInfo = tmpInfo->next;

        }

        printf("Create info return %d, data number %d\n", ret, *tagNum);

        //Store shared memory id

        shmId = ret;

        //Show data

        //printf("Show dataInfo\n");
        

    }

    return 0;

}

int OpenPorts()
{

    int i = 0;

    for(i = 0 ; i < 2 ; i++)
    {

        iTable[i].fd =  sgsSetupModbusRTU(iTable[i].portPath, iTable[i].portParam);
        if(iTable[i].fd < 0)
        {

            perror("sgsSetupModbusRTU");
            return -1;

        }

    }
    return 0;

}

void ReadySwitchCmd()
{
    
    unsigned short crcCheck = 0;

    switch_cmd[0x00] = 0x01;
    switch_cmd[0x01] = 0x03;
    switch_cmd[0x02] = (SW_addr & 0xff00) >> 8;
    switch_cmd[0x03] = SW_addr & 0x00ff ;
    switch_cmd[0x04] = (0x01 & 0xff00) >> 8;
    switch_cmd[0x05] = (0x01 & 0x00ff);
    crcCheck = sgsCaculateCRC(switch_cmd, 6);
    switch_cmd[0x06] = (crcCheck & 0xff00) >> 8;
    switch_cmd[0x07] = crcCheck & 0x00ff;

    return;

}


int CheckSwitch(){
    
    unsigned char response[128];
    unsigned char status[2];
    unsigned short crcCheck = 0;
    int ret;
    int i;

    memset(response,'\0',sizeof(response));    
	memset(status,'\0',sizeof(status));
    
    ret = sgsSendModbusCommandRTU(switch_cmd,8,330000,response);    
    if(ret < 0)
    {

        printf("CheckSwitch Read address %d failed\n",SW_addr); 
        for(i=0; i<8; i++)
            printf(" %02x", switch_cmd[i]);
        printf("\n");
    }
    
    if(0)
    {        
        printf("Switch Data:");
        for(i=0; i<ret; i++)
            printf(" %02x", *(response + i));

        printf("\n");

    }
	
    status[0] = response[3];
    status[1] = response[4];
	
	if(status[1] & 0x01)
		heating = 1 ;
	else
		heating = 0;

	if(status[1] & 0x04)
		compression = 1 ;
	else
		compression = 0;
	
	if(status[1] & 0x08)
		watercharging = 1 ;
	else
		watercharging = 0;
	
	
	
	if(status[1] & 0x80){
	    //printf("warn switch on\n");			
	    return WARM;
	}
	if(status[0] & 0x01){
	    //printf("ice switch on\n");	
	    return COLD;
	}
	if(status[1] & 0x40){
	    //printf("hot switch on\n");	
	    return HOT;
	}
	/*
	switch_cmd[0x00] = 0x01;
    switch_cmd[0x01] = 0x03;
    switch_cmd[0x02] = (411 & 0xff00) >> 8;
    switch_cmd[0x03] = 411 & 0x00ff ;
    switch_cmd[0x04] = (0x01 & 0xff00) >> 8;
    switch_cmd[0x05] = (0x01 & 0x00ff);
    crcCheck = sgsCaculateCRC(switch_cmd, 6);
    switch_cmd[0x06] = (crcCheck & 0xff00) >> 8;
    switch_cmd[0x07] = crcCheck & 0x00ff;*/
   


    return 0;
}

int init_Valve(){
    dValve = NULL;
    dataInfo *dataTemp = dInfo;    
    dataLog dLog ;
    dataLog test;
    int ret;
    

    while(dataTemp != NULL){

        if(!strcmp(dataTemp->valueName, "Valve"))
        {               
            dLog.valueType = STRING_VALUE;
            memset(dLog.value.s,'\0',sizeof(dLog.value.s));          
            snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0;0.0;0.0");   
            ret = sgsWriteSharedMemory(dataTemp, &dLog);
            if(ret!=0)
                printf("[Error] sgsWriteSharedMemory\n");
            else
                printf("[Su] sgsWriteSharedMemory\n");

            sgsReadSharedMemory(dataTemp, &test);
            if(test.valueType == STRING_VALUE)
                printf(GREEN"content is %s\n"NONE, test.value.s);
            dValve = dataTemp;
            return 1;
        }        

        dataTemp = dataTemp->next;
    }

    return 0;

}

int init_Consumption(){
    dHotC = NULL;
    dataInfo *dataTemp = dInfo;    
    dataLog dLog ;

    

    while(dataTemp != NULL){

        if(!strcmp(dataTemp->valueName, "HotConsumption"))
        {               
            dLog.valueType = STRING_VALUE;
            memset(dLog.value.s,'\0',sizeof(dLog.value.s));          
            snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0");   
            sgsWriteSharedMemory(dataTemp, &dLog);   
            dHotC = dataTemp;
       
        }        
        else if(!strcmp(dataTemp->valueName, "WarmConsumption"))
        {               
            dLog.valueType = STRING_VALUE;
            memset(dLog.value.s,'\0',sizeof(dLog.value.s));          
            snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0");   
            sgsWriteSharedMemory(dataTemp, &dLog);   
            dWarmC = dataTemp;
  
        }    
        else if(!strcmp(dataTemp->valueName, "ColdConsumption"))
        {               
            dLog.valueType = STRING_VALUE;
            memset(dLog.value.s,'\0',sizeof(dLog.value.s));          
            snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"0.0");   
            sgsWriteSharedMemory(dataTemp, &dLog);   
            dColdC = dataTemp;
      
        }    
        dataTemp = dataTemp->next;
    }

    return 0;

}

int sgsWriteValve(){

   
    if(dValve == NULL){
        printf("[Failed] dataInfo dValve is NULL \n");
        return 0;
    }
    dataLog dLog ;    
               
    dLog.valueType = STRING_VALUE;
    
    memset(dLog.value.s,'\0',sizeof(dLog.value.s));      
    
    snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f;%2f;%2f",HotValve,WarmValve,ColdValve);
   
    sgsWriteSharedMemory(dValve, &dLog);  
   
    HotValve = 0.0 ;
    WarmValve = 0.0;
    ColdValve = 0.0;

    return 1;
}

int sgsWriteConsumption(){
        
    if(dHotC == NULL){
        printf("[Failed] dataInfo dHotC is NULL \n");
        return 0;
    }


    if(dWarmC == NULL){
        printf("[Failed] dataInfo dWarmC is NULL \n");
        return 0;
    }


    if(dColdC == NULL){
        printf("[Failed] dataInfo dColdC is NULL \n");
        return 0;
    }


    
    dataLog dLog ;                       
    dLog.valueType = STRING_VALUE;        

    memset(dLog.value.s,'\0',sizeof(dLog.value.s));     
    snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f",HotConsumption);       
    sgsWriteSharedMemory(dHotC, &dLog);  
    
    memset(dLog.value.s,'\0',sizeof(dLog.value.s));     
    snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f",ColdConsumption);       
    sgsWriteSharedMemory(dColdC, &dLog);

    memset(dLog.value.s,'\0',sizeof(dLog.value.s));     
    snprintf(dLog.value.s,(sizeof(dLog.value.s) - 1),"%2f",WarmConsumption);       
    sgsWriteSharedMemory(dWarmC, &dLog);

    HotConsumption = 0.0 ;
    ColdConsumption = 0.0;
    WarmConsumption = 0.0;

    return 1;
}

// Reveive command from http_mongo.c
// data format like type1,val1,type2,val2
// define type1 is Coolingg....
int SetCmd(char cmd[]){
   
    int count = 0;
    int c_type = 0;
    int c_val = 0;
    printf("%s\n", cmd);    
    char *buf = strtok(cmd, ",");
   
    printf("%s\n", buf);     
    while (buf != NULL) {        
            
            // if(count == 3){ // val of type 3
            //     buf = "000120002001";
            //     DoModbusCmd_Time(308, buf);
            // }
            // else 
            if(count % 2 != 0){
                c_val = atoi(buf) ;//* 10;
                MakeControlCmd(c_type, c_val);
            }            
            else{
                 c_type = atoi(buf);
            }
            buf = strtok(NULL, ",");
            count++;
       
    }  

    return 0;  

}

int MakeControlCmd(int control_type , int control_val){

    int ret = 0;
    int count = 0;
    //printf("**control_type %d control_val %d\n", control_type, control_val);
    switch(control_type){
        // please refer Protocol about Dispenser
        //Cooling_Temp
        case 1:
            printf(LIGHT_GREEN"Cooling_val %d\n"NONE,control_val);
            while(1){
                ret = DoModbusCmd(121 , control_val*10);
                if (ret > 1){
                    printf(LIGHT_GREEN"Cooling_val %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set Cooling_val %d\n"NONE,control_val);
                    break;
                }

                count++;
                
            }
            
        break;
        //Hot_Temp
        case 2:
            printf(LIGHT_GREEN"Hot_val %d\n"NONE,control_val);          
            while(1){
                ret = DoModbusCmd(118 , control_val*10);
                if (ret > 1){
                    printf(LIGHT_GREEN"Hot_val %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set Hot_val %d\n"NONE,control_val);
                    break;
                }
                
                count++;
            }
        break;
        //save_power
        case 3:
                        
            while(1){
                ret = DoModbusCmd(308 , control_val);
                if (ret > 1){
                    printf(LIGHT_GREEN"savepower %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set savepower %d\n"NONE,control_val);
                    break;
                }
                
                count++;
            }
        break;
        //Monday start time
        case 4:
         
            while(1){
                ret = DoModbusCmd(309 , control_val);
                if (ret > 1){
                    printf(LIGHT_GREEN"Mon_S_T %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set Mon_S_T %d\n"NONE,control_val);
                    break;
                }
                
                count++;
            }
        break;
        //Monday end time
        case 5:           
            while(1){
                ret = DoModbusCmd(316 , control_val);
                if (ret > 1){
                    printf(LIGHT_GREEN"Mon_M_T %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set Mon_E_T %d\n"NONE,control_val);
                    break;
                }
                
                count++;
            }
        break;
        //Tuesday start time
        case 6:
        
           while(1){
               ret = DoModbusCmd(310 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Tue_S_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Tue_S_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
           }
        break;
        //Tuesday end time
        case 7:           
            while(1){
               ret = DoModbusCmd(317 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Tue_M_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Tue_E_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
            }
        break;
        //Wednesday start time
        case 8:
        
           while(1){
               ret = DoModbusCmd(311 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Wed_S_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Wed_S_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
           }
        break;
        //Wednesday end time
        case 9:           
            while(1){
               ret = DoModbusCmd(318 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Wed_M_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Wed_E_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
            }
        break;
        //Thursday start time
        case 10:
        
           while(1){
               ret = DoModbusCmd(312 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Thu_S_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Thu_S_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
           }
        break;
        //Thursday end time
        case 11:           
            while(1){
               ret = DoModbusCmd(319 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Thu_M_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Thu_E_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
            }
        break;
        //Friday start time
        case 12:
        
           while(1){
               ret = DoModbusCmd(313 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Fri_S_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Fri_S_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
           }
        break;
        //Friday end time
        case 13:           
            while(1){
               ret = DoModbusCmd(320 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Fri_M_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Fri_E_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
            }
        break;
        //Saturday start time
        case 14:
        
           while(1){
               ret = DoModbusCmd(314 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Sat_S_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Sat_S_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
           }
        break;
        //Saturday end time
        case 15:           
            while(1){
               ret = DoModbusCmd(321 , control_val);
               if (ret > 1){
                   printf(LIGHT_GREEN"Sat_M_T %d\n"NONE,control_val);
                   break;
               }
               else if(count>10){
                   printf(LIGHT_RED"Failed Set Sat_E_T %d\n"NONE,control_val);
                   break;
               }
               
               count++;
            }
        break;
         //Sunday start time
         case 16:
         
            while(1){
                ret = DoModbusCmd(315 , control_val);
                if (ret > 1){
                    printf(LIGHT_GREEN"Sun_S_T %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set Sun_S_T %d\n"NONE,control_val);
                    break;
                }
                
                count++;
            }
         break;
         //Sunday end time
         case 17:           
             while(1){
                ret = DoModbusCmd(322 , control_val);
                if (ret > 1){
                    printf(LIGHT_GREEN"Sun_M_T %d\n"NONE,control_val);
                    break;
                }
                else if(count>10){
                    printf(LIGHT_RED"Failed Set Sun_E_T %d\n"NONE,control_val);
                    break;
                }
                
                count++;
             }
         break;
        default:
            printf(LIGHT_GREEN"MakeControlCmd default control_type\n"NONE);
            return 0;
        break;

    }

}

int DoModbusCmd(int addr, int val)
{
    
    unsigned short crcCheck = 0;
    unsigned char cmd[128]={'\0'};
    unsigned char response[128]; 
    int i = 0;
    int ret;
    cmd[0x00] = 0x01;
    cmd[0x01] = 0x06;
    cmd[0x02] = (addr & 0xff00) >> 8;
    cmd[0x03] = addr & 0x00ff ;
    cmd[0x04] = (val & 0xff00) >> 8;
    cmd[0x05] = (val & 0x00ff);
    crcCheck = sgsCaculateCRC(cmd, 6);
    cmd[0x06] = (crcCheck & 0xff00) >> 8;
    cmd[0x07] = crcCheck & 0x00ff;

    for(i=0; i<8; i++)
        printf(" %02x", cmd[i]);
    printf("\n");
    
    ret = sgsSendModbusCommandRTU(cmd,8,330000,response);   
    printf("response:");

    for(i=0; i<ret; i++)
        printf(" %02x", *(response + i));

    printf("\n");

    return ret;

}

int DoModbusCmd_Time(int addr, char *val)
{
    
    unsigned short crcCheck = 0;
    unsigned char cmd[128]={'\0'};
    unsigned char response[128]; 
    char t[5];
    int i = 0;
    int ret;
    cmd[0x00] = 0x01;
    cmd[0x01] = 0x06;
    cmd[0x02] = (addr & 0xff00) >> 8;
    cmd[0x03] = addr & 0x00ff ;

    //number of device
    cmd[0x04] = 0;
    cmd[0x05] = 1;
    
    // //number of byte
    // cmd[0x06] = 0x04;  
    
    // substr(t, val, 0, 4);	
    // cmd[0x07] = (atoi(t) & 0xff00) >> 8;
    // cmd[0x08] = (atoi(t) & 0x00ff);


    // substr(t, val, 8, 4);	
    // cmd[0x08] = (atoi(t) & 0xff00) >> 8;
    // cmd[0x09] = (atoi(t) & 0x00ff);   
    // printf("%s\n", t);	

    crcCheck = sgsCaculateCRC(cmd, 6);
    cmd[0x06] = (crcCheck & 0xff00) >> 8;
    cmd[0x07] = crcCheck & 0x00ff;

    for(i=0; i<13; i++)
        printf(" %02x", cmd[i]);
    printf("\n");
    
    ret = sgsSendModbusCommandRTU(cmd,8,330000,response);   
    printf("response: %d\n",ret);

    for(i=0; i<ret; i++)
        printf(" %02x", *(response + i));

    printf("\n");

    return ret;

}

void substr(char *dest, const char* src, unsigned int start, unsigned int cnt) {
	strncpy(dest, src + start, cnt);
	dest[cnt] = 0;
} 
