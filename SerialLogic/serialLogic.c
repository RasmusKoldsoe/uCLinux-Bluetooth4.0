/*
 * serialLogic.c
 *
 *  Created on: Sep 19, 2013
 *      Author: rasmus
 */
#include "serialLogic.h"
#include <termios.h>
#include <sys/time.h>

void print_byte_array(char *buff, int length, int offset)
{
#if (defined VERBOSITY) && (VERBOSITY >= 2) // 3
	int i;
	for(i=offset; i<length; i++) {
		printf("%02X ", buff[i] & 0xff);
	}

#endif
#if (defined VERBOSITY) && (VERBOSITY >= 2)
	printf("\n");
#endif
}

int open_serial(char *port, int oflags)
{
	struct termios tio;
	fd_set rdset;
	unsigned char c=0;

	memset(&tio,0,sizeof(tio));
	tio.c_iflag=0;
	tio.c_oflag  = 0; //RAw output &= ~OPOST; //0;
	tio.c_cflag &= CRTSCTS | CS8 | CLOCAL | CREAD; //| CRTSCTS | CS8 | CLOCAL | CREAD;
	tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // (ICANON | ECHO | ECHOE);
	tio.c_cc[VMIN]=0;
	tio.c_cc[VTIME]=10;

	int fd;
	fd = open(port, oflags);
	if( fd < 0 ){
		//printf("Error opening port %s\n",port);
		return fd;
	}

	cfsetospeed(&tio,B115200);
	cfsetispeed(&tio,B115200);
	tcsetattr(fd,TCSANOW,&tio);

	initNetworkStat();
	return fd;
}

void close_serial(int fd)
{
	close(fd);
}

void *read_serial(void *_bleCentral)
{
	int n, offset=0;
	unsigned int next, count=0; 
	char buff[STD_BUF_SIZE]; memset(buff, 0, STD_BUF_SIZE);
	BLE_Central_t *bleCentral = (BLE_Central_t *)_bleCentral;
	datagram_t datagram;
	enum parserState_t parserState = package_type_token;
	enum parserState_t previousParserState = package_type_token;
	debug(1, "Read function started\n");

	while(bleCentral->_run) {
		n = read(bleCentral->fd, buff + count, 1);

		if ( n < 0 ) {
			printf("ERROR reading message header: Read return %d\n", n);
			break;
		}
		else if( n == 0 ) {
			usleep(STD_WAIT_TIME);
			continue;
		}
		
		count += n;
		updateRxStat(0, n);

		previousParserState = parserState;
		parserState = parse_data(&datagram, buff, count, &offset, parserState);

		if( parserState == package_type_token && previousParserState != parserState ) {
			debug(2, "Datagram received ");
			gettimeofday( &datagram.timestamp, NULL );
			print_byte_array(buff, count, 0);
			pretty_print_datagram(&datagram);

			enqueue(&bleCentral->rxQueue, &datagram);	
			updateRxStat(1, 0);

			memset(&datagram, 0, sizeof(datagram_t));
			offset = 0;
			count = 0;
		}
	}

	debug(1, "Read thread exiting\n");
	return NULL;
}

void *write_serial(void *_bleCentral)
{
	int n, l;
	char msg[STD_BUF_SIZE]; memset(msg, 0, STD_BUF_SIZE);
	BLE_Central_t *bleCentral = (BLE_Central_t *)_bleCentral;
	datagram_t datagram;
	debug(1, "Write thread started\n");

	while(bleCentral->_run){
		if(queueCount(&bleCentral->txQueue) == 0) {
			usleep(STD_WAIT_TIME);
			continue;
		}

		dequeue(&bleCentral->txQueue, &datagram);
		compose_datagram(&datagram, msg, &l);

		n = write(bleCentral->fd, msg, l);
		updateTxStat(1, n);
		debug(2, "Message sent (%d bytes) ", n);
		print_byte_array(msg, l, 0);
		pretty_print_datagram(&datagram);
	}
	debug(1, "Write thread exiting\n");
	return NULL;
}
