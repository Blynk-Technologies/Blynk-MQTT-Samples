#include "libGSM.h"
#include <stdio.h>
#include "cmsis_os.h"
#include "netif/ppp/pppos.h"
#include "netif/ppp/ppp.h"
#include "lwip/dns.h"
#include "main.h"
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "client_blynk.h"

#define GSM_DEBUG 1
#define GSM_OK_Str "OK"
#define CONFIG_GSM_APN "internet"
#define PPPOSMUTEX_TIMEOUT 1000
#define PPPOS_CLIENT_STACK_SIZE 1024
#define BUF_SIZE (256)
#define PPP_User ""
#define PPP_Pass ""

typedef struct
{
   char* cmd;
   uint16_t cmdSize;
   char* cmdResponseOnOk;
   uint16_t timeoutMs;
   uint16_t delayMs;
   uint8_t skip;
} GSM_Cmd;

static GSM_Cmd cmd_AT = {
   .cmd = "AT\r\n",
   .cmdSize = sizeof("AT\r\n") - 1,
   .cmdResponseOnOk = GSM_OK_Str,
   .timeoutMs = 300,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_NoSMSInd __attribute__((unused)) =
{
   .cmd = "AT+CNMI=0,0,0,0,0\r\n",
   .cmdSize = sizeof("AT+CNMI=0,0,0,0,0\r\n") - 1,
   .cmdResponseOnOk = GSM_OK_Str,
   .timeoutMs = 1000,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_Reset =
{
   .cmd = "ATZ\r\n",
   .cmdSize = sizeof("ATZ\r\n") - 1,
   .cmdResponseOnOk = GSM_OK_Str,
   .timeoutMs = 300,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_RFOn =
{
   .cmd = "AT+CFUN=1\r\n",
   .cmdSize = sizeof("ATCFUN=1,0\r\n") - 1,
   .cmdResponseOnOk = GSM_OK_Str,
   .timeoutMs = 10000,
   .delayMs = 1000,
   .skip = 0,
};

static GSM_Cmd cmd_EchoOff =
{
   .cmd = "ATE0\r\n",
   .cmdSize = sizeof("ATE0\r\n") - 1,
   .cmdResponseOnOk = GSM_OK_Str,
   .timeoutMs = 300,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_CGATT __attribute__((unused)) =
{
   .cmd = "AT+CGATT=0\r\n",
   .cmdSize = sizeof("AT+CGATT=0\r\n") - 1,
   .cmdResponseOnOk = GSM_OK_Str,
   .timeoutMs = 6000,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_Pin =
{
   .cmd = "AT+CPIN?\r\n",
   .cmdSize = sizeof("AT+CPIN?\r\n") - 1,
   .cmdResponseOnOk = "CPIN: READY",
   .timeoutMs = 5000,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_Cops =
{
   .cmd = "AT+COPS=0\r\n",
   .cmdSize = sizeof("AT+COPS=0\r\n") - 1,
   .cmdResponseOnOk = "OK",
   .timeoutMs = 5000,
   .delayMs = 0,
   .skip = 0,
};

static GSM_Cmd cmd_Reg =
{
   .cmd = "AT+CREG?\r\n",
   .cmdSize = sizeof("AT+CREG?\r\n") - 1,
   .cmdResponseOnOk = "CREG: 0,1",  //"CREG: 0,5" for roaming
   .timeoutMs = 5000,
   .delayMs = 2000,
   .skip = 0,
};

static GSM_Cmd cmd_Signal =
{
   .cmd = "AT+CSQ\r\n",
   .cmdSize = sizeof("AT+CSQ\r\n") - 1,
   .cmdResponseOnOk = "CSQ:",
   .timeoutMs = 3000,
   .delayMs = 1000, //wait better signal
   .skip = 0,
};

static GSM_Cmd cmd_APN =
{
    .cmd = NULL,
    .cmdSize = 0,
    .cmdResponseOnOk = GSM_OK_Str,
    .timeoutMs = 8000,
    .delayMs = 0,
    .skip = 0,
};

static GSM_Cmd cmd_Connect =
{
   .cmd = "ATD*99#\r\n",
   .cmdSize = sizeof("ATD*99#\r\n") - 1,
   .cmdResponseOnOk = "CONNECT",
   .timeoutMs = 30000,
   .delayMs = 1000,
   .skip = 0,
};

static GSM_Cmd* GSM_Init[] =
{
   &cmd_AT,
   &cmd_Reset,
   &cmd_EchoOff,
   &cmd_RFOn,
   // &cmd_NoSMSInd,
   &cmd_Pin,
   &cmd_Cops,
   &cmd_Reg,
   &cmd_Signal,
   // &cmd_CGATT,
   &cmd_APN,
   &cmd_Connect,
};

#define GSM_InitCmdsSize (sizeof(GSM_Init) / sizeof(GSM_Cmd *))

extern UART_HandleTypeDef huart6;
// static void MX_USART6_UART_Init(void);
osThreadId pppThreadHandle;
static osSemaphoreId pppos_mutex = NULL;
static int do_pppos_connect = 1;
static uint8_t pppos_task_started = 0;
static uint8_t gsm_status = GSM_STATE_FIRSTINIT;

static ppp_pcb* ppp = NULL;
struct netif ppp_netif;
// extern struct netif gnetif;

static uint32_t pppos_rx_count;
static uint32_t pppos_tx_count;
static uint8_t gsm_rfOff = 0;

static uint8_t uart_rx_buff[BUF_SIZE];
volatile static uint16_t UART_LastPos = 0;  // Keeps track of last read position

static uint16_t UART_DMA_GetBytesAvailable() {
   return UART_LastPos != (BUF_SIZE - __HAL_DMA_GET_COUNTER(huart6.hdmarx));
}

static uint16_t UART_DMA_Read(uint8_t *dest, uint16_t maxLen) {
    uint16_t newPos = BUF_SIZE - __HAL_DMA_GET_COUNTER(huart6.hdmarx); // Current DMA position
    uint16_t dataCount = 0;

    if (newPos != UART_LastPos) {  // Check if new data is available
        if (newPos > UART_LastPos) {  
            dataCount = newPos - UART_LastPos;  // Data is contiguous
        } else {
            // Wrap-around case
            dataCount = (BUF_SIZE - UART_LastPos) + newPos;
        }

        // Ensure we don't copy more than `maxLen`
        if (dataCount > maxLen) {
            dataCount = maxLen;
        }

        // Copy data from circular buffer to destination buffer
        if (newPos > UART_LastPos) {
            memcpy(dest, &uart_rx_buff[UART_LastPos], dataCount);
        } else {
            uint16_t firstChunk = BUF_SIZE - UART_LastPos;
            if (firstChunk > dataCount) firstChunk = dataCount;
            memcpy(dest, &uart_rx_buff[UART_LastPos], firstChunk);
            
            if (dataCount > firstChunk) {
                memcpy(dest + firstChunk, uart_rx_buff, dataCount - firstChunk);
            }
        }

        // Update the last read position
        UART_LastPos = (UART_LastPos + dataCount) % BUF_SIZE;
    }

    return dataCount;  // Return number of bytes copied
}

int UART_Read(uint8_t* dataToRead, int size, int timeout) 
{
   int count = 0;
   while( timeout-- && count < size ){
      if( 0 < UART_DMA_GetBytesAvailable() ){
         uint8_t someByte;
         UART_DMA_Read(&someByte, 1);
         dataToRead[count] = someByte;
         count++;
      }
      osDelay(1);
   }
   return count;
}

static void enableAllInitCmd()
{
   for (int idx = 0; idx < GSM_InitCmdsSize; idx++) {
      GSM_Init[idx]->skip = 0;
   }
}

static void infoCommand(char* cmd, int cmdSize, char* info)
{
   char buf[cmdSize + 2];
   memset(buf, 0, cmdSize + 2);

   for (int i = 0; i < cmdSize; i++)
   {
      if ((cmd[i] != 0x00) && ((cmd[i] < 0x20) || (cmd[i] > 0x7F)))
         buf[i] = '.';
      else
         buf[i] = cmd[i];
      if (buf[i] == '\0')
         break;
   }
   printf("GSM: %s [%s]\r\n", info, buf);
}

static int atCmd_waitResponse(char* cmd, char* resp, char* resp1, int cmdSize, int timeout, char** response, int size)
{
   char sresp[256] = { '\0' };
   char data[256] = { '\0' };
   int len, res = 1, idx = 0, tot = 0, timeoutCnt = 0;

   // ** Send command to GSM
   osDelay(100);
   if (cmd != NULL)
   {
      if (cmdSize == -1)
         cmdSize = strlen(cmd);
#if GSM_DEBUG
      infoCommand(cmd, cmdSize, "AT COMMAND:");
#endif
      HAL_UART_Transmit(&huart6, (uint8_t*)cmd, cmdSize, 100);
   }

   if (response != NULL)
   {
      // Read GSM response into buffer
      char* pbuf = *response;
      len = UART_Read((uint8_t*)data, 256, timeout);
      while (len > 0)
      {
         if ((tot + len) >= size)
         {
            char* ptemp = realloc(pbuf, size + 512);
            if (ptemp == NULL)
               return 0;
            size += 512;
            pbuf = ptemp;
         }
         memcpy(pbuf + tot, data, len);
         tot += len;
         response[tot] = '\0';
         len = UART_Read((uint8_t*)data, 256, timeout);
      }
      *response = pbuf;
      return tot;
   }

   // ** Wait for and check the response
   idx = 0;
   while (1)
   {
      memset(data, 0, 256);
      len = 0;
      len = UART_Read((uint8_t*)data, 256, 10);
      if (len > 0)
      {
         for (int i = 0; i < len; i++)
         {
            if (idx < 256)
            {
               if ((data[i] >= 0x20) && (data[i] < 0x80))
                  sresp[idx++] = data[i];
               else
                  sresp[idx++] = 0x2e;
            }
         }
         tot += len;
      }
      else
      {
         if (tot > 0)
         {
            // Check the response
            if (strstr(sresp, resp) != NULL)
            {
#if GSM_DEBUG
               printf("GSM: AT RESPONSE: [%s]\r\n", sresp);
#endif
               break;
            }
            else
            {
               if (resp1 != NULL)
               {
                  if (strstr(sresp, resp1) != NULL)
                  {
#if GSM_DEBUG
                     printf("GSM: AT RESPONSE (1): [%s]\r\n", sresp);
#endif
                     res = 2;
                     break;
                  }
               }
               // no match
#if GSM_DEBUG
               printf("GSM: AT BAD RESPONSE: [%s]\r\n", sresp);
#endif
               res = 0;
               break;
            }
         }
      }

      timeoutCnt += 10;
      if (timeoutCnt > timeout)
      {
         // timeout
#if GSM_DEBUG
         printf("GSM: AT: TIMEOUT\r\n");
#endif
         res = 0;
         break;
      }
   }

   return res;
}

int LTE_ppposInit()
{
   printf("GSM: LTE_ppposInit\r\n");

   // MX_USART6_UART_Init();
   HAL_UART_Receive_DMA(&huart6, uart_rx_buff, BUF_SIZE);

   if (pppos_mutex != NULL) osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
   do_pppos_connect = 1;
   int gstat = 0;
   int task_s = pppos_task_started;
   if (pppos_mutex != NULL) osSemaphoreRelease(pppos_mutex);

   if (task_s == 0)
   {
      if (pppos_mutex == NULL)
      {
         osSemaphoreDef(pposSem);
         pppos_mutex = osSemaphoreCreate(osSemaphore(pposSem), 1);
      }
      if (pppos_mutex == NULL)
         return 0;


      //   tcpip_init( NULL, NULL );
      //   LWIP must be initialized before this.

      osThreadDef(mythread, PPPosClientThread, osPriorityNormal, 0, 512+256);
      pppThreadHandle = osThreadCreate(osThread(mythread), NULL);

      while (task_s == 0)
      {
         osDelay(10);
         osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
         task_s = pppos_task_started;
         osSemaphoreRelease(pppos_mutex);
      }
   }

   while (gstat != 1)
   {
      osDelay(10);
      osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
      gstat = gsm_status;
      task_s = pppos_task_started;
      osSemaphoreRelease(pppos_mutex);
      if (task_s == 0)
         return 0;
   }

   printf("GSM: LTE Init done\r\n");
   return 1;
}

static void ppp_status_cb(ppp_pcb* pcb, int err_code, void* ctx)
{
   struct netif* pppif = ppp_netif(pcb);
   LWIP_UNUSED_ARG(ctx);

   printf("GSM: ppp_status_cb: %d\r\n", err_code);

   switch (err_code)
   {
   case PPPERR_NONE:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Connected\r\n");
#if PPP_IPV4_SUPPORT
      printf("GSM:    ipaddr    = %s\r\n", ipaddr_ntoa(&pppif->ip_addr));
      printf("GSM:    gateway   = %s\r\n", ipaddr_ntoa(&pppif->gw));
      printf("GSM:    netmask   = %s\r\n", ipaddr_ntoa(&pppif->netmask));
#endif

#if PPP_IPV6_SUPPORT
      printf("GSM: ip6addr   = %s", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
#endif
#if LWIP_DNS
      const ip_addr_t* dns_server;
      dns_server = dns_getserver(0);
      if (NULL != dns_server && IP4_ADDR_ANY != dns_server) {
         printf("GSM:    DNS1      = %s\n", ipaddr_ntoa(dns_server));
      }
      dns_server = dns_getserver(1);
      if (NULL != dns_server && IP4_ADDR_ANY != dns_server) {
         printf("GSM:    DNS2      = %s\n", ipaddr_ntoa(dns_server));
      }
#endif
#endif
      osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
      gsm_status = GSM_STATE_CONNECTED;
      osSemaphoreRelease(pppos_mutex);

      ppp_set_default(ppp);
      // ppp_set_usepeerdns(ppp, 1);
      mqtt_send_connect();

      break;
   }
   case PPPERR_PARAM:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Invalid parameter\r\n");
#endif
      break;
   }
   case PPPERR_OPEN:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Unable to open PPP session\r\n");
#endif
      break;
   }
   case PPPERR_DEVICE:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Invalid I/O device for PPP\r\n");
#endif
      break;
   }
   case PPPERR_ALLOC:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Unable to allocate resources\r\n");
#endif
      break;
   }
   case PPPERR_USER:
   {
      /* ppp_free(); -- can be called here */
#if GSM_DEBUG
      printf("GSM: status_cb: User interrupt (disconnected)\r\n");
#endif
      osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
      gsm_status = GSM_STATE_DISCONNECTED;
      osSemaphoreRelease(pppos_mutex);
      break;
   }
   case PPPERR_CONNECT:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Connection lost\r\n");
#endif
      osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
      gsm_status = GSM_STATE_DISCONNECTED;
      osSemaphoreRelease(pppos_mutex);
      break;
   }
   case PPPERR_AUTHFAIL:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Failed authentication challenge\r\n");
#endif
      break;
   }
   case PPPERR_PROTOCOL:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Failed to meet protocol\r\n");
#endif
      break;
   }
   case PPPERR_PEERDEAD:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Connection timeout\r\n");
#endif
      break;
   }
   case PPPERR_IDLETIMEOUT:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Idle Timeout\r\n");
