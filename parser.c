#include "mysh.h"

/*
   CITS2002 Project 2 2015
   Name(s): 			Tim Ings
   Student number(s): 	21716194
   Date: 				29/10/15
 */

//  Written by Chris.McDonald@uwa.edu.au, October 2015

/*  This file provides the most complicated part of the shell -
    its command parser.  This file provides two functions (at bottom of file):

	    CMDTREE	*parse_cmdtree(FILE *fp);
    and	    void	free_cmdtree(CMDTREE *t);

    These two functions need only be called from within the main() function.
    All other functions are variables are declared as 'static' so that they
    are not visible outside of this file.

    Remember, there is no need for you to understand this code;
    however, it is provided here for your edification and didaction.
 */

#include <signal.h>
#include <setjmp.h>

#define	MAXARGS		254		// hope that this will be enough!

// ----------------------- parsing definitions --------------------------

typedef enum {
	T_AND = 0,
	T_APPEND,
	T_BACKGROUND,
	T_BAD,
	T_DQUOTE,
	T_EOF,
	T_FROMFILE,
	T_LEFTB,
	T_NL,
	T_OR,
	T_PIPE,
	T_RIGHTB,
	T_SCOLON,
	T_SQUOTE,
	T_TOFILE,
	T_WORD,
} TOKEN;

#define	is_redirection(t) (t == T_FROMFILE || t == T_TOFILE || t == T_APPEND)
#define	is_word(t)        (t == T_WORD || t == T_DQUOTE || t == T_SQUOTE)

// -------------------------- lexical stuff -----------------------------

static	FILE	*fp;

static	TOKEN	token;
static	char	chararray[BUFSIZ];
static	char	line[BUFSIZ];
static	char	nerrors		= 0;

static	char	*cp;
static	int	nextch;
static	int	cc;
static	int	ll;
static	int	lc;

static	int	prompt_no	= 1;

static void get(void)		// get the next buffered char from line
{
	if(cc == ll) {
		if(interactive) {
			static char	PS1[32], PS2[32];

			if(lc == 1) {
				char *s = PS1;

				sprintf(PS1, "\n%s.%i ", argv0, prompt_no);
				while(*s)
					s++;
				strcpy(PS2," ++          ");
				PS2[ s-PS1-1 ] = '\0';
			}
			fputs(lc == 1 ? PS1 : PS2 , stdout);
		}
		fgets(line, sizeof line, fp);
		ll	= strlen(line);
		cc	= 0;
		lc++;
	}
	nextch = line[cc++];
}

#define unget()		(nextch = line[--cc])

static void skip_blanks(void)		//  Skip spaces, tabs and comments
{
	while(nextch == ' ' || nextch == '\t' || nextch == COMMENT_CHAR) {
		if(nextch == COMMENT_CHAR)	//  ignore to end-of-line
			while (nextch != '\n')
				get();
		get();
	}
}

static void escape_char(void)
{
	get();
	switch(nextch) {
	case 'b' :  *cp++ = '\b'; break;
	case 'f' :  *cp++ = '\f'; break;
	case 'n' :  *cp++ = '\n'; break;
	case 'r' :  *cp++ = '\r'; break;
	case 't' :  *cp++ = '\t'; break;
	default  :  *cp++ = nextch;
	}
	get();
}

