#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#define FFLAG (O_WRONLY | O_CREAT | O_TRUNC)
#define FMODE (S_IRUSR | S_IWUSR)
#define PROMPT_STRING "ush4>>"
#define QUIT_STRING "q"
#define BLANK_STRING " "

int makeargv(const char *s, const char *delimiters, char ***argvp) {
   int error;
   int i;
   int numtokens;
   const char *snew;
   char *t;

   if ((s == NULL) || (delimiters == NULL) || (argvp == NULL)) {
      errno = EINVAL;
      return -1;
   }
   *argvp = NULL;                           
   snew = s + strspn(s, delimiters);         /* snew is real start of string */
   if ((t = malloc(strlen(snew) + 1)) == NULL) 
      return -1; 
   strcpy(t, snew);               
   numtokens = 0;
   if (strtok(t, delimiters) != NULL)     /* count the number of tokens in s */
      for (numtokens = 1; strtok(NULL, delimiters) != NULL; numtokens++) ; 

                             /* create argument array for ptrs to the tokens */
   if ((*argvp = malloc((numtokens + 1)*sizeof(char *))) == NULL) {
      error = errno;
      free(t);
      errno = error;
      return -1; 
   } 
                        /* insert pointers to tokens into the argument array */
   if (numtokens == 0) 
      free(t);
   else {
      strcpy(t, snew);
      **argvp = strtok(t, delimiters);
      for (i = 1; i < numtokens; i++)
          *((*argvp) + i) = strtok(NULL, delimiters);
    } 
    *((*argvp) + numtokens) = NULL;             /* put in final NULL pointer */
    return numtokens;
}

//redirect standard input if '<'
int parseandredirectin(char *cmd)
{
  int error;
  int infd;
  char *infile;

  if ((infile = strchr(cmd, '<')) == NULL)
    {
      return 0;
    }
  
  // take everything after '<' out of cmd
  *infile = 0;
  infile = strtok(infile + 1, " \t");
  if (infile == NULL)
    {
      return 0;
    }
  if ((infd = open(infile, O_RDONLY)) == -1)
    {
      return -1;
    }
  if (dup2(infd, STDIN_FILENO) == -1)
    {
      //make sure errno is correct
      error = errno;
      close(infd);
      errno = error;
      return -1;
    }
  return close(infd);
}

//redirect standard output if '>'
int parseandredirectout(char *cmd)
{
  int error;
  int outfd;
  char *outfile;

  if ((outfile = strchr(cmd, '>')) == NULL)
    {
      return 0;
    }
  //take everthing after '>' out of cmd
  *outfile = 0;
  outfile = strtok(outfile + 1, " \t");

  if (outfile == NULL)
    {
      return 0;
    }
  if ((outfd = open(outfile, FFLAG, FMODE)) == -1)
    {
      return -1;
    }
  if (dup2(outfd, STDOUT_FILENO) == -1)
    {
      //make sure errno is correct
      error = errno;
      close(outfd);
      errno = error;
      return -1;
    }
  return close(outfd);
}

void executeredirect(char *s, int in, int out)
{
  char **chargv;
  char *pin;
  char *pout;

  if (in && ((pin = strchr(s, '<')) != NULL) && out && ((pout = strchr(s, '>')) != NULL) && (pin > pout)  )
    {
      if (parseandredirectin(s) == -1)
	{
	  //redirect input is last on line
	  perror("Failed to redirect input");
	  return;
	}
      in = 0;
    }
  if (out && (parseandredirectout(s) == -1))
    {
      perror("Failed to redirect output");
    }
  else if (in && (parseandredirectin(s) == -1))
    {
      perror("Failed to redirect input");
    }
  else if (makeargv(s, " \t", &chargv) <= 0)
    {
      fprintf(stderr, "Failed to parse command line\n");
    }
  else
    {
      execvp(chargv[0], chargv);
      perror("Failed to execute command");
    }
  exit(1);
}

static void perror_exit(char *s)
{
  perror(s);
  exit(1);
}

void executecmd(char *cmds) 
{ 

  int child;
  int count;
  int fds[2];
  int i;
  char **pipelist;
  
  count = makeargv(cmds, "|", &pipelist);
  if (count <= 0)
    {
      fprintf(stderr, "Failed to find any commands\n");
      exit(1);
    }


  
  for (i = 0; i< count - 1; i++)   //handle all but last one
    {
      if (pipe(fds) == -1)
	{
	  perror_exit("Failed to create pipes");
	}
      else if ((child = fork()) == -1)
	{
	  perror_exit("Failed to create process to run command");
	}
      else if (child)    //parent code
	{
	  if (dup2(fds[1], STDOUT_FILENO) == -1)
	    {
	      perror_exit("Failed to connect pipeline");
	    }
	  if (close(fds[0]) || close(fds[1]))
	    {
	      perror_exit("Failed to close needed files");
	    }
	  executeredirect(pipelist[i], i == 0, 0);
	  exit(1);
	}
      
      if (dup2(fds[0], STDIN_FILENO) == -1)      //child code
	{   
	  perror_exit("Failed to connect last component");
	}
      if (close(fds[0]) || close(fds[1]))
	{
	  perror_exit("Failed to do final close");
	}
    }
  executeredirect(pipelist[i], i == 0, 1);      //handle the last one
  exit(1);
}