#endif
      break;
   }
   case PPPERR_CONNECTTIME:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Max connect time reached\r\n");
#endif
      break;
   }
   case PPPERR_LOOPBACK:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Loopback detected\r\n");
#endif
      break;
   }
   default:
   {
#if GSM_DEBUG
      printf("GSM: status_cb: Unknown error code %d\r\n", err_code);
#endif
      break;
   }
   }
}

static uint32_t ppp_output_callback(ppp_pcb* pcb, uint8_t* data, uint32_t len, void* ctx)
{
   uint32_t ret = 0;
   if(osOK == osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT)) {
      HAL_StatusTypeDef res = HAL_UART_Transmit(&huart6, data, len, 10000);
      if(HAL_OK != res) {
         printf("%s %s data len:%lu err:%d!\n", pcTaskGetName(NULL), __PRETTY_FUNCTION__, len, res);
      }
      else {
         // printf("%s %ld ->\n", pcTaskGetName(NULL), len);
         ret = len;
      }
      osSemaphoreRelease(pppos_mutex);
   }else{
      printf("%s %s can't acquire lock in %d ms!\n", pcTaskGetName(NULL), __PRETTY_FUNCTION__, PPPOSMUTEX_TIMEOUT);
   }
   return ret;
}

//------------------------------------
static void _disconnect(uint8_t rfOff)
{
   int res = atCmd_waitResponse("AT\r\n", GSM_OK_Str, NULL, 4, 1000, NULL, 0);
   if (res == 1)
   {
      if (rfOff)
      {
         cmd_Reg.timeoutMs = 10000;
         res = atCmd_waitResponse("AT+CFUN=4\r\n", GSM_OK_Str, NULL, 11, 10000, NULL, 0); // disable RF function
      }
      return;
   }

#if GSM_DEBUG
   printf("GSM: ONLINE, DISCONNECTING...\r\n");
#endif
   osDelay(1000);
   HAL_UART_Transmit(&huart6, (uint8_t*)"+++", 3, 10);
   osDelay(1100);

   int n = 0;
   res = atCmd_waitResponse("ATH\r\n", GSM_OK_Str, "NO CARRIER", 5, 3000, NULL, 0);
   while (res == 0)
   {
      n++;
      if (n > 10)
      {
#if GSM_DEBUG
         printf("GSM: STILL CONNECTED.\r\n");
#endif
         n = 0;
         osDelay(1000);
         HAL_UART_Transmit(&huart6, (uint8_t*)"+++", 3, 10);
         osDelay(1000);
      }
      osDelay(100);
      res = atCmd_waitResponse("ATH\r\n", GSM_OK_Str, "NO CARRIER", 5, 3000, NULL, 0);
   }
   osDelay(100);
   if (rfOff)
   {
      cmd_Reg.timeoutMs = 10000;
      res = atCmd_waitResponse("AT+CFUN=4\r\n", GSM_OK_Str, NULL, 11, 3000, NULL, 0);
   }
#if GSM_DEBUG
   printf("GSM: DISCONNECTED.\r\n");
#endif
}

