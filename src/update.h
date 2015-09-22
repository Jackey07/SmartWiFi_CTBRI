/********************************************************************\
 *
 *
\********************************************************************/

#ifndef _UPDATE_
#define _UPDATE_

#define UPDATE_FILE "/tmp/chinanet.bin"

void thread_update(void *arg);
unsigned int random_delay_time();
int in_update_time_period();
int update(void);
int retrieve_update_file(char *request);
unsigned long int network_traffic();
int do_update();

#endif