int signalsetup(struct sigaction *def, sigset_t *mask, void (*handler) (int))
{
  struct sigaction catch;

  catch.sa_handler = handler; //set up signal structures
  def->sa_handler = SIG_DFL;
  catch.sa_flags = 0;
  def->sa_flags = 0;

  if ((sigemptyset(&(def->sa_mask)) == -1) ||
      (sigemptyset(&(catch.sa_mask)) == -1) ||
      (sigaddset(&(catch.sa_mask), SIGINT) == -1) ||
      (sigaddset(&(catch.sa_mask), SIGQUIT) == -1) ||
      (sigaction(SIGINT, &catch, NULL) == -1) ||
      (sigaction(SIGQUIT, &catch, NULL) == -1) ||
      (sigemptyset(mask) == -1) ||
      (sigaddset(mask, SIGINT) == -1) ||
      (sigaddset(mask, SIGQUIT) == -1))
    {
      return -1;
    }
  return 0;
}

void changedirhandler(char *commands)
{
  char **directoryarg;
  
  if (makeargv(commands, " \t", &directoryarg) <= 0)
    {
      fprintf(stderr, "Failed to parse command line\n");
      exit(1);
    }

  if (chdir(directoryarg[1]) == -1)
    {
      perror("Failed to change current working directory");
    }
}

void exporthandler(char *commands)
{
  char **exportarg;

  int count;

  int i;
  
  if ((count = makeargv(commands, " ", &exportarg)) <= 0)
    {
      fprintf(stderr, "Failed to parse command line\n");
      exit(1);
    }

  for (i = 1; i < count; i++)
    {
      if (putenv(exportarg[i]) != 0)
	{
	  perror("Failed to change environment variable or add new environment variable");
	  
	}
    }
}

static sigjmp_buf jumptoprompt;
static volatile sig_atomic_t okaytojump = 0;

//ARGSUSEDD
static void jumphd(int signalnum)
{
  if (!okaytojump)
    {
      return;
    }
  okaytojump = 0;
  siglongjmp(jumptoprompt, 1);
}


int main (void)
{
  sigset_t blockmask;
  pid_t childpid;
  char leftbracket = '[';
  char cwd[256];
  char tempcwd[256];
  char rightbracket = ']';
  char arrow = '>';

  struct sigaction defhandler;
  char inbuf[MAX_CANON];
  int len;

  if (signalsetup(&defhandler, &blockmask, jumphd) == -1)
    {
      perror("Failed to set up shell signal handling");
      return 1;
    }

  for ( ; ; )
    {

      memset(cwd, 0, strlen(cwd));
      strncat(cwd, &leftbracket, 1);
      strncat(cwd, getcwd(tempcwd, sizeof(tempcwd)), sizeof(tempcwd));
      strncat(cwd, &rightbracket, 1);
      strncat(cwd, &arrow, 1);

      if ((sigsetjmp(jumptoprompt, 1)) && (fputs("\n", stdout) == EOF)  )
	{
	  continue;
	}
      wait(NULL);
      okaytojump = 1;
      if (fputs(cwd, stdout) == EOF)
	{
	  continue;
	}
      if (fgets(inbuf, MAX_CANON, stdin) == NULL)
	{
	  continue;
	}
      len = strlen(inbuf);
      if (inbuf[len - 1] == '\n')
	{
	  inbuf[len - 1] = 0;
	}
      if (strcmp(inbuf, QUIT_STRING) == 0)
	{
	  break;
	}
      if (sigprocmask(SIG_BLOCK, &blockmask, NULL) == -1)
	{
	  perror("Failed to block signals");
	}
      if (strncmp (inbuf, "cdxx", 2) == 0)
      	{
	  changedirhandler(inbuf);
      	}
      else if (strncmp (inbuf, "export", 6) == 0)
	{
	  exporthandler(inbuf);
	}
      else if ((childpid = fork()) == -1)
	{
	  perror("Failed to fork");
	}
      else if (childpid == 0)
	{
	  if ((sigaction(SIGINT, &defhandler, NULL) == -1) ||
	      (sigaction(SIGQUIT, &defhandler, NULL) == -1) || 
	      (sigprocmask(SIG_UNBLOCK, &blockmask, NULL) == -1))
	    {
	      perror("Failed to set signal handling for command ");
	      return 1;
	    }
	  executecmd(inbuf);
	  return -1;
	}
      if (sigprocmask(SIG_UNBLOCK, &blockmask, NULL) == -1)
	{
	  perror("Failed to unblock signals");
	}
    }
  return 0;
}
