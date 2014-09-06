#include <sys/types.h>
#include <netdb.h>

#ifndef CONSTANTS_H
#define CONSTANTS_H

#define BUF_SIZE 1024
#define TRUE 1
#define FALSE 0

const uint16_t PORT_DEF = 10000 + (282146%10000);
const size_t FIFO_SIZE_DEF = 10560;
const int FIFO_LOW_WATERMARK_DEF = 0;
const int BUF_LEN_DEF = 10;
const int RETRANSMIT_LIMIT_DEF = 10;
const int TX_INTERVAL_DEF = 5;

#endif