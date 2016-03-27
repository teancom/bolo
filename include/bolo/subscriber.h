/*
  Copyright 2016 James Hunt <james@jameshunt.us>

  This file is part of Bolo.

  Bolo is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  Bolo is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along
  with Bolo.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BOLO_SUBSCRIBER_H
#define BOLO_SUBSCRIBER_H

int subscriber_init(void);

int subscriber_monitor_thread(void *zmq, const char *prefix, const char *endpoint);
int subscriber_scheduler_thread(void *zmq, int interval);
int subscriber_supervisor(void *zmq);

int subscriber_metrics(void *zmq, ...);

int subscriber_connect_monitor(void *zmq, void **zocket);
int subscriber_connect_scheduler(void *zmq, void **zocket);
int subscriber_connect_supervisor(void *zmq, void **zocket);

#endif
