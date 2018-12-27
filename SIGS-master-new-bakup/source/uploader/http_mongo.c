/*

    Name: Xi-Ping Xu
    Date: July 19,2017
    Last Update: August 18,2017
    Program statement: 
        This is an agent used to test SGS system. 
        It has following functions:
        1. Get a data buffer info from the data buffer pool
        2. Show fake data.
        3. Issue Command to FakeTaida and receive result

*/

/*

    Process:
    
    1. Init

    2. Send the data to server regarding to a certain period of the time passed in by the SolarPost

    3. Leaving

*/



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include <error.h>
#include <errno.h>
#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "../thirdparty/cJSON.h"
#include "../thirdparty/sqlite3.h" 
#include "../ipcs/SGSipcs.h"
#include "../events/SGSEvent.h"
#include "../controlling/SGScontrol.h"

//execlp("./SolarPut","./SolarPut", Resend_time_s, Resend_time_e, address,NULL);

static int callback(void *data, int argc, char **argv, char **azColName);


//Post definitions for max length

#define SA      struct sockaddr
#define MAXLINE 16384
#define MAXSUB  16384

//Intent    : Get config from conf file. If failed, use the default one
//Pre       : Nothing
//Post      : Always return 0

int GetConfig();

//Intent    : Post data to server
//Pre       : Nothing
//Post      : On success, return 0, otherwise return -1

int PostToServer();

ssize_t process_http( char *content, char *address);

int my_write(int fd, void *buffer, int length);

int my_read(int fd, void *buffer, int length);

//Intent    : Process queue message
//Pre       : Nothing
//Post      : Always return 0

int CheckAndRespondQueueMessage();

int ShutdownSystemBySignal(int sigNum);

int callconfig();

cJSON *jsonRoot = NULL;

dataInfo *dInfo[2] = {NULL,NULL};    // pointer to the shared memory
int interval = 30;  // time period between last and next collecting section
int eventHandlerId; // Message queue id for event-handler
int shmId;          // shared memory id
int msgId;          // created by sgsCreateMessageQueue
int msgType;        // 0 1 2 3 4 5, one of them

int serverFlag = 0;

typedef struct postNode
{

    char GW_ver[16];        //string
    char IP[4][128];        //string
    epochTime Date_Time;    //time_t
    int Send_Rate;          //float to int
    int Gain_Rate;          //float to int
    int Backup_time;        //int
    char MAC_Address[32];   //string
    char Station_ID[16];    //string
    char GW_ID[16];         //string

}pNode;

pNode postConfig;

char serverIp[64] = {0};

char start_time[32] = {'\0'};

char end_time[32] = {'\0'};

char *deviceID = "EE_06_01";

int serverCount = 0;

int main(int argc, char* argv[])
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc, interval = 30;
    char sql[256], *unformatted = NULL;
    
    time_t last, now;
    struct tm abc;
    time(&last);
    now = last;


    eventHandlerId = sgsCreateMsgQueue(EVENT_HANDLER_KEY, 0);
    if(eventHandlerId == -1)
    {
        printf("Open eventHandler queue failed...\n");
        exit(0);
    }
    // if(argc < 3)
    // {

    //     printf(LIGHT_RED"SolarPut [Start Date] [End Date] [target server ip]\n"NONE);
    //     exit(0);

    // }

    //snprintf(serverIp, sizeof(serverIp) - 1, "%s", "140.118.123.98:11045/status");
    snprintf(serverIp, sizeof(serverIp) - 1, "%s", "140.118.123.95:10000/status");	
  

    /* Open database */
    rc = sqlite3_open("./log/SGSdb.db", &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return(0);
    }else{
        fprintf(stderr, "Opened database successfully\n");
    }

    int ret=0;
    while(1){

        
        time(&now);
       
      

        if( ((now-last) >= (interval +2) )){

            
            if(ret == -1){
                printf("[Failed];hezhong;http_mongo; ret = %d\n",ret);
            }
                
            //printf("%ld\n",now);            
            localtime_r(&now,&abc);
            printf("utc+8 : %lds\n",abc.tm_gmtoff + now);
            //printf("%lds\n",abc.tm_gmtoff);
            //printf("%s\n",abc.tm_zone);

            //printf(LIGHT_GREEN"interval now %d\n"NONE,now-last);
            
            //if(start_time[0] == '\0')

            // get the lastest time and command from server
            callconfig();
    
        
            /* Create SQL statement */    
            if(start_time[0] != '\0'){
    
                //1503931674
                memset(sql, '\0', sizeof(sql));
                snprintf(sql, 255,  "SELECT * from hezhong where Timestamp BETWEEN %s AND %ld; ", start_time , now + abc.tm_gmtoff);
                printf("command : %s \n",sql);
    
                /* Get Config from setting file */
    
              
                
                /* Execute SQL statement */
                rc = sqlite3_exec(db, sql, callback, ((void*)(NULL)), &zErrMsg);
                if( rc != SQLITE_OK ){
                    fprintf(stderr, "SQL error: %s\n", zErrMsg);
                    sqlite3_free(zErrMsg);
                }else{
                    fprintf(stdout, "Operation done successfully\n");
                }
         
            }      
            
            time(&last);
            now = last;
            last -= 2;

        }
               
    }    
   
       sqlite3_close(db);
    return 0;
}



