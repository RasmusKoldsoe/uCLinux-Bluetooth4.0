/*
 * serialLogic.h
 *
 *  Created on: Sep 19, 2013
 *      Author: rasmus
 */

#ifndef SERIALLOGIC_H_
#define SERIALLOGIC_H_

#define VERBOSE_SERIAL 1

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "../ble_device.h"
#include "../Queue/Queue.h"
#include "../Parser/Parser.h"
#include "../NetworkStat/NetworkStatistics.h"

//queue_t txQueue;
//queue_t rxQueue;

int open_serial(char *port, int oflags);
void close_serial(int fd);
void *read_serial(void *_bleCentral);
void *write_serial(void *_bleCentral);

#endif /* SERIALLOGIC_H_ */
