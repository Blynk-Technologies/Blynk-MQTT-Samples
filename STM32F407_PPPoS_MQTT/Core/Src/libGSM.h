#ifndef LIB_GSM_H_
#define LIB_GSM_H_

#define GSM_STATE_DISCONNECTED	0
#define GSM_STATE_CONNECTED		1
#define GSM_STATE_IDLE			89
#define GSM_STATE_FIRSTINIT		98

void PPPosClientThread();
int LTE_ppposInit();

#endif