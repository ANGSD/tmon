/****************************************************************************
forked by thorfinn thorfinn@binf.ku.dk
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
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/statvfs.h>
#define MSGLEN 64
#define BAD_OPEN_MESSAGE "Error: /proc filesystem not mounted.\n"
#define STAT_FILE "/proc/stat"
#define MEMINFO_FILE "/proc/meminfo"
#define VERSION "0.3"
#define SLEEPTIME 250000
#define DISKWAIT 20 //This means that the disk will only be check every 5th sec.
#define NOBODY "nobody"
#define PIDFILE "/var/run/tmond.pid"

pthread_attr_t attr;

#define FILE_TO_BUF(filename, fd) do	{						\
    static int n;							\
    if (fd == -1 && (fd = open(filename, O_RDONLY)) == -1) {	\
      log_err("%s", BAD_OPEN_MESSAGE);					\
      close(fd);							\
      exit(1);								\
    }									\
    lseek(fd, 0L, SEEK_SET);						\
    if ((n = read(fd, buf, sizeof buf - 1)) < 0) {			\
      close(fd);							\
      fd = -1;								\
    }									\
    buf[n] = '\0';							\
  }while(0)


static char message[MSGLEN];
pthread_mutex_t	mut = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t	mut2 = PTHREAD_MUTEX_INITIALIZER;
static char buf[1024];
static char *appname;
int sock;
int ncores_val;
float mems_val;
int SIG_STOP = 1;
void log_init (void)	{
  closelog();
  openlog(appname, LOG_PID, LOG_DAEMON);
}


void log_msg (const char *fmt, ...)	{
  char msg[1024];
  va_list args;
  
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  syslog(LOG_INFO, "%.500s", msg);
}

int ncores(){
  FILE * fp;
  char res[128];
  memset(res,0,128);
  fp = popen("/bin/cat /proc/cpuinfo |grep -c '^processor'","r");
  fread(res, 1, sizeof(res)-1, fp);
  fclose(fp);
  return atoi(res);
}

float nmem(){
  FILE * fp;
  char res[128];
  memset(res,0,128);
  fp = popen("/usr/bin/head -n1 /proc/meminfo","r");
  fread(res, 1, sizeof(res)-1, fp);
  float val;
  sscanf(res,"MemTotal: %f kB\n",&val);
  // fprintf(stderr,"d:%f\n",val);
  fclose(fp);
  return val/1024.0/1024.0;
}

void log_err (const char *fmt, ...)	{
  char msg[1024];
  va_list args;
  
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);
  syslog(LOG_ERR, "%.500s", msg);
}


static void sigterm_handler (int sig)	{
  log_msg("SIGTERM signal received");
  SIG_STOP = 0;
  pthread_mutex_lock(&mut2);
  close(sock);
  pthread_mutex_unlock(&mut2);
  exit(0);
}


int send_to_client (int sock_fd)	{
  int		result; 
  
  if (sock_fd >= 0)	{
    /* Lock and unlock the shared message, just to be safe. */
    pthread_mutex_lock(&mut);
    result = send(sock_fd, message, MSGLEN, 0);
    pthread_mutex_unlock(&mut);
    if (result < MSGLEN) return 1;
  }
  return 0;
}


void spawn (int *client)	{
  int						sock_fd;
  struct sockaddr_in		addr;
  socklen_t				addrlen = sizeof(addr);
  
  /* Set signal SIGPIPE to be ignored. */
  signal(SIGPIPE, SIG_IGN);
  sock_fd = (int) *client;
  if (getsockname(sock_fd, (struct sockaddr *) &addr, &addrlen))	{
    close(sock_fd);
    pthread_exit(NULL);
  }
  log_msg("Connection from %s on port %hd", 
	  inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
  /* Upon error, this thread will self-destruct. */
  while (1)	{
    if (send_to_client(sock_fd))	{
      log_msg("Closing connection to %s", inet_ntoa(addr.sin_addr));
      close(sock_fd);
      pthread_exit(NULL);
    }
    usleep(SLEEPTIME);
  }
}


int make_socket (unsigned short int port)	{
  int yes  = 1;
  int sock;
  struct sockaddr_in	addr;
  
  /* The initial server side socket is created and initialized. */
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)	{
    log_err("Socket: %s", strerror(errno));
    exit(1);
  }
  /* Allow the port to be reused if we die */
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    log_err("SocketOpt: %s", strerror(errno));
    exit(1);
  }
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)))	{
    log_err("Bind: %s", strerror(errno));
    exit(1);
  }
  listen(sock, SOMAXCONN);
  return sock;
}


