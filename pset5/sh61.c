#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define TOKEN_CONTROL       0  // token is a control operator,
                               // and terminates the current command
#define TOKEN_NORMAL        1  // token is normal command word
#define TOKEN_REDIRECTION   2  // token is a redirection operator

#define PWDLEN				80
// parse_shell_token(str, type, token)
//    Parse the next token from the shell command `str`. Stores the type of
//    the token in `*type`; this is one of the TOKEN_ constants. Stores the
//    token itself in `*token`. The token is a newly-allocated string that
//    should be freed eventually with `free`. Returns the pointer within
//    `str` to the next token.
//
//    Returns NULL and sets `*token = NULL` at the end of string.

const char* parse_shell_token(const char* str, int* type, char** token);


// struct command
//    Data structure describing a command. Add your own stuff.

typedef struct command command;
struct command {
    int argc;                   // number of arguments
    int needcondition;
    int background;
    int piping;
    int redirectstdin;
    int redirectstdout;
    int redirectstderr;
    char* stdinfilename;
    char* stdoutfilename;
    char* stderrfilename;
    int* type;
    char** argv;                // arguments, terminated by NULL
};


// command_alloc()
//    Allocate and return a new command structure.

static command* command_alloc(void) {
    command* c = (command*) malloc(sizeof(command));
    c->argc = 0;
    c->needcondition = -1;
    c->background = 0;
    c->piping = 0;
    c->redirectstdin = 0;
    c->redirectstdout = 0;
    c->redirectstderr = 0;
    c->stdinfilename = NULL;
    c->stdoutfilename = NULL;
    c->stderrfilename = NULL;
    c->type = NULL;
    c->argv = NULL;
    return c;
}


// command_free(c)
//    Free command structure `c`, including all its words.

static void command_free(command* c) {
    for (int i = 0; i != c->argc; ++i)
        free(c->argv[i]);
    free(c->argv);
    free(c->type);
    free(c);
}


// command_append_arg(c, word)
//    Add `word` as an argument to command `c`. This increments `c->argc`
//    and augments `c->argv`.

static void command_append_arg(command* c, int type, char* word) {
	c->type = (int*) realloc(c->type, sizeof(int) * (c->argc + 1));
    c->type[c->argc] = type;
    c->argv = (char**) realloc(c->argv, sizeof(char*) * (c->argc + 2));
    c->argv[c->argc] = word;
    c->argv[c->argc + 1] = NULL;
    ++c->argc;
}


// COMMAND PARSING

typedef struct buildstring {
    char* s;
    int length;
    int capacity;
} buildstring;

// buildstring_append(bstr, ch)
//    Add `ch` to the end of the dynamically-allocated string `bstr->s`.

void buildstring_append(buildstring* bstr, int ch) {
    if (bstr->length == bstr->capacity) {
        int new_capacity = bstr->capacity ? bstr->capacity * 2 : 32;
        bstr->s = (char*) realloc(bstr->s, new_capacity);
        bstr->capacity = new_capacity;
    }
    bstr->s[bstr->length] = ch;
    ++bstr->length;
}

// isshellspecial(ch)
//    Test if `ch` is a command that's special to the shell (that ends
//    a command word).

static inline int isshellspecial(int ch) {
    return ch == '<' || ch == '>' || ch == '&' || ch == '|' || ch == ';'
        || ch == '(' || ch == ')' || ch == '#';
}

// parse_shell_token(str, type, token)

