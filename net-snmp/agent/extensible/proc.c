#include "mibincl.h"
#include "mibdefs.h"
#include "../../config.h"

int fixProcError();

struct myproc *get_proc_instance();
struct myproc *procwatch;
static struct extensible fixproc;
int numprocs=0;

unsigned char *var_extensible_proc(vp, name, length, exact, var_len, write_method)
    register struct variable *vp;
/* IN - pointer to variable entry that points here */
    register oid	*name;
/* IN/OUT - input name requested, output name found */
    register int	*length;
/* IN/OUT - length of input and output oid's */
    int			exact;
/* IN - TRUE if an exact match was requested. */
    int			*var_len;
/* OUT - length of variable or 0 if function returned. */
    int			(**write_method)();
/* OUT - pointer to function to set variable, otherwise 0 */
{

  oid newname[30];
  int count, result,i, rtest=0;
  register int interface;
  struct myproc *proc;
  long long_ret;
  char errmsg[300];


  if (!checkmib(vp,name,length,exact,var_len,write_method,newname,numprocs))
    return(NULL);
  
  if (proc = get_proc_instance(procwatch,newname[8])) {
    switch (vp->magic) {
      case MIBINDEX:
        long_ret = newname[8];
        return((u_char *) (&long_ret));
      case ERRORNAME:   /* process name to check for */
        *var_len = strlen(proc->name);
        return((u_char *) (proc->name));
      case PROCMIN:
        long_ret = proc->min;
        return((u_char *) (&long_ret));
      case PROCMAX:
        long_ret = proc->max;
        return ((u_char *) (&long_ret));
      case PROCCOUNT:
        long_ret = sh_count_procs(proc->name);
        return ((u_char *) (& long_ret));
      case ERRORFLAG:
        long_ret = sh_count_procs(proc->name);
        if ((proc->min && long_ret < proc->min) || 
            (proc->max && long_ret > proc->max) ||
            (proc->min == 0 && proc->max == 0 && long_ret < 1)) {
          long_ret = 1;
        }
        else {
          long_ret = 0;
        }
        return ((u_char *) (& long_ret));
      case ERRORMSG:
        long_ret = sh_count_procs(proc->name);
        if (proc->min && long_ret < proc->min) {
          sprintf(errmsg,"Too few %s running (# = %d)",
                  proc->name, long_ret);
        }
        else if (proc->max && long_ret > proc->max) {
          sprintf(errmsg,"Too many %s running (# = %d)",
                  proc->name, long_ret);
        }
        else if (proc->min == 0 && proc->max == 0 && long_ret < 1) {
          sprintf(errmsg,"No %s process running.", proc->name);
        }
        else {
          errmsg[0] = NULL;
        }
        *var_len = strlen(errmsg);
        return((u_char *) errmsg);
      case ERRORFIX:
        *write_method = fixProcError;
        long_return = fixproc.result;
        return ((u_char *) &long_return);
    }
    return NULL;
  }
  return NULL;
}

fixProcError(action, var_val, var_val_type, var_val_len, statP, name, name_len)
   int      action;
   u_char   *var_val;
   u_char   var_val_type;
   int      var_val_len;
   u_char   *statP;
   oid      *name;
   int      name_len;
{
  
  struct myproc *proc;
  int tmp=0, tmplen=1000;

  if (proc = get_proc_instance(procwatch,name[8])) {
    if (var_val_type != INTEGER) {
      printf("Wrong type != int\n");
      return SNMP_ERR_WRONGTYPE;
    }
    asn_parse_int(var_val,&tmplen,&var_val_type,&tmp,sizeof(int));
    if (tmp == 1 && action == COMMIT) {
      sprintf(fixproc.command,PROCFIXCMD,proc->name);
      exec_command(&fixproc);
    } 
    return SNMP_ERR_NOERROR;
  }
  return SNMP_ERR_WRONGTYPE;
}