void get_statistics (void *ptr) {
  static int		fd_stat=-1, fd_meminfo=-1;
  long			cpu_used, idle;
  static long		cpu_old, idle_old;
  long			rt_cpu, rt_idle, cpu_total;
  float			cpu, mem, swp;
  int				i;
  struct statvfs diskinfo;
  int runTimes = 0; 
  char *filename = "/";
  /* A dedicated thread is spawned to update the statistics. It is noted
   * that it is necessary to read the whole /proc file in order to force
   * an update. To restrict CPU usage, memory and swap info is updated
   * only once for every 8 updates in CPU info.
   */
  pthread_mutex_lock(&mut2);
  while (SIG_STOP)	{
    FILE_TO_BUF(MEMINFO_FILE, fd_meminfo);
    {
      long long total=1, memfree=0, buffers=0, cached=0;
      sscanf(buf, "%*[^T]Total: %lld", &total);
      sscanf(buf, "%*[^F]Free: %lld", &memfree);
      sscanf(buf, "%*[^\n]%*[^u]uffers: %lld", &buffers);
      sscanf(buf, "%*[^C]Cached: %lld", &cached);
      mem = 100.*(total - memfree - buffers - cached)/total;
    }
    {
      if (!statvfs(filename, &diskinfo)) 
	swp = (100* (1-(float)diskinfo.f_bavail/diskinfo.f_blocks));
      else
	swp = 100;
      runTimes = 0;
    }
    //swp=runTimes;
    for (i=0; i<8; i++)	{
      FILE_TO_BUF(STAT_FILE, fd_stat);
      {
	long a, b, c;
	sscanf(buf,"cpu %ld %ld %ld %ld",&a,&b,&c,&idle);
	cpu_used = a + b + c;
      }
      
      rt_cpu = cpu_used - cpu_old;
      rt_idle = idle - idle_old;
      cpu_total = rt_cpu+rt_idle;
      if (cpu_total > 0) { cpu = 100.*rt_cpu/cpu_total; }
      else { cpu = 0.; }
      cpu_old = cpu_used;
      idle_old = idle;
      
      /* Lock and unlock the shared message space. */
      pthread_mutex_lock(&mut);
      
      snprintf(message, MSGLEN,"cores:%d mem:%f cpu:%6.2f mem:%6.2f swp:%6.2f",ncores_val,mems_val, cpu, mem, swp);
      //fprintf(stderr,"%s\n",message);
      pthread_mutex_unlock(&mut);
      usleep(SLEEPTIME);
    }
    runTimes++;
  }
  fprintf(stderr,"Killing statcollector\n");fflush(stderr);
  pthread_mutex_unlock(&mut2);
  pthread_exit(NULL);
}


void set_nobody (void)	{
  struct passwd *pw;
  
  /* If the daemon was started as root, reset the effective uid and gid
   * to belong to user nobody. This is to prevent ANY security leaks.
   */
  if (!geteuid())	{
    /* We are running as root, so continue to set uid and gid to nobody.
     * First we search the password file for nobody's uid and gid.
     */
    pw = getpwent();
    while ((pw != NULL) && strncmp(pw->pw_name, NOBODY, sizeof(NOBODY)))
      pw = getpwent();
    endpwent();
    if (pw)	{
      /* Located nobody's password file entry. Proceed to set uid and gid
       * and additional groups. Do uid last, because we need that to 
       * change our groups too */
      if (setgid(pw->pw_gid))
	log_err("WARNING - Unable to set gid to nobody.");
      if (initgroups(NOBODY, pw->pw_gid))
	log_err("WARNING - Unable to set additional groups to nobody.");
      if (setuid(pw->pw_uid)) /* we aren't root after this... */
	log_err("WARNING - Unable to set uid to nobody.");
    }
    else	{
      log_err("WARNING - Unable to set effective uid to nobody.");
    }
  }
}


