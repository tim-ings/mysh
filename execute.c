#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>

#include "mysh.h"

/*
   CITS2002 Project 2 2015
   Name(s): 			Tim Ings
   Student number(s): 	21716194
   Date: 				29/10/15
 */

// -------------------------------------------------------------------

// searches the PATH, as defined in mysh.c, for executable files that match name
int execv_p(const char* name, char * const * argv)
{
	// check if name is null
	if (name[0] == '\0')
		return -1; // return -1 as execv didn't replace the current image

	// check if it's absolute/relative
	if (strchr(name, '/'))
	{
		execv(name, argv); // simple execv
	}
	// finally, attempt to find it in PATH
	char buffer[BUFSIZ];
	int name_len = strlen(name);
	do
	{
		intptr_t p;
		for (p = (intptr_t)PATH; *PATH != 0 && *PATH != (intptr_t)':'; PATH++) // find the next path seperator
			continue;

		int pathLen = (intptr_t)(PATH - p); 			// length of this section
		strncpy(buffer, (char*)p, pathLen); 			// copy the contents of this section to the buffer
		buffer[pathLen] = '/';							// add a directory seperator
		strncpy(buffer + pathLen + 1, name, name_len); 	// copy the filename to the buffer
		buffer[pathLen + name_len + 1] = '\0';			// terminate the buffer

		char* final = buffer;
		execv(final, argv); // if this works, execution stops here, otherwise we keep looking
	}
	while (*PATH++ == ':');

	// we didn't find anything in path and execv didn't replace this process,
	// so let's try and execute the file as a script, if it exists
	FILE* script = fopen(name, "r");
	if (script)
	{
		dup2(fileno(script), STDIN_FILENO); // use the script file as stdin
		execv(argv0, argv);					// execute another shell
	}

	return -1; // return -1 if execv didn't replace the current image
}

// changes the current working directory
int cmd_cd(int argc, char **argv)
{
	// save the current directory for error reporting
	char olddir[BUFSIZ];
	getcwd(olddir, sizeof(olddir));

	if (argc != 2) // check if there are no arguments
	{
		// change dir to HOME or DEFAULT_HOME
		if (HOME)
			chdir(HOME);
		else
			chdir(DEFAULT_HOME);
		return 1;
	}

	// check for HOME_CHAR at the beginning of a directory
	char finaldir[BUFSIZ];
	if (argv[1][0] == HOME_CHAR)
	{
		// expand it
		strncpy(finaldir, HOME, BUFSIZ);
		strcat(finaldir, (char*)(argv + 1));
	}
	else
	{
		// just use the entire directory
		strncpy(finaldir, argv[1], BUFSIZ);
	}

	chdir(finaldir); // change the directory

	char newdir[BUFSIZ];
	getcwd(newdir, sizeof(newdir));
	if (strncmp(olddir, newdir, sizeof(newdir)) == 0) // check if we succeeded in changing directories
		fprintf(stderr, "%s: cd: No such file or directory\n", argv[0]); // report an error if we haven't

	return 1;
}

// exits the shell
int cmd_exit(int argc, char **args)
{
	exit(EXIT_SUCCESS); // gracefully exit
}

// times the execution of a command
int cmd_time(int argc, char** args)
{
	int exitstatus = EXIT_FAILURE;

	// remove "time" from args to be passed
	char** argsPass = malloc(sizeof(argsPass) * (argc - 1));
	for (int i = 1; i < argc; i++)
	{
		int len = strlen(args[i]);
		argsPass[i - 1] = malloc(len);
		strncpy(argsPass[i - 1], args[i], len);
	}

	struct timeval tv0, tv1;
	gettimeofday(&tv0, NULL);	// get time before

	// run the command
	int child = fork();
	if (child == 0) // child
		exitstatus = execv_p(args[1], argsPass);
	else if (child < 0) // error
		fprintf(stderr, "An error occured calling program/file: %s", args[1]); // report the error
	else // main program
	{
		int status;
		wait(&status); // wait until the child process is finished
		exitstatus = WEXITSTATUS(status);
	}

	if (exitstatus == -1)
		exit(EXIT_FAILURE); // kill the child if execv_p has failed

	gettimeofday(&tv1, NULL);	// get time afterwards

	// calculate and display the time the command took in msec
	long long elapsed = (tv1.tv_sec-tv0.tv_sec)*1000 + (tv1.tv_usec-tv0.tv_usec) / 1000;
	fprintf(stderr, "Time elapsed = %lld msec\n", elapsed);

	// clean up
	for (int i = 0; i < (argc - 1); i++)
		free(argsPass[i]);
	free(argsPass);

	return 1;
}

