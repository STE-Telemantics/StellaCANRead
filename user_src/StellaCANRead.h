#ifndef STELLA_CAN_READ_H
#define STELLA_CAN_READ_H

#define QUEUE_SIZE 64
#define CAN_BITRATE 500 // Bitrate in kbit/s
#define CAN_SAMPLEP 80
#define MSG_PER_FILE 250000
#define MOUNT_PATH "/mnt"

#include <sys/types.h>

// Define thread functions

void data_handler();
void socket_can_reader();
void sd_controller();

#endif