const char* parse_shell_token(const char* str, int* type, char** token) {
    buildstring buildtoken = { NULL, 0, 0 };

    // skip spaces; return NULL and token ";" at end of line
    while (str && isspace((unsigned char) *str))
        ++str;
    if (!str || !*str || *str == '#') {
        *type = TOKEN_CONTROL;
        *token = NULL;
        return NULL;
    }

    // check for a redirection or special token
    for (; isdigit((unsigned char) *str); ++str)
        buildstring_append(&buildtoken, *str);
    if (*str == '<' || *str == '>') {
        *type = TOKEN_REDIRECTION;
        buildstring_append(&buildtoken, *str);
        if (*str == '>' && str[1] == '>') {
            buildstring_append(&buildtoken, *str);
            str += 2;
        } else
            ++str;
    } else if (buildtoken.length == 0
               && (*str == '&' || *str == '|')
               && str[1] == *str) {
        *type = TOKEN_CONTROL;
        buildstring_append(&buildtoken, *str);
        buildstring_append(&buildtoken, str[1]);
        str += 2;
    } else if (buildtoken.length == 0
               && isshellspecial((unsigned char) *str)) {
        *type = TOKEN_CONTROL;
        buildstring_append(&buildtoken, *str);
        ++str;
    } else {
        // it's a normal token
        *type = TOKEN_NORMAL;
        int quoted = 0;
        // Read characters up to the end of the token.
        while ((*str && quoted)
               || (*str && !isspace((unsigned char) *str)
                   && !isshellspecial((unsigned char) *str))) {
            if (*str == '\"')
                quoted = !quoted;
            else if (*str == '\\' && str[1] != '\0') {
                buildstring_append(&buildtoken, str[1]);
                ++str;
            } else
                buildstring_append(&buildtoken, *str);
            ++str;
        }
    }

    // store new token and return the location of the next token
    buildstring_append(&buildtoken, '\0'); // terminating NUL character
    *token = buildtoken.s;
    return str;
}


// COMMAND EVALUATION

// set_foreground(p)
//    Tell the operating system that `p` is the current foreground process.
int set_foreground(pid_t p);

