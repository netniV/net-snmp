#include <config.h>

#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <sys/types.h>
#ifdef __alpha
#ifndef _BSD
#define _BSD
#define _myBSD
#endif
#endif
#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifdef __alpha
#ifdef _myBSD
#undef _BSD
#undef _myBSD
#endif
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <errno.h>
#include <signal.h>
#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#include <ctype.h>

#include "mibincl.h"
#include "struct.h"
#include "util_funcs.h"
#ifdef USING_ERRORMIB_MODULE
#include "errormib.h"
#else
#define setPerrorstatus(x) perror(x)
#endif
#include "read_config.h"
#include "mib_module_config.h"

#ifdef EXCACHETIME
static long cachetime;
#endif

extern int numprocs, numextens;

void
Exit(var)
  int var;
{
  fprintf(stderr, "Server Exiting with code %d\n",var);
  fclose (stderr);
  exit(var);
}

int shell_command(ex)
  struct extensible *ex;
{
  char shellline[STRMAX];
  FILE *shellout;
  
  sprintf(shellline,"%s > /tmp/shoutput",ex->command);
  ex->result = system(shellline);
  ex->result = WEXITSTATUS(ex->result);
  shellout = fopen("/tmp/shoutput","r");
  if((shellout = fopen("/tmp/shoutput","r")) != NULL) {
    if (fgets(ex->output,STRMAX,shellout) == NULL) {
      ex->output[0] = 0;
    }
    fclose(shellout);
  }
  unlink("/tmp/shoutput");
  return(ex->result);
}

#define MAXOUTPUT 300

int exec_command(ex)
     struct extensible *ex;
{
  int fd;
  FILE *file;
  
  if ((fd = get_exec_output(ex))) {
    
    file = fdopen(fd,"r");
    if (fgets(ex->output,STRMAX,file) == NULL) {
      ex->output[0] = 0;
    }
    fclose(file);
    close(fd);
    wait_on_exec(ex);
  } else {
    ex->output[0] = 0;
    ex->result = 0;
  }
  return(ex->result);
}

void wait_on_exec(ex)
  struct extensible *ex;
{
#ifndef EXCACHETIME
    if (ex->pid && waitpid(ex->pid,&ex->result,0) < 0) {
      setPerrorstatus("waitpid");
    }
    ex->pid = 0;
#endif
}

#define MAXARGS 30

int get_exec_output(ex)
  struct extensible *ex;
{
  int fd[2],i, cnt;
  char ctmp[STRMAX], *cptr1, *cptr2, argvs[STRMAX], **argv, **aptr;
#ifdef EXCACHETIME
  char cache[MAXCACHESIZE];
  long cachebytes;
  long curtime;
  static char lastcmd[STRMAX];
  int cfd;
  static int lastresult;
  int readcount;
#endif

#ifdef EXCACHETIME
  curtime = time(NULL);
  if (curtime > (cachetime + EXCACHETIME) ||
      strcmp(ex->command, lastcmd) != 0) {
    strcpy(lastcmd,ex->command);
    cachetime = curtime;
#endif
    if (pipe(fd)) 
      {
        setPerrorstatus("pipe");
#ifdef EXCACHETIME
        cachetime = 0;
#endif
        return 0;
      }
    if ((ex->pid = fork()) == 0) 
      {
        close(1);
        if (dup(fd[1]) != 1)
          {
            setPerrorstatus("dup");
            return 0;
          }
        close(fd[1]);
        close(fd[0]);
        for(cnt=1,cptr1 = ex->command, cptr2 = argvs; *cptr1 != 0;
            cptr2++, cptr1++) {
          *cptr2 = *cptr1;
          if (*cptr1 == ' ') {
            *(cptr2++) = 0;
            cptr1 = skip_white(cptr1);
            *cptr2 = *cptr1;
            if (*cptr1 != 0) cnt++;
          }
        }
        *cptr2 = 0;
        *(cptr2+1) = 0;
        argv = (char **) malloc((cnt+2) * sizeof(char *));
        aptr = argv;
        *(aptr++) = argvs;
        for (cptr2 = argvs, i=1; i != cnt; cptr2++)
          if (*cptr2 == 0) {
            *(aptr++) = cptr2 + 1;
            i++;
          }
        while (*cptr2 != 0) cptr2++;
        *(aptr++) = NULL;
        copy_word(ex->command,ctmp);
        execv(ctmp,argv);
        perror("execv");
        exit(1);
      }
    else
      {
        close(fd[1]);
        if (ex->pid < 0) {
          close(fd[0]);
          setPerrorstatus("fork");
#ifdef EXCACHETIME
          cachetime = 0;
#endif
          return 0;
        }
#ifdef EXCACHETIME
        unlink(CACHEFILE);
        if ((cfd = open(CACHEFILE,O_WRONLY|O_TRUNC|O_CREAT,0644)) < 0) {
          setPerrorstatus("open");
          cachetime = 0;
          return 0;
        }
        fcntl(fd[0],F_SETFL,O_NONBLOCK);  /* don't block on reads */
        for (readcount = 0; readcount <= MAXREADCOUNT &&
                         (cachebytes = read(fd[0],(void *) cache,MAXCACHESIZE));
                       readcount++) {
          if (cachebytes > 0)
            write(cfd,(void *) cache, cachebytes);
          else if (cachebytes == -1 && errno != EAGAIN) {
            setPerrorstatus("read");
            break;
          }
          else
            sleep (1);
        }
        close(cfd);
        close(fd[0]);
        /* wait for the child to finish */
        if (ex->pid > 0 && waitpid(ex->pid,&ex->result,0) < 0) {
          setPerrorstatus("waitpid()");
          cachetime = 0;
          return 0;
        }
        ex->pid = 0;
        ex->result = WEXITSTATUS(ex->result);
        lastresult = ex->result;
#else
        return(fd[0]);
#endif
      }
#ifdef EXCACHETIME
  }
  else {
      ex->result = lastresult;
  }
  if ((cfd = open(CACHEFILE,O_RDONLY)) < 0) {
    setPerrorstatus("open");
    return 0;
  }
  return(cfd);
#endif
}