static void gettoken(void)
{
	*chararray	= '\0';
	get();
	skip_blanks();
	if(feof(fp)) {
		token = T_EOF;
		return;
	}
	else if(nextch == '<') {		// input redirection
		token = T_FROMFILE;
		return;
	}
	else if(nextch == '>') {		// output redirection
		get();
		if(nextch == '>') {
			token = T_APPEND;
		}
		else {
			unget();
			token = T_TOFILE;
		}
		return;
	}
	else if(nextch == ';') {		// sequential
		token = T_SCOLON;
		return;
	}
	else if(nextch == '&') {		// and-conditional
		get();
		if(nextch == '&')
			token = T_AND;
		else {
			unget();			// background
			token = T_BACKGROUND;
		}
	}
	else if(nextch == '|') {		// or-conditional
		get();
		if(nextch == '|') {
			token = T_OR;
		}
		else {				// pipe operator
			unget();
			token = T_PIPE;
		}
		return;
	}
	else if(nextch == '(') {		// subshells
		token = T_LEFTB;
		return;
	}
	else if(nextch == ')') {		// subshells
		token = T_RIGHTB;
		return;
	}
	else if(nextch == '\n') {		// newline => sequential
		token = T_NL;
		return;
	}
	else if(nextch == '"' || nextch == '\'') {
		*chararray = nextch;
		cp = chararray+1;
		do {
			get();
			while(nextch == '\\')
				escape_char();
			*cp++ = nextch;
		}   while(nextch != *chararray && !feof(fp));
		*--cp = '\0';
		token = *chararray == '"' ? T_DQUOTE : T_SQUOTE;
		return;
	}
	else {
		cp = chararray;
		while(!feof(fp)  && !strchr(" \t\n<>|();&",nextch)) {
			while(nextch == '\\')
				escape_char();
			*cp++ = nextch;
			get();
		}
		unget();
		*cp = '\0';
		token = T_WORD;
		return;
	}
}

// -------------------- parsing code (at last!) ------------------------

static CMDTREE *new_cmdtree(NODETYPE t)
{
	CMDTREE	*t1 = calloc(1, sizeof(*t1));

	check_allocation(t1);
	t1->type	= t;
	return t1;
}

static void get_redirection(CMDTREE *t1)
{
	char	*filename;
	TOKEN	cptoken	= token;

	gettoken();
	if(token == T_WORD) {
		filename = strdup(chararray);
		check_allocation(filename);
	}
	else if(token == T_DQUOTE) {
		filename = strdup(chararray+1);
		check_allocation(filename);
	}
	else if(token == T_SQUOTE) {
		filename = strdup(chararray+1);
		check_allocation(filename);
	}
	else {
		fprintf(stderr, "%s redirection filename expected\n",
				cptoken == T_FROMFILE ? "input" : "output");
		nerrors++;
		return;
	}

	if(cptoken == T_FROMFILE) {
		if(t1->infile != NULL) {
			fprintf(stderr, "multiple input redirection\n");
			nerrors++;
			return;
		}
		t1->infile = filename;
	}
	else {
		if(t1->outfile != NULL) {
			fprintf(stderr, "multiple output redirection\n");
			nerrors++;
			return;
		}
		t1->append	= (cptoken == T_APPEND);
		t1->outfile	= filename;
	}
}

static CMDTREE *cmd_wordlist(void)
{
	static char	*argv[MAXARGS+2];

	int		argc	= 0;
	CMDTREE	*t1	= new_cmdtree(N_COMMAND);

	while(!feof(fp) && (is_redirection(token) || is_word(token))) {
		switch ((int)token) {
		case T_WORD :
			if(argc < MAXARGS) {
				if(chararray[0] == HOME_CHAR) {
					argv[argc]	= malloc(strlen(HOME) + strlen(chararray) + 1);
					check_allocation(argv[argc]);
					sprintf(argv[argc], "%s%s", HOME, chararray+1);
				}
				else {
					argv[argc]	= strdup(chararray);
					check_allocation(argv[argc]);
				}
				++argc;
				argv[argc] = NULL;
			}
			break;
		case T_SQUOTE :
		case T_DQUOTE :
			if(argc < MAXARGS) {
				argv[argc] = strdup(chararray+1);
				check_allocation(argv[argc]);
				++argc;
				argv[argc] = NULL;
			}
			break;
		case T_FROMFILE :
		case T_TOFILE :
		case T_APPEND :
			get_redirection(t1);
			break;
		}
		gettoken();
	}
	if(argc == 0) {
		free(t1);
		return NULL;
	}
	argv[argc] = NULL;
	t1->argc	= argc;
	t1->argv	= malloc(((argc+1) * sizeof(t1->argv[0])));
	check_allocation(t1->argv);

	while(argc >= 0) {
		t1->argv[argc] = argv[argc];
		--argc;
	}
	return t1;
}

static CMDTREE *cmd_pipeline(void);		// a forward declaration

static CMDTREE *cmd_condition(void)
{
	CMDTREE		*t1, *t2;

	t1 = cmd_pipeline();
	if(token == T_AND || token == T_OR) {
		t2		= new_cmdtree(token == T_AND ? N_AND : N_OR);
		t2->left	= t1;
		gettoken();
		t2->right	= cmd_condition();
		t1		= t2;
	}
	return t1;
}

