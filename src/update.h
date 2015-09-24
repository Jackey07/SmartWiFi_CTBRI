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

#define UPDATE_FILE "/tmp/chinanet.bin"

void thread_update(void *arg);

/* Delay sending request for a random time
 * Using random function with seed which is the last 2 character of MAC address */
unsigned int random_delay_time();
int in_update_time_period(unsigned int delay_time);
int update(void);
char* send_request(int sockfd, char *request);
int retrieve_update_file(char *request);

/* Check network traffic, if the rate is high, check it minutes later
 * maximum check times is 3 */
unsigned long int check_network_traffic();
unsigned long int get_network_traffic();
int do_update();

#endif