static int callback(void *data, int argc, char **argv, char **azColName)
{

    int i, tempID = 1, firstTempArray = 1, inverterID = 1 ,i_data,ret;
    float val;
    char *unformatted = NULL;
    char buf[32] = {0};
    epochTime nowTime;
    cJSON *inverter = NULL;
    cJSON *tempArray = NULL;
    cJSON *outerObj = NULL;


    char buff[64] = {'\0'};


    time(&nowTime);

    fprintf(stderr, "Callback start: \n");

    //Fill first json object here

    outerObj = cJSON_CreateObject();

    //cJSON_AddItemToObject(outerObj, "rows", jsonRoot = cJSON_CreateArray());

    // We check the JSON data at here

    
        //printf("\t\t\tupdated time : %s\n",buf);
    cJSON_AddStringToObject(outerObj, "Device_ID", deviceID);
    cJSON_AddStringToObject(outerObj, "Key", "ntustdispenser");
    //cJSON_AddStringToObject(outerObj, "UploadTime", buff);
    

    //fill up the rest of the data with callback datas

    for(i=0; i<argc; i++)
    {

        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");

        

        if(strstr(azColName[i], "sensorName"))
        {
            printf("Skipping sensorName \n");
        }
        else if(strstr(azColName[i], "Timestamp"))
        {
            cJSON_AddStringToObject(outerObj, "UploadTime", argv[i]);
            strncpy(buff, argv[i], sizeof(buff));

            if(!strcmp(start_time, buff)){
                printf("Skipping same Time \n");
                return 0;
                
            }
            else{
                printf("Time is different\n");
                printf("%s\n",start_time);
                strncpy(start_time, buff, sizeof(start_time));
                printf("%s\n",start_time);
            }


        }
        else if(strstr(azColName[i], "HotTemp"))
        {
            i_data = atoi(argv[i]);
            printf("HotTemp i_data %d\n",i_data);
            val = i_data;
            val = val / 10.0;
            printf("HotTemp val %lf\n",val);
            cJSON_AddNumberToObject(outerObj, "HotTemp", val);

        }
        else if(strstr(azColName[i], "ColdTemp"))
        {

            i_data = atoi(argv[i]);
            val = i_data;
            val = val / 10;
            cJSON_AddNumberToObject(outerObj, "ColdTemp", val);

        }
        else if(strstr(azColName[i], "WarmTemp"))
        {

            i_data = atoi(argv[i]);
            val = i_data;
            val = val / 10;
            cJSON_AddNumberToObject(outerObj, "WarmTemp", val);

        }
        else if(strstr(azColName[i], "TDS"))
        {

            i_data = atoi(argv[i]);
            val = i_data;      
            cJSON_AddNumberToObject(outerObj, "TDS", val);

        }
        // else if(strstr(azColName[i], "Lock"))
        // {

        //     cJSON_AddStringToObject(outerObj, "Lock", argv[i]);

        // }
        else if(strstr(azColName[i], "SavingPower"))
        {

            cJSON_AddStringToObject(outerObj, "SavingPower", argv[i]);

        }
        // else if(strstr(azColName[i], "IceSystem"))
        // {

        //     cJSON_AddStringToObject(outerObj, "IceSystem", argv[i]);

        // }
        // else if(strstr(azColName[i], "Sterilizing"))
        // {

        //     cJSON_AddStringToObject(outerObj, "Sterilizing", argv[i]);

        // }
        else if(strstr(azColName[i], "HighWaterLevel"))
        {
            i_data = atoi(argv[i]);						
            cJSON_AddNumberToObject(outerObj, "HighWaterLevel", i_data);

        }
        else if(strstr(azColName[i], "LowWaterLevel"))
        {

            i_data = atoi(argv[i]);						
            cJSON_AddNumberToObject(outerObj, "LowWaterLevel", i_data);

        }
        else if(strstr(azColName[i], "MeanWaterLevel"))
        {

            i_data = atoi(argv[i]);						
            cJSON_AddNumberToObject(outerObj, "MeanWaterLevel", i_data);

        }
        else if(strstr(azColName[i], "Heating"))
        {

            i_data = atoi(argv[i]);						
            cJSON_AddNumberToObject(outerObj, "Heating", i_data);

        }
        else if(strstr(azColName[i], "Compression"))
        {

            i_data = atoi(argv[i]);						
            cJSON_AddNumberToObject(outerObj, "Compression", i_data);

        }
        else if(strstr(azColName[i], "WaterCharge"))
        {

            i_data = atoi(argv[i]);						
            cJSON_AddNumberToObject(outerObj, "WaterCharge", i_data);

        }
        else if(strstr(azColName[i], "HotConsumption"))
        {

            val = atof(argv[i]);											
            cJSON_AddNumberToObject(outerObj, "HotConsumption", val);		        

        }
        else if(strstr(azColName[i], "WarmConsumption"))
        {

            val = atof(argv[i]);											
            cJSON_AddNumberToObject(outerObj, "WarmConsumption", val);

        }
        else if(strstr(azColName[i], "ColdConsumption"))
        {

            val = atof(argv[i]);											
            cJSON_AddNumberToObject(outerObj, "ColdConsumption", val);

        }
        else if(strstr(azColName[i], "Alert"))
        {

            cJSON_AddStringToObject(outerObj, "Alert", argv[i]);

        }
        else if(strstr(azColName[i], "Valve"))
        {

            cJSON_AddStringToObject(outerObj, "Valve", argv[i]);

        }
	else if(strstr(azColName[i], "Filter_CC"))
        {
	    val = atof(argv[i]);		
            cJSON_AddNumberToObject(outerObj, "Filter_CC", val);

        }
	else if(strstr(azColName[i], "Filter_Day"))
        {

	     val = atof(argv[i]);
            cJSON_AddNumberToObject(outerObj, "Filter_Day", val);

        } 
	else if(strstr(azColName[i], "Filter_Month"))
        {
		val = atof(argv[i]);
            cJSON_AddNumberToObject(outerObj, "Filter_Month", val);

        } 
	else if(strstr(azColName[i], "Usage_CC"))
        {
		val = atof(argv[i]);
            cJSON_AddNumberToObject(outerObj, "Usage_CC", val);

        } 
	else if(strstr(azColName[i], "Usage_L"))
        {
		val = atof(argv[i]);
            cJSON_AddNumberToObject(outerObj, "Usage_L", val);

        }
	else if(strstr(azColName[i], "Usage_Record"))
        {
		val = atof(argv[i]);
            cJSON_AddNumberToObject(outerObj, "Usage_Record", val);

        }
	else if(strstr(azColName[i], "ErrorCode"))
        {
		val = atof(argv[i]);
            cJSON_AddNumberToObject(outerObj, "ErrorCode", val);

        } 
        
    }

    //jsonRoot = outerObj;

    printf(LIGHT_RED"Callback function ends. We should call process_http here.\n"NONE);

    //unformatted = cJSON_Print(jsonRoot);
    unformatted = cJSON_PrintUnformatted(outerObj);

    //printf("json:\n %s \n", unformatted);
    ret = process_http(unformatted, serverIp);
    
  
    cJSON_Delete(outerObj);

    return 0;

}