// sets the given var to the given value
int cmd_set(int argc, char** args)
{
	if (argc != 3) // check for correct arg count
		return 0;

	// set the correct variable
	if (strcmp(args[1], "PATH") == 0)
		PATH = args[2];
	else if (strcmp(args[1], "HOME") == 0)
		HOME = args[2];
	else
	{
		fprintf(stderr, "Unknown variable: %s\n", args[1]); // report the error
		return 0;
	}
	fprintf(stderr, "%s updated\n", args[1]); // report success
	return 1;
}

// calls and corresponding functions
// the order of both lists must match
char *cmd_calls[] = {
		"cd",
		"exit",
		"time",
		"set"
};
int (*cmd_functions[])(int, char **) = {
		&cmd_cd,
		&cmd_exit,
		&cmd_time,
		&cmd_set
};

// counts the number of cmd_calls
int cmd_call_count()
{
	return sizeof(cmd_calls) / sizeof(char*);
}

// checks for inbuilt commands
int try_inbuilt(int argc, char** argv)
{
	int cmdCallCount = cmd_call_count();
	for (int i = 0; i < cmdCallCount; i++)
	{
		if (strcmp(argv[0], cmd_calls[i]) == 0) 		// compare command to each possible inbuilt call
		{
			return (*cmd_functions[i])(argc, argv); 	// if we have a match, execute the corresponding command
		}
	}
	return 0;
}

// executes a tree of type N_COMMAND
int execute_command(CMDTREE* t)
{
	if (!t || (t && t->type != N_COMMAND))
		return EXIT_FAILURE; // that's the wrong type of tree

	int exitstatus = 0;
	int stdin = 0;
	int stdout = 0;
	FILE* infile = NULL;
	FILE* outfile = NULL;

	// check if redirection is required
	if (t->infile)
	{
		infile = fopen(t->infile, "r"); // open the file
		if (!infile)
		{
			fprintf(stderr, "No such file or directory\n");	// report the error
			return EXIT_FAILURE;
		}
		stdin = dup(STDIN_FILENO);			// save the current stdin
		dup2(fileno(infile), STDIN_FILENO);	// redirect stdin
	}
	if (t->outfile)
	{
		if (t->append) // check if we need to append or overwrite the outfile, and open it as required
			outfile = fopen(t->outfile, "a");
		else
			outfile = fopen(t->outfile, "w");

		if (!outfile)
		{
			fprintf(stderr, "No such file or directory\n"); // report the error
			return EXIT_FAILURE;
		}
		stdout = dup(STDOUT_FILENO);			// save the current stdout
		dup2(fileno(outfile), STDOUT_FILENO);	// redirect stdout
	}

	// run the command
	if (!try_inbuilt(t->argc, t->argv))
	{
		int child = fork();
		if (child == 0) // child
		{
			exitstatus = execv_p(t->argv[0], t->argv); // search PATH and try and execute the command
		}
		else if (child < 0) // error
		{
			fprintf(stderr, "Forking failed\n"); // report the error
		}
		else // main program
		{
			int status;
			wait(&status); // wait until the child process is finished
			exitstatus = WEXITSTATUS(status); // get the exitstatus
		}
		if (exitstatus == -1)
		{
			fprintf(stderr, "%s: %s: No such file or directory\n", argv0, t->argv[0]); // report the error
			exit(EXIT_FAILURE); // kill the child
		}
	}

	// clean up
	if (infile)
		fclose(infile);
	if (outfile)
		fclose(outfile);
	if (t->infile)
		dup2(stdin, STDIN_FILENO);
	if (t->outfile)
		dup2(stdout, STDOUT_FILENO);

	return exitstatus;
}