int
clear_cache(action, var_val, var_val_type, var_val_len, statP, name, name_len)
   int      action;
   u_char   *var_val;
   u_char   var_val_type;
   int      var_val_len;
   u_char   *statP;
   oid      *name;
   int      name_len;
{
  
  long tmp=0;
  int tmplen=1000;

  if (var_val_type != ASN_INTEGER) {
    printf("Wrong type != int\n");
    return SNMP_ERR_WRONGTYPE;
  }
  asn_parse_int(var_val,&tmplen,&var_val_type,&tmp,sizeof(int));
  if (tmp == 1 && action == COMMIT) {
#ifdef EXCACHETIME
    cachetime = 0;                      /* reset the cache next read */
#endif 
  } 
  return SNMP_ERR_NOERROR;
}

extern char **argvrestartp, *argvrestartname;

RETSIGTYPE restart_doit(a)
int a;
{
  int i;
  
  /* close everything open */
  for (i=0; i<= 2; i++)
    close(i);

  /* do the exec */
  execv(argvrestartname,argvrestartp);
  setPerrorstatus("execv");
}

int
restart_hook(action, var_val, var_val_type, var_val_len, statP, name, name_len)
   int      action;
   u_char   *var_val;
   u_char   var_val_type;
   int      var_val_len;
   u_char   *statP;
   oid      *name;
   int      name_len;
{
  
  long tmp=0;
  int tmplen=1000;

  if (var_val_type != ASN_INTEGER) {
    printf("Wrong type != int\n");
    return SNMP_ERR_WRONGTYPE;
  }
  asn_parse_int(var_val,&tmplen,&var_val_type,&tmp,sizeof(int));
  if (tmp == 1 && action == COMMIT) {
    signal(SIGALRM,restart_doit);
    alarm(RESTARTSLEEP);
  } 
  return SNMP_ERR_NOERROR;
}

void
print_mib_oid(name,len)
  oid name[];
  int len;
{
  int i;
  printf("Mib:  ");
  for(i=0; i < len; i++) {
    printf(".%d",(int) name[i]);
  }
}

void
sprint_mib_oid(buf,name,len)
  char *buf;
  oid name[];
  int len;
{
  int i;
  for(i=0; i < len; i++) {
    sprintf(buf,".%d",(int) name[i]);
    while (*buf != 0)
      buf++;
  }
}

int checkmib(vp,name,length,exact,var_len,write_method,max)
    register struct variable *vp;
    register oid	*name;
    register int	*length;
    int			exact;
    int			*var_len;
    int			(**write_method)__P((int, u_char *, u_char, int, u_char *, oid *, int));
    int                 max;
{
  int i, rtest;
  int newname[100];

  for(i=0,rtest=0; i < (int) vp->namelen && i < (int)(*length) && !rtest; i++) {
    if (name[i] != vp->name[i]) {
      if (name[i] < vp->name[i]) 
        rtest = -1;
      else
        rtest = 1;
    }
  }
  if (rtest > 0 || (rtest == 0 && (int) vp->namelen+1 < (int) *length) ||
    (exact == -1 && rtest)) {
    if (var_len)
	*var_len = 0;
    return 0;
  }
/*  printf("%d/ck:  vp=%d  ln=%d lst=%d\n",exact,
         vp->namelen,*length,name[*length-1]); */
  if (((int) *length) <= (int) vp->namelen || rtest == -1) {
    memmove(newname, vp->name, (int)vp->namelen * sizeof (oid));
    newname[vp->namelen] = 1;
    *length = vp->namelen+1;
  }
  else {
    *length = vp->namelen+1;
    memmove(newname, name, (*length) * sizeof(oid));
    if (!exact)
      newname[*length-1] = name[*length-1] + 1;
    else
      newname[*length-1] = name[*length-1];
  }  
  if (max >= 0 && newname[*length-1] > max) {
    if(var_len)
      *var_len = 0;
    return 0;
  }
  memmove(name, newname, (*length) * sizeof(oid)); 
  if (write_method)
    *write_method = 0;
  if (var_len)
    *var_len = sizeof(long);   /* default */
  return(1);
}