ssize_t process_http( char *content, char *address)
{
    
    int sockfd;
	struct sockaddr_in servaddr;
	char **pptr;
	char str[50];
	struct hostent *hptr;
    char sendline[MAXLINE + 1], recvline[MAXLINE + 1];
    char buff_cmd[128] = {'\0'};    
    char buff_cmd2[64] = {'\0'};    
    int i = 0, ret = 0;
    char *error = NULL;
    char *hname = NULL;             //IP
    char *serverPort = NULL;        //port
    char page[128] = {'\0'};        //rest api
    char adrBuf[128] = {'\0'};
    char *tmp = NULL;
	ssize_t n;
    cJSON *root = NULL;
    cJSON *obj = NULL;
    cJSON *cmd = NULL;

    //process address (host name, page, port) example, 203.73.24.133:8000/solar_rawdata

    strncpy(adrBuf, address, sizeof(adrBuf));

    hname = strtok(adrBuf, ":");
    serverPort = strtok(NULL, "/");
    tmp = strtok(NULL, "/");

    //Get what's behind the / 

    
    memset(page, 0, sizeof(page));
    snprintf(page, sizeof(page), "/%s",tmp);

    //Intialize host entity with server ip address

    if ((hptr = gethostbyname(hname)) == NULL) 
    {

		printf("[%s:%d] gethostbyname error for host: %s: %s", __FUNCTION__, __LINE__,hname ,hstrerror(h_errno));

		return -1;

	}

	printf("[%s:%d] hostname: %s\n",__FUNCTION__,__LINE__, hptr->h_name);

    //Set up address type (FAMILY)

	if (hptr->h_addrtype == AF_INET && (pptr = hptr->h_addr_list) != NULL) 
    {

		printf("[%s:%d] address: %s\n",__FUNCTION__,__LINE__,inet_ntop( hptr->h_addrtype , *pptr , str , sizeof(str) ));

	} 
    else
    {

		printf("[%s:%d] Error call inet_ntop \n",__FUNCTION__,__LINE__);

        return -1;

	}

    //Create socket

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if(sockfd == -1)
    {

        serverCount++;
        
        if(serverCount > 30 ) {
            
            system("reboot");
            
        }

        printf("socket failed, errno %d, %s\n",errno,strerror(errno));
        return -1;

    }
    else
    {

        printf("[%s,%d]socket() done\n",__FUNCTION__,__LINE__);

    }

    //Set to 0  (Initialization)

	bzero(&servaddr, sizeof(servaddr));

    //Fill in the parameters

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(atoi(serverPort));
	inet_pton(AF_INET, str, &servaddr.sin_addr);

    //Connect to the target server

	ret = connect(sockfd, (SA *) & servaddr, sizeof(servaddr));

    if(ret == -1)
    {
        serverCount++;
        
        if(serverCount > 30 ) {
            
            system("reboot");
            
        }


        printf("connect() failed, errno %d, %s\n",errno,strerror(errno));
        close(sockfd);
        return -1;

    }
    else
    {

        printf("[%s,%d]connect() done\n",__FUNCTION__,__LINE__);

    }

    //Header content for HTTP POST

	snprintf(sendline, MAXSUB,
		 "POST %s HTTP/1.1\r\n"
		 "Host: %s\r\n"
		 "Content-type: application/json; charset=UTF-8\r\n"
         "User-Agent: Kelier/0.1\r\n"
		 "Content-Length: %lu\r\n\r\n"
		 "%s", page, hname, strlen(content), content);

    //print out the content

    printf("sendline : \n %s\n",sendline);

    //Send the packet to the server

    ret = my_write(sockfd, sendline, strlen(sendline));

    if(ret == -1)
    {
        serverCount++;
        
        if(serverCount > 30 ) {
            
            system("reboot");
            
        }

        printf("write() failed, errno %d, %s\n",errno,strerror(errno));
        close(sockfd);
        return -1;

    }
    printf("[%s,%d]Write() done\n",__FUNCTION__,__LINE__);

    //Get the result

    memset(recvline, 0, sizeof(recvline));
    memset(buff_cmd,'\0',sizeof(buff_cmd));    
    

    ret = my_read(sockfd, recvline, (sizeof(recvline) - 1));

    if(ret == -1)
    {
        serverFlag = 1;
        printf("read() failed, errno %d, %s\n",errno,strerror(errno));
        close(sockfd);
        return -1;

    }
    printf("[%s,%d]Read done\n",__FUNCTION__,__LINE__);

    printf("%s\n",recvline);
    //Check the result with cJSON

    tmp = strstr(recvline, "{");

    root = cJSON_Parse(tmp);

    obj = cJSON_GetObjectItem(root, "flag");

    if( obj != NULL)
    {

        if(obj->type == 1) //upload unsuccessfully
        {
            printf("flag False\n");
           
            cJSON_Delete(root);
            close(sockfd);
            return -2; //tell PostToServer() to resend the data
        }
        else
        {
            
            obj = cJSON_GetObjectItem(root, "config");
            if( obj != NULL)
            {

                printf("%d\n",obj->type);
                printf("%s\n",obj->valuestring);
                strncpy(start_time, obj->valuestring, sizeof(start_time));
                printf("start_time:%s\n",start_time);     

            }
            
            cmd = cJSON_GetObjectItem(root, "command");
            if( cmd != NULL)
            {
                
                printf("command %d\n",cmd->type);
                snprintf(buff_cmd, sizeof(buff_cmd) - 1, "[Control];hezhong;http_mongo;");
                obj = cJSON_GetObjectItem(cmd, "ST_Cooling");
                if( obj != NULL)
                {              
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));       
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "1,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);                
                   
                }

                obj = cJSON_GetObjectItem(cmd, "ST_Hot");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "2,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "savepower");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "3,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Mon_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "4,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Mon_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "5,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Tue_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "6,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Tue_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "7,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Wed_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "8,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Wed_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "9,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Thu_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "10,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Thu_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "11,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Fri_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "12,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Fri_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "13,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Sat_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "14,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Sat_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "15,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Sun_S_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "16,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                obj = cJSON_GetObjectItem(cmd, "Sun_E_T");
                if( obj != NULL)
                {                     
                    memset(buff_cmd2, '\0', sizeof(buff_cmd2));
                    snprintf(buff_cmd2, sizeof(buff_cmd2) - 1, "17,%s,", obj->valuestring);  
                    strcat(buff_cmd, buff_cmd2);
                     
                }

                sgsSendQueueMsg(eventHandlerId, buff_cmd, EnumCollector); 
            }

            cJSON_Delete(root);         
            close(sockfd);       
            return 0;

        }

    }
    else
    {
        printf("[Error] Server format\n");
       
    }

    //close the socket
    
    close(sockfd);
    cJSON_Delete(root);
	return n;

}