// executes a tree of type N_SUBSHELL
int execute_subshell(CMDTREE* t)
{
	if (!t || (t && t->type != N_SUBSHELL))
		return EXIT_FAILURE; // that's the wrong type of tree

	int exitstatus = 0;
	int child = fork();
	if (child == 0) // child
	{
		exitstatus = execute_cmdtree(t->left);
	}
	else if (child < 0) // fork failed
	{
		fprintf(stderr, "Forking failed\n");
	}
	else // main program
	{
		int status;
		wait(&status); // wait until the sub shell is finished
		exitstatus = WEXITSTATUS(status); // get the exit status
	}
	if (exitstatus == -1)
		exit(EXIT_FAILURE); // kill the child if execv_p has failed (error reporting is already handled)
	return exitstatus;
}

// executes a tree of type N_AND
int execute_and(CMDTREE* t)
{
	if (!t || (t && t->type != N_AND))
		return EXIT_FAILURE; // that's the wrong type of tree

	CMDTREE* current_tree = t;
	int result = EXIT_FAILURE;
	while (current_tree->right)	// keep going until we run out of nodes to move to
	{
		CMDTREE* to_execute;
		switch (current_tree->type) // get the tree to execute
		{
		case N_AND: 	to_execute = current_tree->left; 	break;
		case N_COMMAND:	to_execute = current_tree;			break;
		default:											return result;
		}

		if (to_execute)
			result = execute_command(to_execute);	// run the current command
		if (result == EXIT_FAILURE) 				// check if the last command succeeded
			return result;							// stop executing any more commands

		current_tree = current_tree->right;			// move to the next tree
	}

	if (current_tree && current_tree->type == N_COMMAND)
		result = execute_command(current_tree); // run the final command

	return result;
}

// executes a tree of type N_OR
int execute_or(CMDTREE* t)
{
	if (!t || (t && t->type != N_OR))
		return EXIT_FAILURE; // that's the wrong type of tree

	CMDTREE* current_tree = t;
	int result = EXIT_FAILURE;
	while (current_tree->right)	// keep going until we run out of nodes to move to
	{
		CMDTREE* to_execute;
		switch (current_tree->type) // get the tree to execute
		{
		case N_OR: 	to_execute = current_tree->left; 		break;
		case N_COMMAND:	to_execute = current_tree;			break;
		default:											return result;
		}

		if (to_execute)
			result = execute_command(to_execute);	// run the current command
		if (result == EXIT_SUCCESS) 				// check if the last command succeeded
			return result;							// stop executing any more commands

		current_tree = current_tree->right;			// move to the next tree
	}

	if (current_tree && current_tree->type == N_COMMAND)
		result = execute_command(current_tree); // run the last command

	return result;
}

// executes a tree of type N_PIPE
int execute_pipe(CMDTREE* t)
{
	if (!t || (t && t->type!= N_PIPE))
		return EXIT_FAILURE; // that's the wrong type of tree

	// setup the pipe
	int exitstatus = 0;
	int status;
	int fds[2];
	pipe(fds);

	int child0 = fork();
	int child0exit = 0;
	if (child0 == 0) // child 0
	{
		dup2(fds[PIPE_WRITE], STDOUT_FILENO); // redirect the first child's output to the pipe's input
		close(fds[PIPE_READ]);
		close(fds[PIPE_WRITE]);
		child0exit = execv_p(t->left->argv[0], t->left->argv); // run the first command
	}
	else if (child0 < 0) // error
	{
		fprintf(stderr, "Forking failed\n"); // report the error
	}
	else // main program
	{
		int status;
		waitpid(child0, &status, 0); // wait until the child process is finished
		child0exit = WEXITSTATUS(status); // get the exitstatus
	}
	if (child0exit == -1)
	{
		fprintf(stderr, "%s: %s: No such file or directory\n", argv0, t->left->argv[0]); // report the error
		exit(EXIT_FAILURE); // kill the child
	}

	int child1 = fork();
	int child1exit = 0;
	if (child1 == 0)
	{
		dup2(fds[PIPE_READ], STDIN_FILENO); // redirect the second child's input to the pipe's output
		close(fds[PIPE_READ]);
		close(fds[PIPE_WRITE]);
		child1exit = execv_p(t->right->argv[0], t->right->argv); // run the second command
	}
	else if (child1 < 0) // error
	{
		fprintf(stderr, "Forking failed\n"); // report the error
	}
	else // main program
	{
		int status;
		waitpid(child1, &status, 0); // wait until the child process is finished
		child1exit = WEXITSTATUS(status); // get the exitstatus
	}
	if (child1exit == -1)
	{
		fprintf(stderr, "%s: %s: No such file or directory\n", argv0, t->right->argv[0]); // report the error
		exit(EXIT_FAILURE); // kill the child
	}

	close(fds[PIPE_READ]);
	close(fds[PIPE_WRITE]);

	wait(&status); // wait for both children
	wait(&status);
	exitstatus = WEXITSTATUS(status);

	if (exitstatus == -1)
		exit(EXIT_FAILURE); // kill the child if execv_p has failed
	return exitstatus;
}

