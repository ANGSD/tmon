/****************************************************************************
forked by thorfinn thorfinn.sand@gmail.com
                   tMon - Distributed Resource Monitor

                   Copyright (C) 1999 Jaco Breitenbach

    This code is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this package; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Jaco Breitenbach can be contacted at <jjb@dsp.sun.ac.za> 

****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>
#include <termios.h>
#include <sgtty.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <math.h>


#define MSGLEN 64

#define ETC_RCFILE "/etc/tmonrc"
#define USER_RCFILE ".tmonrc"

#define APPNAME "tMon"
#define CREDITS "by Thorfinn Sand Korneliussen (2009)"
#define LEGEND  "            CPU                      Memory                     Swap            "
#define CPU  "CPU"
#define MEM  "Memory"
#define SWAP "Harddrive"
#define minsize 40

#define MAJ_VER "0.4"
#define MIN_VER ".3"

#define DEFAULT_COLOUR (attr_t) 0

 
struct hostitem	{
  char hostname[64];
  unsigned short int port;
  int sock_fd;
  struct sockaddr_in addr;
  float ncore,nmem,cpu, mem, swp;
  char active;
  struct hostitem *prev, *next;
};


struct hostitem	*firsthost=NULL, *lasthost=NULL; 
struct hostitem	*topdisphost=NULL, *botdisphost=NULL;
static int interrupted, waiting, NUM_HOSTS;


int connect_socket (struct hostitem *server);
int addhost (const char *hostname, unsigned short int port);
void remove_host (struct hostitem *ptr);
void parse_rcfile (const char *rcfilename);
void bar_draw (int len, float val);
static void show_all(void);
static void finish(int sig);
static void adjust(int sig);
void Usage (void);


int connect_socket (struct hostitem *server)	{
  struct hostent *hostinfo;
  static int pos = 0;
  int retval = 0;
  if ((server->sock_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)	{
    endwin();
    perror("Socket");
    exit(1);
  }
  
  server->addr.sin_family = AF_INET;
  server->addr.sin_port = htons(server->port);
  hostinfo = gethostbyname(server->hostname);
  if (hostinfo == NULL)	{
    endwin();
    attrset(COLOR_PAIR(3) | A_BOLD);
    mvprintw(pos,0, "Unknown host: \"%s\" port: \"%d\"\n", server->hostname,server->port);
    pos++;
    refresh();
    sleep(2);
    remove_host(server);
  }else{
    server->addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
    attrset(DEFAULT_COLOUR);
    mvprintw(pos,0,"Connecting to host %s : %d",server->hostname, server->port);
    refresh();
    if (connect(server->sock_fd, (struct sockaddr *) &server->addr, 
		sizeof(server->addr)) < 0)	{
      attrset(COLOR_PAIR(3) | A_BOLD);
      mvprintw(pos,65,"Failed");
      mvprintw(++pos,0, "Host:\"%s\" is online but no daemon is running...\n",server->hostname);
      pos++;
      refresh();
      sleep(2);
      remove_host(server);
    }
    else	{ 
      attrset(COLOR_PAIR(1) | A_BOLD);
      mvprintw(pos++,65,"Success");
      retval = 1;
    }
    refresh();
  }
  return retval;
}


int addhost (const char *hostname, unsigned short int port)	{
  struct hostitem *ptr;

  if ((ptr=(struct hostitem *) calloc(sizeof(struct hostitem),1)) == NULL) {
    endwin();
    perror("calloc()");
    exit(1);
  }
  
  if (firsthost == NULL)	{
    firsthost = ptr;
    ptr->prev = NULL;
  }
  else	{ 
    lasthost->next = ptr;
    ptr->prev = lasthost; 
  }
  
  ptr->next = NULL;
  strncpy(ptr->hostname, hostname, 64);
  ptr->port = port;
  ptr->active = 1;
  
  lasthost = ptr;
  return connect_socket(ptr);
}
void remove_host (struct hostitem *ptr)	{
  if (ptr != NULL)	{
    if (lasthost == ptr) { lasthost = ptr->prev; }
    if (firsthost == ptr) { firsthost = ptr->next; }
    if (ptr->prev != NULL) { ptr->prev->next = ptr->next; }
    if (ptr->next != NULL) { ptr->next->prev = ptr->prev; }
    free(ptr);
    NUM_HOSTS--;
  }
}


void parse_rcfile (const char *rcfilename)	{
  FILE *rcfile;
  static char hostline[80];
  static char hostname[64];
  static int port;
  int astop = 0;
  NUM_HOSTS = 0;
  
  if (((rcfile=fopen(rcfilename, "r")) == NULL)
      && ((rcfile=fopen(ETC_RCFILE, "r")) == NULL))	{
    endwin();
    perror("fopen(RCFILE)");
    Usage();
    exit(1);
  }
  while (fgets(hostline, 80, rcfile) != NULL)	{
    if(strlen(hostline)>1&&hostline[0]=='#')
      continue;
    if (sscanf(hostline, "%s %d", hostname, &port) == 2)	{
      if(addhost(hostname, (unsigned short int) port)!=1)
	astop++;
      NUM_HOSTS++;
    }
  }
  if(astop){
    int x,y;
    getyx(stdscr,x,y);
    attrset(DEFAULT_COLOUR);
    mvprintw(x+2,0,"Problems finding tmond daemons from serverlist, will pause 4 seconds");
    refresh();
    sleep(4);
  }
  fclose(rcfile);
}


void bar_draw (int len, float val)	{
  int num;
  int i, colour;

  num = ceil(((double) len) * val / 100.);
  num = num > len ? len : num;

  colour = floor(val/33.) + 1;
  colour = colour > 3 ? 3 : colour;
  attrset(COLOR_PAIR(colour) | A_BOLD);
  
  for (i = 0; i < num; i++)
    addch('=');
  for (i = 0; i < len-num; i++)
    addch(' ');
}


static void show_all(void)	{
  struct hostitem *disphost;
  int n, pos = 3;
  div_t	result;
  
  result = div(LINES+1, 3);

  scrollok(stdscr, FALSE);
  disphost = topdisphost;
  
  move(0,0);
  attrset(DEFAULT_COLOUR | A_BOLD);
  n = (COLS-(strlen(APPNAME)+strlen(MAJ_VER)+			\
	     strlen(MIN_VER)+strlen(CREDITS)+5))/2;
  printw("%*s (v%s%s) %s", n+strlen(APPNAME), APPNAME,	\
	 MAJ_VER, MIN_VER, CREDITS);
  clrtoeol();
  move(2,0);
  attrset(DEFAULT_COLOUR | A_REVERSE);
  printw("%*s%*s%*s",
	 4+((COLS-20)/3-strlen(CPU))/2+strlen(CPU), CPU,
	 ((COLS-20)/3-strlen(CPU))/2+
	 7+((COLS-20)/3-strlen(MEM))/2+strlen(MEM), MEM,
	 ((COLS-20)/3-strlen(MEM))/2+
	 6+((COLS-20)/3-strlen(SWAP))/2+strlen(SWAP), SWAP);
  clrtoeol();
  chgat(-1, A_REVERSE, DEFAULT_COLOUR, NULL);
  
  while ((disphost != NULL) && (pos < 3*result.quot))	{
    move(pos++, 0);
    attrset(DEFAULT_COLOUR);
    printw("%s: (%.0f Cores,%.2f Gig Memory)", disphost->hostname, disphost->ncore,disphost->nmem);
    clrtoeol();
    move(pos++, 0);
    attrset(DEFAULT_COLOUR);
    printw("%3.0f%%[", disphost->cpu);
    bar_draw((COLS - 20) / 3, disphost->cpu);
    attrset(DEFAULT_COLOUR);
    printw("]%3.0f%%[", disphost->mem);
    bar_draw((COLS - 20) / 3, disphost->mem);
    attrset(DEFAULT_COLOUR);
    printw("]");
    if (disphost->swp >= 0) {
      printw("%3.0f%%[", disphost->swp);
      bar_draw((COLS - 20) / 3, disphost->swp);
      attrset(DEFAULT_COLOUR);
      printw("]");
    }
    clrtoeol();
    move(pos++, 0);
    clrtoeol();
    disphost = disphost->next;
  }
  clrtobot();
  botdisphost = disphost;
  setscrreg(3, LINES-3);
  scrollok(stdscr, TRUE);
  refresh();
}


static void finish(int sig)	{
  endwin();
  exit(sig);
}


static void adjust(int sig)	{
  if (waiting || sig == 0)	{
    struct winsize size;
    
    if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0)	{
      resizeterm(size.ws_row, size.ws_col);
      wrefresh(curscr);
      topdisphost = firsthost;
      show_all();
    }
    interrupted = FALSE;
  } 
  else	{
    interrupted = TRUE;
  }
  signal(SIGWINCH, (void *) &adjust);
}


void Usage (void)	{
  printf("\nUsage:\ttmon [-v] [-f rcfile]\nThe client for the tMon daemon. ");
  printf("Hosts running the tmond daemon are listed in\nthe tmonrc file. ");
  printf("The default rcfile is .tmonrc in the user's home directory,\n");
  printf("or /etc/tmonrc if the first does not exist. ");
  printf("The format is as follows:");
  printf("\n\n\thostname\t\t\tport\ne.g.\n");
  printf("\twintermute.dsp.sun.ac.za\t7777\n");
  printf("\tsaiph.dsp.sun.ac.za\t\t7777\n\n");
  exit(1);
}


void Version (void)	{
  printf("tMon client, version %s%s\n", MAJ_VER, MIN_VER);
  exit(0);
}


int main (int argc, char *argv[])	{
  int done = 0, result, c;
  struct hostitem *thishost;
  fd_set act_rd_set, rd_set;
  float	ncore,nmem,cpu, mem, swp;
  char	buf[MSGLEN];
  static int my_bg = COLOR_BLACK;
  char rcfile[64] = "";
  char	opt, *home;
  

  if ((home = getenv("HOME")) != NULL)	{
    strncpy(rcfile, home, 64);
    if (strlen(rcfile) < 64) 
      strncat(rcfile, "/", 1);
  }
  strncat(rcfile, USER_RCFILE, 64-strlen(rcfile));
  
  while ((opt=getopt(argc,argv,"f:vh")) != EOF )	{
    switch(opt)	{
    case 'f':	{
      strncpy(rcfile, optarg, 64);
      break;
    }
    case 'v':	{
      Version();
      break;
    }
    default:	Usage();
    }	
  }
  
  initscr();
  keypad(stdscr, TRUE);
  nonl();
  cbreak();
  noecho();
  idlok(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  if (has_colors())	{
    start_color();
    if (use_default_colors() == OK)
      my_bg = -1;
  }
  curs_set(0);
  init_pair(1,2,my_bg);
  init_pair(2,3,my_bg);
  init_pair(3,1,my_bg);
  erase();
  parse_rcfile(rcfile);
  erase();
  
  if ((thishost = firsthost) != NULL)	{
    FD_ZERO (&act_rd_set);
    while (thishost != NULL)	{
      FD_SET(thishost->sock_fd, &act_rd_set);
      thishost = thishost->next;
    }
    
    signal(SIGINT, (void *) &finish);
    signal(SIGWINCH, (void *) &adjust);
    signal(SIGPIPE, SIG_IGN);
    
    topdisphost = firsthost;
    
    while(!done)	{
      rd_set = act_rd_set;
      result = select(FD_SETSIZE, &rd_set, NULL, NULL, NULL);
      thishost = firsthost;
      while (thishost != NULL)	{
	if (FD_ISSET(thishost->sock_fd, &rd_set))	{
	  if (recv(thishost->sock_fd, buf, MSGLEN, 0) == MSGLEN)	{
	    //fprintf(stderr,"%s\n",buf);
	    if (sscanf(buf,"cores:%f mem:%f cpu:%f mem:%f swp:%f",&ncore,&nmem, 
		       &cpu, &mem, &swp) == 5)	{
	      //	      fprintf(stderr,"\nncores:%f,nmem:%f cpu:%f mem:%f\n",ncore,nmem,cpu,mem);
	      thishost->ncore = ncore;
	      thishost->nmem = nmem;
	      thishost->cpu = cpu;
	      thishost->mem = mem;
	      thishost->swp = swp;
	    }else{
	      fprintf(stderr,"Error parsing socket message\n");
	      exit(0);

	    }
	  }
	}
	thishost = thishost->next;
      }
      
            show_all();
      
      if (interrupted) adjust(0);
      waiting = TRUE;
      c = getch();
      waiting = FALSE;
      
      switch(c) {
      case KEY_DOWN:
	if ((botdisphost != NULL) && (topdisphost != NULL))	{
	  topdisphost = topdisphost->next;
	  wscrl(stdscr, 1);
	}
	break;
      case KEY_UP:
	if ((topdisphost!=firsthost) && (topdisphost!=NULL))	{
	  topdisphost = topdisphost->prev;
	  wscrl(stdscr, -1);
	} 
	break;
      case 'q':
	done = TRUE;
	break;
      case ERR:
      case KEY_RESIZE:
	break;
      default:
	beep();
      }
      usleep(100000);
    }
  }
  finish(0);
  return 0;
}