void eval_command(command* c, int* condition, char* pwd) {
	int status;
	if(c->piping) {
		int numpipes = 0;
		char** child_argv = (char**) malloc((c->argc + 1) * sizeof(char**));
		child_argv[c->argc] = NULL;
		int* child_argvbegin = (int*) malloc(sizeof(int*));
		child_argvbegin[0] = 0;
		for(int i = 0; i != c->argc; ++i) {
			if(c->argv[i][0] == '|') {
				child_argv[i] = NULL;
				++numpipes;
				child_argvbegin = (int*) realloc(child_argvbegin, (numpipes + 1) * sizeof(int*));
				child_argvbegin[numpipes] = i + 1;
			} else {
				child_argv[i] = c->argv[i];
			}
		}
		
		typedef int pipefd[2];
		pipefd* pipes = (pipefd*) malloc(numpipes * sizeof(pipefd));
		pid_t* pids = (pid_t*) malloc((numpipes + 1) * sizeof(pid_t*));
		for(int i = 0; i <= numpipes; ++i) {
			if(i != numpipes) {
				pipe(pipes[i]);
			}
  			pids[i] = fork();
  			if(pids[i] == 0 && i == 0) {
  				close(pipes[i][0]);
  				dup2(pipes[i][1], STDOUT_FILENO);
  				close(pipes[i][1]);
  				if(c->stdinfilename) {
	    			int fin = open(c->stdinfilename, O_RDONLY);
	    			if(fin < 0) {
	    				perror("No such file or directory\n");
	    			}
	    			dup2(fin, STDIN_FILENO);
	    			close(fin);
	    		}
	    		if(c->stderrfilename) {
	    			int ferr = open(c->stderrfilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
	    			if(ferr < 0) {
	    				perror("No such file or directory\n");
	    			}
	    			dup2(ferr, STDERR_FILENO);
	    			close(ferr);
	    		}
  				execvp(child_argv[child_argvbegin[i]], &child_argv[child_argvbegin[i]]);
  				exit(1);
  			} else if(pids[i] == 0 && i == numpipes) {
  				close(pipes[i-1][1]);
  				dup2(pipes[i-1][0], STDIN_FILENO);
  				close(pipes[i-1][0]);
	    		if(c->stdoutfilename) {
	    			int fout = open(c->stdoutfilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
	    			if(fout < 0) {
	    				perror("No such file or directory\n");
	    			}
	    			dup2(fout, STDOUT_FILENO);
	    			close(fout);
	    		}
	    		if(c->stderrfilename) {
	    			int ferr = open(c->stderrfilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
	    			if(ferr < 0) {
	    				perror("No such file or directory\n");
	    			}
	    			dup2(ferr, STDERR_FILENO);
	    			close(ferr);
	    		}
  				execvp(child_argv[child_argvbegin[i]], &child_argv[child_argvbegin[i]]);
  				exit(1);
  			} else if(pids[i] == 0) {
  				close(pipes[i-1][1]);
  				dup2(pipes[i-1][0], STDIN_FILENO);
  				close(pipes[i-1][0]);
  				close(pipes[i][0]);
  				dup2(pipes[i][1], STDOUT_FILENO);
  				close(pipes[i][1]);
	    		if(c->stderrfilename) {
	    			int ferr = open(c->stderrfilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
	    			if(ferr < 0) {
	    				perror("No such file or directory\n");
	    			}
	    			dup2(ferr, STDERR_FILENO);
	    			close(ferr);
	    		}
  				execvp(child_argv[child_argvbegin[i]], &child_argv[child_argvbegin[i]]);
  				exit(1);
  			} else if(i != numpipes) {
//  			close(pipes[i][0]);		OMG THIS STUPID LINE WASTED 20 HOURS OF MY LOST-FOREVER LIFETIME
  				close(pipes[i][1]);
  				if(!c->background) {
    				waitpid(pids[i], &status, 0);
    				if (WIFEXITED(status)) {
    					*condition = WEXITSTATUS(status);
    				}
    			}
  			} else {
  				if(!c->background) {
    				waitpid(pids[i], &status, 0);
    				if (WIFEXITED(status)) {
    					*condition = WEXITSTATUS(status);
    				}
    			}
  			}
		}
		free(child_argv);
		free(child_argvbegin);
		free(pipes);
		free(pids);
	} else {
    	pid_t pid = fork();
    	if (pid) {
    		if(!strcmp(c->argv[0], "cd")) {
	    		if(!chdir(c->argv[1])) {
	    			strcpy(pwd, getcwd(pwd, PWDLEN));
	    		}
	    	}
    		if(!c->background) {
    			waitpid(pid, &status, 0);
    			if (WIFEXITED(status)) {
    				*condition = WEXITSTATUS(status);
    			}
    		} else {
    			;
    		}
    	}
    	else {
	    	if(c->stdinfilename) {
	    		int fin = open(c->stdinfilename, O_RDONLY);
	    		if(fin < 0) {
	    			perror("No such file or directory\n");
	    		}
	    		dup2(fin, STDIN_FILENO);
	    		close(fin);
	    	}
	    	if(c->stdoutfilename) {
	    		int fout = open(c->stdoutfilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
	    		if(fout < 0) {
	    			perror("No such file or directory\n");
	    		}
	    		dup2(fout, STDOUT_FILENO);
	    		close(fout);
	    	}
	    	if(c->stderrfilename) {
	    		int ferr = open(c->stderrfilename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
	    		if(ferr < 0) {
	    			perror("No such file or directory\n");
	    		}
	    		dup2(ferr, STDERR_FILENO);
	    		close(ferr);
	    	}
	    	if(!strcmp(c->argv[0], "cd")) {
	    		if(!chdir(c->argv[1])) {
	    			exit(0);
	    		} else {
	    			exit(1);
	    		}
	    	} else {
	    		execvp(c->argv[0], &(c->argv[0]));
	    		exit(1);
	    	}
    	}
    }
}


void eval_command_line(const char* s) {
    int type;
    char* token;
    command** commandlist = (command**) malloc(sizeof(command*));
    int listcontent = 1;
    // Your code here!

    // build the command
    command* c = command_alloc();
    commandlist[0] = c;
    while ((s = parse_shell_token(s, &type, &token)) != NULL) {
    		if(type == TOKEN_CONTROL) {
    			if(token[0] == ';') {
    				c = command_alloc();
    				commandlist = (command**) realloc(commandlist, (listcontent + 1)*sizeof(command*));
    				commandlist[listcontent] = c;
    				++listcontent;
    			} else if(token[0] == '&' && !token[1]) {
        			c->background = 1;
    				c = command_alloc();
    				commandlist = (command**) realloc(commandlist, (listcontent + 1)*sizeof(command*));
    				commandlist[listcontent] = c;
    				++listcontent;
    			} else if(token[0] == '&' && token[1] == '&') {
    				c = command_alloc();
    				c->needcondition = 0;
    				commandlist = (command**) realloc(commandlist, (listcontent + 1)*sizeof(command*));
    				commandlist[listcontent] = c;
    				++listcontent;
    			} else if(token[0] == '|' && !token[1]) {
    				c->piping = 1;
    				command_append_arg(c, type, token);
    			} else if(token[0] == '|' && token[1] == '|') {
    				c = command_alloc();
    				c->needcondition = 1;
    				commandlist = (command**) realloc(commandlist, (listcontent + 1)*sizeof(command*));
    				commandlist[listcontent] = c;
    				++listcontent;
    			}
        	} else if(type == TOKEN_REDIRECTION) {
        		if(token[0] == '<' || token[0] == '0') {
        			c->redirectstdin = 1;
        		} else if(token[0] == '>' || token[0] == '1') {
        			c->redirectstdout = 1;
        		} else if(token[0] == '2' && token[1] == '>') {
        			c->redirectstderr = 1;
        		}
        	} else {
        		if(c->redirectstdin) {
        			c->stdinfilename = token;
        			c->redirectstdin = 0;
        		} else if(c->redirectstdout) {
        			c->stdoutfilename = token;
        			c->redirectstdout = 0;
        		} else if(c->redirectstderr) {
        			c->stderrfilename = token;
        			c->redirectstderr = 0;
        		} else {
        			command_append_arg(c, type, token);
        		}
        	}
    }

    // execute the command
    int condition = -1;
    char* pwd = (char*) malloc(PWDLEN * sizeof(char));
    strcpy(pwd, getcwd(pwd, PWDLEN));
    for(int i = 0; i < listcontent; i++) {
    	if (commandlist[i]->argc)
    		if (commandlist[i]->needcondition == -1 || (commandlist[i]->needcondition == 0 && condition == 0) || (commandlist[i]->needcondition == 1 && condition != 0)) {
/*
       			for(int j = 0; j < commandlist[i]->argc; j++) {
        			fprintf(stderr, "%s\t", commandlist[i]->argv[j]);
       		}
         		fprintf(stderr, "%s\n", pwd);
 */       		eval_command(commandlist[i], &condition, pwd);
        	}
	    command_free(commandlist[i]);
    }
    free(commandlist);
    free(pwd);
}


// set_foreground(p)
//    Tell the operating system that `p` is the current foreground process
//    for this terminal. This engages some ugly Unix warts, so we provide
//    it for you.
int set_foreground(pid_t p) {
    // YOU DO NOT NEED TO UNDERSTAND THIS.
    static int ttyfd = -1;
    static int shell_owns_foreground = 0;
    if (ttyfd < 0) {
        // We need a fd for the current terminal, so open /dev/tty.
        int fd = open("/dev/tty", O_RDWR);
        assert(fd >= 0);
        // Re-open to a large file descriptor (>=10) so that pipes and such
        // use the expected small file descriptors.
        ttyfd = fcntl(fd, F_DUPFD, 10);
        assert(ttyfd >= 0);
        close(fd);
        // The /dev/tty file descriptor should be closed in child processes.
        fcntl(ttyfd, F_SETFD, FD_CLOEXEC);
        // Only mess with /dev/tty's controlling process group if the shell
        // is in /dev/tty's controlling process group.
        shell_owns_foreground = (getpgrp() == tcgetpgrp(ttyfd));
    }
    // `p` is in its own process group.
    int r = setpgid(p, p);
    if (r < 0)
        return r;
    // The terminal's controlling process group is `p` (so processes in group
    // `p` can output to the screen, read from the keyboard, etc.).
    if (shell_owns_foreground)
        return tcsetpgrp(ttyfd, p);
    else
        return 0;
}


int main(int argc, char* argv[]) {
    int command_file = stdin;
    int quiet = 0;
    int r = 0;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = 1;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            exit(1);
        }
    }

    char buf[BUFSIZ];
    int bufpos = 0;
    int needprompt = 1;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = 0;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == NULL) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file))
                    perror("sh61");
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            eval_command_line(buf);
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        // Your code here!
    }

    return 0;
}