void PPPosClientThread()
{
   printf("GSM: Starting Modem thread\r\n");

   osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
   pppos_task_started = 1;
   osSemaphoreRelease(pppos_mutex);

   int gsmCmdIter = 0;
   // int nfail = 0;

   uint8_t* data = (uint8_t*)malloc(BUF_SIZE);

   char PPP_ApnATReq[sizeof(CONFIG_GSM_APN) + 24];
   sprintf(PPP_ApnATReq, "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", CONFIG_GSM_APN);
   cmd_APN.cmd = PPP_ApnATReq;
   cmd_APN.cmdSize = strlen(PPP_ApnATReq);


   _disconnect(1); // Disconnect if connected

   osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
   pppos_tx_count = 0;
   pppos_rx_count = 0;
   gsm_status = GSM_STATE_FIRSTINIT;
   osSemaphoreRelease(pppos_mutex);

   enableAllInitCmd();

   while (1)
   {
      while (gsmCmdIter < GSM_InitCmdsSize)
      {
         if (GSM_Init[gsmCmdIter]->skip)
         {
#if GSM_DEBUG
            // infoCommand(GSM_Init[gsmCmdIter]->cmd, GSM_Init[gsmCmdIter]->cmdSize, "Skip command:");
#endif
            gsmCmdIter++;
            continue;
         }
         if (atCmd_waitResponse(GSM_Init[gsmCmdIter]->cmd,
            GSM_Init[gsmCmdIter]->cmdResponseOnOk, NULL,
            GSM_Init[gsmCmdIter]->cmdSize,
            GSM_Init[gsmCmdIter]->timeoutMs, NULL, 0) == 0)
         {
            // * No response or not as expected, start from first initialization command
#if GSM_DEBUG
            printf("GSM: Wrong response, restarting...\r\n");
#endif

            // nfail++;
            // if (nfail > 20)
            //    goto exit;

            osDelay(3000);
            gsmCmdIter = 0;
            continue;
         }

         if (GSM_Init[gsmCmdIter]->delayMs > 0)
            osDelay(GSM_Init[gsmCmdIter]->delayMs);
         GSM_Init[gsmCmdIter]->skip = 1;
         if (GSM_Init[gsmCmdIter] == &cmd_Reg)
            GSM_Init[gsmCmdIter]->delayMs = 0;
         // Next command
         gsmCmdIter++;
      }

#if GSM_DEBUG
      printf("GSM: GSM initialized.\r\n");
#endif

      osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
      if (gsm_status == GSM_STATE_FIRSTINIT)
      {
         osSemaphoreRelease(pppos_mutex);
         // osDelay(5000);

         // ** After first successful initialization create PPP control block
         ppp = pppos_create(&ppp_netif, ppp_output_callback, ppp_status_cb, NULL);

         if (ppp == NULL)
         {
#if GSM_DEBUG
            printf("GSM: Error initializing PPPoS\r\n");
#endif
            break; // end task
         }
      }
      else
         osSemaphoreRelease(pppos_mutex);


      osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
      gsm_status = GSM_STATE_IDLE;
      osSemaphoreRelease(pppos_mutex);
      UART_LastPos = BUF_SIZE - __HAL_DMA_GET_COUNTER(huart6.hdmarx);
      ppp_set_usepeerdns(ppp, 1);
      ppp_set_auth(ppp, PPPAUTHTYPE_NONE, NULL, NULL);
      ppp_connect(ppp, 0);

      while (1)
      {
         // === Check if disconnect requested ===
         osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
         if (do_pppos_connect <= 0)
         {
            int end_task = do_pppos_connect;
            do_pppos_connect = 1;
            osSemaphoreRelease(pppos_mutex);
#if GSM_DEBUG
            printf("\r\n");
            printf("GSM: Disconnect requested.\r\n");
#endif

            ppp_close(ppp, 0);
            int gstat = 1;
            while (gsm_status != GSM_STATE_DISCONNECTED)
            {
               // Handle data received from GSM
               int len = UART_Read(data, BUF_SIZE, 30);
               if (len > 0)
               {
                  pppos_input_tcpip(ppp, (u8_t*)data, len);
                  osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
                  pppos_tx_count += len;
                  osSemaphoreRelease(pppos_mutex);
               }
               osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
               gstat = gsm_status;
               osSemaphoreRelease(pppos_mutex);
            }
            osDelay(1000);

            osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
            uint8_t rfoff = gsm_rfOff;
            osSemaphoreRelease(pppos_mutex);
            _disconnect(rfoff); // Disconnect GSM if still connected

#if GSM_DEBUG
            printf("GSM: Disconnected.\r\n");
#endif

            gsmCmdIter = 0;
            enableAllInitCmd();
            osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
            gsm_status = GSM_STATE_IDLE;
            do_pppos_connect = 0;
            osSemaphoreRelease(pppos_mutex);

            if (end_task < 0) goto exit;

            // === Wait for reconnect request ===
            gstat = 0;
            while (gstat == 0)
            {
               osDelay(100);
               osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
               gstat = do_pppos_connect;
               osSemaphoreRelease(pppos_mutex);
            }
#if GSM_DEBUG
            printf("\r\n");
            printf("GSM: Reconnect requested.\r\n");
#endif
            break;
         }

         // === Check if disconnected ===
         if (gsm_status == GSM_STATE_DISCONNECTED)
         {
            osSemaphoreRelease(pppos_mutex);
#if GSM_DEBUG
            printf("\r\n");
            printf("GSM: Disconnected, trying again...\r\n");
#endif
            ppp_close(ppp, 0);
            _disconnect(1);

            enableAllInitCmd();
            gsmCmdIter = 0;
            gsm_status = GSM_STATE_IDLE;
            osDelay(10000);
            break;
         }
         else
            osSemaphoreRelease(pppos_mutex);

         // === Handle data received from GSM ===
         // memset(data, 0, BUF_SIZE);
         // int len = UART_Read(data, BUF_SIZE, 50);
         // if (len > 0)
         //uint8_t d;
         int len = 0;
         if( UART_DMA_GetBytesAvailable() && (len = UART_DMA_Read(data, BUF_SIZE)) > 0 )
         //if(HAL_OK == HAL_UART_Receive(&huart6, &d, sizeof(d), 1))
         {
            // printf("<- %d\n", len);
            // pppos_input_tcpip(ppp, (u8_t*)&d, 1);
            pppos_input_tcpip(ppp, data, len);
            // osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
            // pppos_tx_count += 1;//len;
            // osSemaphoreRelease(pppos_mutex);
         }
         osDelay(1); //!!
         // printf(".");
      }  // Handle GSM modem responses & disconnects loop
   }  // main task loop

exit:
   if (data) free(data);  // free data buffer
   if (ppp) ppp_free(ppp);

   osSemaphoreWait(pppos_mutex, PPPOSMUTEX_TIMEOUT);
   pppos_task_started = 0;
   gsm_status = GSM_STATE_FIRSTINIT;
   osSemaphoreRelease(pppos_mutex);
#if GSM_DEBUG
   printf("GSM: PPPoS TASK TERMINATED\r\n");
#endif
   //osThreadTerminate(mythread); 
}