static CMDTREE *cmd_sequence(void)
{
	CMDTREE		*t1, *t2;

	t1 = cmd_condition();
	if(token == T_SCOLON || token == T_BACKGROUND) {
		t2	= new_cmdtree(token == T_SCOLON ? N_SEMICOLON : N_BACKGROUND);
		t2->left	= t1;
		gettoken();
		t2->right	= cmd_sequence();
		t1		= t2;
	}
	return t1;
}

static CMDTREE *cmd_factor(void)
{
	CMDTREE		*t1, *t2;

	if(token == T_LEFTB) {
		gettoken();
		t1 = cmd_sequence();
		if(token != T_RIGHTB) {
			fprintf(stderr, "')' expected\n");
			nerrors++;
			t1 = NULL;
		}
		else {
			if(t1 == NULL) {
				fprintf(stderr, "subshells may not be empty\n");
				nerrors++;
			}
			t2		= new_cmdtree(N_SUBSHELL);
			t2->left	= t1;
			t2->right	= NULL;
			gettoken();
			while(is_redirection(token)) {
				get_redirection(t2);
				gettoken();
			}
			t1		= t2;
		}
	}
	else
		t1 = cmd_wordlist();
	return t1;
}

static CMDTREE *cmd_pipeline(void)
{
	CMDTREE		*t1, *t2;

	t1 = cmd_factor();
	if(token == T_PIPE) {

		if(t1 != NULL && t1->outfile != NULL) {
			fprintf(stderr, "output cannot be both redirected and piped\n");
			nerrors++;
			free_cmdtree(t1);
			return NULL;
		}
		t2		= new_cmdtree(N_PIPE);
		t2->left	= t1;
		gettoken();
		t2->right	= cmd_pipeline();

		if(t2->right != NULL && t2->right->infile != NULL) {
			fprintf(stderr, "input cannot be both redirected and piped\n");
			nerrors++;
			free_cmdtree(t2);
			return NULL;
		}
		t1		= t2;
	}
	return t1;
}

// -------------------------- parsing stuff -----------------------------

static	jmp_buf	env;

static void interrupt(int which)	// control-C to interrupt parsing
{
	longjmp(env,1);
}

typedef	void	(*sighandler_t)(int);

CMDTREE *parse_cmdtree(FILE *_fp)
{
	CMDTREE		*t1;
	sighandler_t	old_handler;

	old_handler	= signal(SIGINT,interrupt);
	if(setjmp(env)) {
		if(interactive)
			fputc('\n',stdout);
	}
	do {
		t1		= NULL;
		fp		= _fp;
		cc		= 0;
		ll		= 0;
		lc		= 1;
		nerrors		= 0;
		if(feof(fp)) {
			break;
		}
		gettoken();
	} while((t1 = cmd_sequence()) == NULL);
	signal(SIGINT, old_handler);	// control-C to interrupt parsing

	++prompt_no;

	if(token != T_NL && token != T_EOF) {
		fputs("garbage at end of line\n", stderr);
		nerrors++;
	}
	return (nerrors == 0 ? t1 : NULL);
}

//  DEALLOCATE ALL DYNAMICALLY ALLOCATED MEMORY IN A COMMAND-TREE
void free_cmdtree(CMDTREE *t)
{
	if(t != NULL) {
		switch (t->type) {
		case N_COMMAND :
			if(t->argc > 0) {			// free command-words
				for(int a=0 ; a<t->argc ; a++)
					free(t->argv[a]);
				free(t->argv);
			}
			if(t->infile)			// free any I/O redirection
				free(t->infile);
			if(t->outfile)
				free(t->outfile);
			break;
		case N_SUBSHELL :
			free_cmdtree(t->left);
			if(t->infile)			// free any I/O redirection
				free(t->infile);
			if(t->outfile)
				free(t->outfile);
			break;
		case N_AND :
		case N_BACKGROUND :
		case N_OR :
		case N_PIPE :
		case N_SEMICOLON :
			free_cmdtree(t->left);		// free the subtrees
			free_cmdtree(t->right);
			break;
		}
		free(t);				// and free the node itself
	}
}