void Usage (void)	{
  printf("\nUsage:\ttmond [-p port]\nThe default port number is 7777.\n\n");
  exit(1);
}


int main (int argc, char *argv[])	{
  ncores_val = ncores();
  mems_val = nmem();
#if 0
  fprintf(stderr,"cnores:%d\n",ncores);
  fprintf(stderr,"mems:%f\n",mems);
  return 0;
#endif


  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int	new_sock, pidfile;
  FILE	*fhandle;
  int 	result;
  struct sockaddr_in 	clientname;
  int 	size = sizeof(struct sockaddr_in);
  unsigned short int	port=7777;
  pthread_t thread;
  int opt;
  /* Check commandline parameters. */
  while ((opt=getopt(argc,argv,"p:h")) != EOF)	{
    switch(opt)	{
    case 'p':	{
      port = (unsigned short) atoi(optarg);
      break;
    }
    default:	Usage();
    }	
  }
  
	/* Extract and save calling application name for syslog. */
  if (strchr(argv[0], '/'))
    appname = strrchr(argv[0], '/') + 1;
  else
    appname = argv[0];
  
  /* Fork, and have the parent exit. The child becomes the server. */
  if (fork())
    exit(0);
  
  /* Installing SIGTERM signal handler. */
  signal(SIGTERM, (void *) &sigterm_handler);
  /* Installing SIGINT signal handler. */
  signal(SIGINT, (void *) &sigterm_handler);
  
  /* Initialize syslog. */
  log_init();
  
  /* Redirect stdin, stdout, and stderr to /dev/null. */
  freopen("/dev/null", "r", stdin);
  freopen("/dev/null", "w", stdout);
  freopen("/dev/null", "w", stderr);
  
  /* Chdir to the root directory so that the current disk can be unmounted
   * if desired. 
   */
  if(chdir("/")){
    fprintf(stderr,"Problem chdir\n");
    return 0;
  }
  
  /* Check whether PID file already exists. If not, and we have the
   * necessary access permissions, write the current PID to the file.
   */
  if ((pidfile = open(PIDFILE, O_WRONLY | O_CREAT | O_EXCL,		\
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)	{
    if (errno == EEXIST)
      log_err("%s already exists.", PIDFILE);
    else
      log_err("Error opening %s.", PIDFILE);
  }
  else if ((fhandle = fdopen(pidfile, "w")) != NULL)	{
    fprintf(fhandle, "%u\n", (unsigned int)getpid());
    fclose(fhandle);
  }

  /* Set uid and gid to nobody (if possible and required). */
  set_nobody();
  
  /* Spawn a dedicated thread to update resource statistics. */
  result = pthread_create(&thread, &attr, (void *) &get_statistics, NULL);
  if (result)	{
    log_err("pthread_create: %s", strerror(errno));
    exit(1);
  }
  
  /* Create and bind socket. */
  sock = make_socket(port);
  
  log_msg("Daemon started on port %d", port);
  
  while (1)	{

    /* Block until new connection received. */
    new_sock = accept(sock, (struct sockaddr *) &clientname, &size);
    if (new_sock < 0)	{
      log_err("accept: %s", strerror(errno));
      exit(1);
    }
    else	{
      /* Spawn a new thread to handle the new connection. */
      result = pthread_create(&thread, &attr, (void *) &spawn,	\
			      (void *) &new_sock);
      if (result)	{
	log_err("pthread_create: %s", strerror(errno));
	exit(1);
      }
    }
  }
  return 0;
}



