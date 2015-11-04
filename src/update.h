/********************************************************************\
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

/* $Id$ */
/**
  @file update.h
  @brief Auto update functions
  @author Copyright (C) 2015 CTBRI <guojia@ctbri.com.cn>
*/

#ifndef _UPDATE_
#define _UPDATE_

#define DEBUG 0

#if DEBUG == 0
#define DELAY_TIME 1200
#define INTERVAL_TIME 300
#else
#define DELAY_TIME 5
#define INTERVAL_TIME 10
#endif

#define UPDATE_FILE "/tmp/ctbri.bin"
#define VER_LENGTH 20

void thread_update(void *arg);
/* Delay time until 2:00 next day */
int delay_to_next_day();
/* Delay sending request for a random time
 * Using random function with seed which is the last 2 character of MAC address */
unsigned int random_delay_time();
/* Check the current time whether in the period 2:00 - 5:00 */
int in_update_time_period(unsigned int delay_time);
/* Main procedure of update */
int update(void);
int send_request(int sockfd, char *request, char *response);
/* Retrieve update file from update server */
int retrieve_update_file(char *request);
/* Check network traffic, if the rate is high, check it minutes later
 * maximum check times is 3 */
unsigned long int check_network_traffic();
/* Get received network traffic from local file "/proc/net/dev" */
unsigned long int get_network_traffic();
/* Execute update command */
int do_update();
/* XXX Get update version from update url, mainly from the name of update file,
 * the premiss is that update version is included in update file name */
int get_update_ver(char *update_url, char *update_ver);

#endif


