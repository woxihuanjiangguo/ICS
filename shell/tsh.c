/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 * Name: Li Zhenxin  ID: 19302010007
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>  
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/*My utils*/
void array_offset(char **array,int length,int *i);

/*To check every ret value of functions called, wrap them*/
pid_t wrapped_fork(void);
void wrapped_kill(pid_t pid,int sig);
void wrapped_setpgid(pid_t pid,pid_t pgid);
void wrapped_sigemptyset(sigset_t *set);
void wrapped_sigaddset(sigset_t *set,int signum);
void wrapped_sigprocmask(int how,const sigset_t *set,sigset_t *oldset);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv, int *argc_local); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS];
    int argc_local;
    int bg;
    pid_t pid;
    sigset_t mask;
    int flagIn = 0,flagOut = 0;
    int countIn = 0,countOut = 0;
    int old_stdin = dup(STDIN_FILENO);
    int old_stdout = dup(STDOUT_FILENO);
    int fd1,fd2;

    bg = parseline(cmdline,argv,&argc_local);
    if(argv[0] == NULL){
        return;
    }
    //set I/O redirection here
    //scan first to validate
    for(int i = 0;i < argc_local;i++){
        if(argv[i] == NULL){
            break;
        }
        if(!strcmp(argv[i],"<")){
            /* error part
            * 1. first or second arg is null
            * 2. multiple <
            */
            countIn ++;
            if(i == 0){
                printf("Error: Ambiguous I/O redirection\n");
                return;
            }
            if(argv[i+1] == NULL){
                printf("Error: Ambiguous I/O redirection\n");
                return;
            }
            if(countIn > 1){
                printf("Error: Ambiguous I/O redirection\n");
                return;
            }
        }else if(!strcmp(argv[i],">") || !strcmp(argv[i],">>")){
            /* error part
            * 1. first or second arg is null
            * 2. multiple > or >>
            */
            countOut ++;
            if(i == 0){
                printf("Error: Ambiguous I/O redirection\n");
                return;
            }
            if(argv[i+1] == NULL){
                printf("Error: Ambiguous I/O redirection\n");
                return;
            }
            if(countOut > 1){
                printf("Error: Ambiguous I/O redirection\n");
                return;
            }
        }
    }
    //deal with it
    for(int i = 0;i < argc_local;i++){
        if(argv[i] == NULL){
            break;
        }
        if(!strcmp(argv[i],"<")){
            // < in
            if((fd1 = open(argv[i+1],O_RDONLY,0)) < 0){
                dup2(old_stdin,STDIN_FILENO);
                dup2(old_stdout,STDOUT_FILENO);
                printf("Error: %s no such file or directory\n",argv[i+1]);
                return;
            }
            dup2(fd1,STDIN_FILENO);
            flagIn = 1;
            //delete the sequence: "< file" in argv
            array_offset(argv,argc_local,&i);
        }else if(!strcmp(argv[i],">")){
            // > out
            fd2 = open(argv[i+1],O_WRONLY|O_TRUNC|O_CREAT,0x0080|0x0100);
            dup2(fd2,STDOUT_FILENO);
            flagOut = 1;
            array_offset(argv,argc_local,&i);
        }else if(!strcmp(argv[i],">>")){
            // >> append
            fd2 = open(argv[i+1],O_WRONLY|O_APPEND|O_CREAT,0x0080|0x0100);
            dup2(fd2,STDOUT_FILENO);
            flagOut = 1;
            array_offset(argv,argc_local,&i);
        }
    }

    //execute a builtin command, if not, continue
    if(!builtin_cmd(argv)){
        //init set mask
        wrapped_sigemptyset(&mask);
		wrapped_sigaddset(&mask, SIGCHLD);
		wrapped_sigprocmask(SIG_BLOCK, &mask, NULL);
        //start child process
        if(!(pid = wrapped_fork())){
            wrapped_setpgid(0,0);
            wrapped_sigprocmask(SIG_UNBLOCK,&mask,NULL);
            if(execve(argv[0],argv,environ) < 0){
                printf("%s: Command not found\n",argv[0]);
                exit(0);
            }
        }
        //parent process
        if(!bg){
            //add foreground job & unblock SIGCHLD
            //check the ret value of addjob
            if(addjob(jobs,pid,FG,cmdline)){
                wrapped_sigprocmask(SIG_UNBLOCK,&mask,NULL);
                //just wait, no msg to print
                waitfg(pid);
            }else{
                wrapped_kill(-pid,SIGINT);
            }
        }else{
            //background job
            //check the ret value of addjob
            if(addjob(jobs,pid,BG,cmdline)){
                wrapped_sigprocmask(SIG_UNBLOCK,&mask,NULL);
                //msg here
                printf("[%d] (%d) %s",pid2jid(pid),pid,cmdline);
            }else{
                wrapped_kill(-pid,SIGINT);
            }
        }
    }
    //recover the original stdin and stdout descriptors
    if(flagIn){
        close(fd1);
        dup2(old_stdin,STDIN_FILENO);
    }
    if(flagOut){
        close(fd2);
        dup2(old_stdout,STDOUT_FILENO);
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv, int *argc_local) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	    buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0){
        *argc_local = argc;
        return 1;
    }  /* ignore blank line */
	

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	    argv[--argc] = NULL;
    }
    *argc_local = argc;
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    if(!strcmp(argv[0],"quit")){
        exit(0);
    }
    if(!strcmp(argv[0],"jobs")){
        listjobs(jobs);
        return 1;
    }
    if(!strcmp(argv[0],"bg") || !strcmp(argv[0],"fg")){
        do_bgfg(argv);
        return 1;
    }
    if(!strcmp(argv[0],"&")){
        //ignore simple '&'
        return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{   
    /*check if argv[1] is valid
    *Invalid cases:
    *empty string 
    *jid and pid not present in the jobs
    *then, get the jobPtr
    */
    
    char *arg2 = argv[1];
    int tempPid,tempJid;
    char *tempCmdline;
    if(arg2 == NULL){
        printf("%s command requires PID or %%jobid argument\n",argv[0]);
        return;
    }
    struct job_t *jobPtr = NULL;
    if((arg2[0] == '%') 
    && strlen(&arg2[1]) == strspn(&arg2[1],"0123456789")){
        //jid, pure digits after %
        int jid = atoi(&arg2[1]);
        if(!(jobPtr = getjobjid(jobs,jid))){
            printf("%%%d : No such job\n",jid);
            return;
        }
    }else if(strlen(arg2) == strspn(arg2,"0123456789")){
        //pid
        pid_t pid = atoi(arg2);
        if(!(jobPtr = getjobpid(jobs,pid))){
            printf("(%d): No such process\n",pid);
            return;
        }
    }else{
        //error
        printf("%s: argument must be a PID or %%jobid\n",argv[0]);
        return;
    }
    //double check, in case the array "jobs" has data loss
    if(jobPtr == NULL){
        return;
    }
    tempPid = jobPtr->pid;
    tempJid = jobPtr->jid;
    tempCmdline = jobPtr->cmdline;

    //bg part
    if(!strcmp(argv[0],"bg")){
        jobPtr->state = BG;
        wrapped_kill(tempPid,SIGCONT);
        printf("[%d] (%d) %s",tempJid,tempPid,tempCmdline);
        return;
    }
    //fg part
    if(!strcmp(argv[0],"fg")){
        jobPtr->state = FG;
        wrapped_kill(-tempPid,SIGCONT);
        waitfg(tempPid);
        return;
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    while(fgpid(jobs) == pid){
        sleep(1);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    int status;
    pid_t pid;
    int jid;

    while(((pid = waitpid(-1,&status,WUNTRACED | WNOHANG)) > 0)){
        jid = pid2jid(pid);
        if(WIFEXITED(status)){
            //exit normally
            //wrap func deletejob to check ret val
            if(!deletejob(jobs,pid)){
                printf("deletejob(%d): failure\n",pid);
            }
        }else if(WIFSIGNALED(status)){
            //ctrl + c
            if(!deletejob(jobs,pid)){
                printf("deletejob(%d): failure\n",pid);
            }else{
                //delete success, print msg
                printf("Job [%d] (%d) terminated by signal %d\n", jid, (int) pid, WTERMSIG(status));
            }
        }else if(WIFSTOPPED(status)){
            //ctrl + z
            if(!(getjobpid(jobs,pid))){
                printf("getjobpid(%d): failure\n",pid);
            }else{
                getjobpid(jobs,pid)->state = ST;
                //state changed success, print msg
                printf("Job [%d] (%d) stopped by signal %d\n", jid, (int) pid, WSTOPSIG(status));
            }
        }
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    pid_t pid = fgpid(jobs);
    if(pid != 0){
        wrapped_kill(-pid,sig);
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t pid = fgpid(jobs);
    if(pid != 0){
        wrapped_kill(-pid,sig);
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

/*wrapped functions definition*/
pid_t wrapped_fork(void){
    pid_t pid;
    if((pid = fork()) < 0){
        unix_error("fork function error");
    }
    return pid;
}

void wrapped_kill(pid_t pid,int sig){
    if(kill(pid,sig) < 0){
        unix_error("kill function error");
    }
}

void wrapped_setpgid(pid_t pid,pid_t pgid){
    if(setpgid(pid,pgid) < 0){
        unix_error("setpgid function error");
    }
}

void wrapped_sigemptyset(sigset_t *set){
    if(sigemptyset(set) < 0){
        unix_error("sigemptyset function error");
    }
}

void wrapped_sigaddset(sigset_t *set,int signum){
    if(sigaddset(set,signum) < 0){
        unix_error("sigaddset function error");
    }
}

void wrapped_sigprocmask(int how,const sigset_t *set,sigset_t *oldset){
    if(sigprocmask(how,set,oldset) < 0){
        unix_error("sigprocmask function error");
    }
}

void array_offset(char **array,int length,int *i){
    if(length == (*i + 2)){
        array[length-2] = array[length-1] = NULL;
        return;
    }
    for(int j = *i;j < length - 2;j++){
        array[j] = array[j + 2];
    }
    array[length-2] = NULL;
    array[length-1] = NULL;
    *i -= 1;
}