// executes a tree of type N_SEMICOLON
int execute_semicolon(CMDTREE* t)
{
	if (!t || (t && t->type!= N_SEMICOLON))
		return EXIT_FAILURE; // that's the wrong type of tree

	CMDTREE* currentTree = t;
	while (currentTree->right && currentTree->right->type == N_SEMICOLON) // keep going until we run out of nodes to move to
	{
		execute_command(currentTree->left); // run the current command
		currentTree = currentTree->right; // move
	}
	// finish the last two trees
	if (currentTree->left) execute_command(currentTree->left);
	if (currentTree->right) execute_command(currentTree->right);

	return EXIT_FAILURE;
}

// executes a tree of type N_BACKGROUND
int execute_background(CMDTREE* t)
{
	if (!t || (t && t->type!= N_BACKGROUND))
		return EXIT_FAILURE; // that's the wrong type of tree

	int exitstatus = 0;
	if (!try_inbuilt(t->left->argc, t->left->argv)) // check if it is an inbuilt command
	{
		int child = fork();
		if (child == 0) // child
		{
			fprintf(stderr, "[%d] %d\n", LAST_BACKGROUND_ID++, getpid());
			execv_p(t->left->argv[0], t->left->argv);
		}
		else if (child < 0) // error
		{
			fprintf(stderr, "An error occured calling program/file: %s", t->argv[0]);
			exitstatus = EXIT_FAILURE;
		}
	}
	fprintf(stderr, "done starting background\n");
	return exitstatus;
}

//  THIS FUNCTION SHOULD TRAVERSE THE COMMAND-TREE and EXECUTE THE COMMANDS
//  THAT IT HOLDS, RETURNING THE APPROPRIATE EXIT-STATUS.
//  READ print_cmdtree0() IN globals.c TO SEE HOW TO TRAVERSE THE COMMAND-TREE
int execute_cmdtree(CMDTREE *t)
{
	int exitstatus;

	if (t == NULL) // hmmmm, a that's problem
	{
		exitstatus	= EXIT_FAILURE;
	}
	else // normal, exit commands
	{
		exitstatus	= EXIT_SUCCESS;
		switch (t->type)
		{
		case N_COMMAND : // <cmd0>, simply call <cmd0>
			execute_command(t);
			break;

		case N_SUBSHELL : // (... <cmdN> ...), run everything inside brackets in a child shell
			execute_subshell(t);
			break;

		case N_AND : // <cmd0> && <cmd1>, if <cmd0> succeeds, call <cmd1>, if <cmdN-1> succeeds, call <cmdN>
			execute_and(t);
			break;

		case N_OR : // <cmd0> || <cmd1>, if <cmd0> fails, call <cmd1>, if <cmdN-1> fails, call <cmdN>
			execute_or(t);
			break;

		case N_PIPE : // <cmd0> | <cmd1>, pass result of <cmd0> to <cmd1> to ... to <cmdN>
			execute_pipe(t);
			break;

		case N_SEMICOLON : // <cmd0> ; <cmd1>, execute commands sequentially <cmd0>, <cmd1>, <cmd2>, ..., <cmdN>
			execute_semicolon(t);
			break;

		case N_BACKGROUND : // <cmd0> & <cmd1>, don't wait for <cmd0> to finish before executing <cmd1>
			execute_background(t);
			break;

		default :
			exit(EXIT_FAILURE);
			break;
		}
	}

	return exitstatus;
}