int my_write(int fd, void *buffer, int length)
{

    int bytes_left;
    int written_bytes;
    char *ptr;

    ptr=buffer;
    bytes_left=length;

    while(bytes_left>0)
    {
            
            //printf("Write loop\n");
            written_bytes=write(fd,ptr,bytes_left);
            //printf("Write %d bytes\n",written_bytes);

            if(written_bytes<=0)
            {       

                if(errno==EINTR)

                    written_bytes=0;

                else             

                    return(-1);

            }

            bytes_left-=written_bytes;
            ptr+=written_bytes;   

    }

    return(0);

}

int my_read(int fd, void *buffer, int length)
{

    int bytes_left;
    int bytes_read;
    char *ptr;
    
    ptr=buffer;
    bytes_left=length;

    while(bytes_left>0)
    {

        bytes_read=read(fd,ptr,bytes_read);

        if(bytes_read<0)
        {

            if(errno==EINTR)

                bytes_read=0;

            else

                return(-1);

        }

        else if(bytes_read==0)
            break;

        bytes_left-=bytes_read;
        ptr+=bytes_read;

    }

    return(length-bytes_left);

}


int callconfig()
{
   
    int ret;
    char *unformatted = NULL;
    
    
    
    cJSON *outerObj = NULL;
    

    fprintf(stderr, "Callback start: \n");

    //Fill first json object here

    outerObj = cJSON_CreateObject();

    //cJSON_AddItemToObject(outerObj, "rows", jsondRoot = cJSON_CreateArray());

    // We check the JSON data at here

    //printf("\t\t\tupdated time : %s\n",buf);
    cJSON_AddStringToObject(outerObj, "Device_ID", deviceID);    
    cJSON_AddStringToObject(outerObj, "Key", "config_time");  
    

   

    printf(LIGHT_RED"Callback function ends. We should call process_http here.\n"NONE);

    //unformatted = cJSON_Print(jsonRoot);
    unformatted = cJSON_PrintUnformatted(outerObj);

    printf("json:\n %s \n", unformatted);
    ret = process_http(unformatted, serverIp);
    
    free(unformatted);
    
    cJSON_Delete(outerObj);
    return 0;

}
