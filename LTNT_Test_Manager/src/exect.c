#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wordexp.h>

#include "exect.h"

int exect(char *command) {
	wordexp_t expanded_words;
	int wordexp_rval=-1;
	char *pch,*rch;
	// [0] for stdout and [1] for stderr
	int fd[2]={0};

	bool append; // File mode: 'true' to append stdout/stderr to existing files (if they already exist), 'false' to overwrite existing files
	int stddescriptor;

	char **argv_values;

	if(command==NULL) {
		fprintf(stderr,"Error: exect() received a NULL command string. Cannot launch the desidered program!\n");
		return -1;
	}

	pch=strtok(command,">");

	if(pch==NULL) {
		fprintf(stderr,"Error: exect() received a NULL return value from strtok(). Cannot launch the desidered program!\n");
		return -1;
	}

	if((wordexp_rval=wordexp(command,&expanded_words,0))!=0) {
		fprintf(stderr,"Error: could not expand command: %s.\n"
			"Error code: %d\n",
			command,
			wordexp_rval);
		return -1;
	}

	argv_values=expanded_words.we_wordv;

	while((pch = strtok(NULL,">"))!=NULL) {
		append=false;

		if(pch[0]!='a' && (pch[0]-'0'==STDOUT_FILENO || pch[0]-'0'==STDERR_FILENO)) {
			stddescriptor=pch[0]-'0';
		} else if(pch[0]=='a' && (pch[1]-'0'==STDOUT_FILENO || pch[1]-'0'==STDERR_FILENO)) {
			append=true;
			stddescriptor=pch[1]-'0';
		} else {
			fprintf(stderr,"Error: invalid file redirection sequence: >");

			if(pch[0]=='\0') {
				fprintf(stderr,"0\n");
			} else {
				fprintf(stderr,"%c%c\n",pch[0],pch[1]=='\0' ? '0':pch[1]);
			}

			return -1;
		}

		if(pch[append==true ? 2 : 1]!=' ') {
			fprintf(stderr,"Error: wrong file redirection sequence.\n"
				"A space should be placed between the command and the file name.\n");
			return -1;
		}

		rch=&(pch[append==true ? 2 : 1]);

		for(int i=0;i<strlen(rch+1) || *(rch+1+i)!='\0';i++) {
			if(*(rch+1+i)==' ') {
				*(rch+1+i)='\0';
				break;
			}
		}

		if(fd[stddescriptor == STDOUT_FILENO ? 0 : 1]>0) {
			// Ignore other file redirections other than the first encountered one
			continue;
		}

		errno=0;
		if(append==false) {
			fd[stddescriptor == STDOUT_FILENO ? 0 : 1]=open(rch+1, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);

			if(fd[stddescriptor == STDOUT_FILENO ? 0 : 1]<0) {
				fprintf(stderr,"Error: cannot open file: %s (overwrite). Details: %s.\n",rch+1,strerror(errno));
				return -1;
			}
		} else {
			fd[stddescriptor == STDOUT_FILENO ? 0 : 1]=open(rch+1, O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);

			if(fd[stddescriptor == STDOUT_FILENO ? 0 : 1]<0) {
				// File already exists
				if(errno==EEXIST) {
					fd[stddescriptor == STDOUT_FILENO ? 0 : 1]=open(rch+1, O_WRONLY | O_APPEND);
				} else {
					fprintf(stderr,"Error: cannot open file: %s (append). Details: %s.\n",rch+1,strerror(errno));
					return -1;
				}
			}
		}
	}

	// Debug fprintfs -> to be left commented unless debugging of exect() is in progress
	// fprintf(stderr,"stdout file descriptor for %s: %d.\n",argv_values[0],fd[0]);
	// fprintf(stderr,"stderr file descriptor for %s: %d.\n",argv_values[0],fd[1]);

	// Redirect stdout to file (if requested)
	if(fd[0]>0) {
		dup2(fd[0],STDOUT_FILENO);
	}

	// Redirect stderr to file (if requested)
	if(fd[1]>0) {
		dup2(fd[1],STDERR_FILENO);
	}

	return execv(argv_values[0],argv_values);
}