#define MATCH_FAILED	1
#define MATCH_SUCCEEDED	0

int
header_generic(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;    /* IN - pointer to variable entry that points here */
    oid     *name;	    /* IN/OUT - input name requested, output name found */
    int     *length;	    /* IN/OUT - length of input and output oid's */
    int     exact;	    /* IN - TRUE if an exact match was requested. */
    int     *var_len;	    /* OUT - length of variable or 0 if function returned. */
    int     (**write_method) __P((int, u_char *,u_char, int, u_char *,oid*, int));
{
    oid newname[MAX_NAME_LEN];
    int result;
    char c_oid[MAX_NAME_LEN];

    if (snmp_get_do_debugging()) {
      sprint_objid (c_oid, name, *length);
      DEBUGP ("header_generic: %s exact=%d\n", c_oid, exact);
    }

    memcpy((char *)newname, (char *)vp->name, (int)vp->namelen * sizeof(oid));
    newname[vp->namelen] = 0;
    result = compare(name, *length, newname, (int)vp->namelen + 1);
    DEBUGP("  result: %d\n", result);
    if ((exact && (result != 0)) || (!exact && (result >= 0)))
        return(MATCH_FAILED);
    memcpy( (char *)name,(char *)newname, ((int)vp->namelen + 1) * sizeof(oid));
    *length = vp->namelen + 1;

    *write_method = 0;
    *var_len = sizeof(long);	/* default to 'long' results */
    return(MATCH_SUCCEEDED);
}

char *find_field(ptr,field)
     char *ptr;
     int field;
{
  int i;
  char *init=ptr;
  
  if (field == LASTFIELD) {
    /* skip to end */
    while (*ptr++);
    ptr = ptr - 2;
    /* rewind a field length */
    while (*ptr != 0 && isspace(*ptr) && init <= ptr) ptr--;
    while (*ptr != 0 && !isspace(*ptr) && init <= ptr) ptr--;
    if (isspace(*ptr)) ptr++;  /* past space */
    if (ptr < init) ptr = init;
    if (!isspace(*ptr) && *ptr != 0) return(ptr);
  } else {
    if ((ptr = skip_white(ptr)) == NULL) return(NULL);
    for (i=1; *ptr != 0 && i != field; i++) 
      {
        if ((ptr = skip_not_white(ptr)) == NULL) return (NULL);
        if ((ptr = skip_white(ptr)) == NULL) return (NULL);
      }
    if (*ptr != 0 && i == field) return(ptr);
    return (NULL);
  }
  return(NULL);
}

int parse_miboid(buf,oidout)
char *buf;
oid *oidout;
{
  int i;
  
  if (!buf)
    return 0;
  if (*buf == '.') buf++;
  for(i=0;isdigit(*buf);i++) {
    oidout[i] = atoi(buf);
    while(isdigit(*buf++));
    if (*buf == '.') buf++;
  }
  oidout[i] = -1;
  return i;
}

void
string_append_int (s, val)
char *s;
int val;
{
    char textVal[16];

    if (val < 10) {
	*s++ = '0' + val;
	*s = '\0';
	return;
    }
    sprintf (textVal, "%d", val);
    strcpy(s, textVal);
    return;
}

int
calculate_time_diff(t1, t2)
  struct timeval t1, t2;
{
  struct timeval tmp, diff;
  memcpy(&tmp, &t1, sizeof(struct timeval));
  tmp.tv_sec--;
  tmp.tv_usec += 1000000L;
  diff.tv_sec = tmp.tv_sec - t2.tv_sec;
  diff.tv_usec = tmp.tv_usec - t2.tv_usec;
  if (diff.tv_usec > 1000000L){
    diff.tv_usec -= 1000000L;
    diff.tv_sec++;
  }
  return ((diff.tv_sec * 100) + (diff.tv_usec / 10000));
}
