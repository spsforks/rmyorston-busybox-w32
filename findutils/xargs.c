/* vi: set sw=4 ts=4: */
/*
 * Mini xargs implementation for busybox
 *
 * (C) 2002,2003 by Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Special thanks
 * - Mark Whitley and Glenn McGrath for stimulus to rewrite :)
 * - Mike Rendell <michael@cs.mun.ca>
 * and David MacKenzie <djm@gnu.ai.mit.edu>.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * xargs is described in the Single Unix Specification v3 at
 * http://www.opengroup.org/onlinepubs/007904975/utilities/xargs.html
 */
//config:config XARGS
//config:	bool "xargs (7.6 kb)"
//config:	default y
//config:	help
//config:	xargs is used to execute a specified command for
//config:	every item from standard input.
//config:
//config:config FEATURE_XARGS_SUPPORT_CONFIRMATION
//config:	bool "Enable -p: prompt and confirmation"
//config:	default y
//config:	depends on XARGS
//config:	help
//config:	Support -p: prompt the user whether to run each command
//config:	line and read a line from the terminal.
//config:
//config:config FEATURE_XARGS_SUPPORT_QUOTES
//config:	bool "Enable single and double quotes and backslash"
//config:	default y
//config:	depends on XARGS
//config:	help
//config:	Support quoting in the input.
//config:
//config:config FEATURE_XARGS_SUPPORT_TERMOPT
//config:	bool "Enable -x: exit if -s or -n is exceeded"
//config:	default y
//config:	depends on XARGS
//config:	help
//config:	Support -x: exit if the command size (see the -s or -n option)
//config:	is exceeded.
//config:
//config:config FEATURE_XARGS_SUPPORT_ZERO_TERM
//config:	bool "Enable -0: NUL-terminated input"
//config:	default y
//config:	depends on XARGS
//config:	help
//config:	Support -0: input items are terminated by a NUL character
//config:	instead of whitespace, and the quotes and backslash
//config:	are not special.
//config:
//config:config FEATURE_XARGS_SUPPORT_REPL_STR
//config:	bool "Enable -I STR: string to replace"
//config:	default y
//config:	depends on XARGS
//config:	help
//config:	Support -I STR and -i[STR] options.
//config:
//config:config FEATURE_XARGS_SUPPORT_PARALLEL
//config:	bool "Enable -P N: processes to run in parallel"
//config:	default y
//config:	depends on XARGS
//config:
//config:config FEATURE_XARGS_SUPPORT_ARGS_FILE
//config:	bool "Enable -a FILE: use FILE instead of stdin"
//config:	default y
//config:	depends on XARGS

//applet:IF_XARGS(APPLET_NOEXEC(xargs, xargs, BB_DIR_USR_BIN, BB_SUID_DROP, xargs))

//kbuild:lib-$(CONFIG_XARGS) += xargs.o

#if ENABLE_PLATFORM_MINGW32
#include <conio.h>
#include "busybox.h"
#include "NUM_APPLETS.h"
#endif
#include "libbb.h"
#include "common_bufsiz.h"

/* This is a NOEXEC applet. Be very careful! */


#if 0
# define dbg_msg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg_msg(...) ((void)0)
#endif

#ifdef TEST
# ifndef ENABLE_FEATURE_XARGS_SUPPORT_CONFIRMATION
#  define ENABLE_FEATURE_XARGS_SUPPORT_CONFIRMATION 1
# endif
# ifndef ENABLE_FEATURE_XARGS_SUPPORT_QUOTES
#  define ENABLE_FEATURE_XARGS_SUPPORT_QUOTES 1
# endif
# ifndef ENABLE_FEATURE_XARGS_SUPPORT_TERMOPT
#  define ENABLE_FEATURE_XARGS_SUPPORT_TERMOPT 1
# endif
# ifndef ENABLE_FEATURE_XARGS_SUPPORT_ZERO_TERM
#  define ENABLE_FEATURE_XARGS_SUPPORT_ZERO_TERM 1
# endif
#endif


struct globals {
	char **args;
#if ENABLE_FEATURE_XARGS_SUPPORT_REPL_STR
	char **argv;
	const char *repl_str;
	char eol_ch;
#endif
	const char *eof_str;
	int idx;
#if !ENABLE_PLATFORM_MINGW32
	int fd_tty;
	int fd_stdin;
#endif
#if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	int running_procs;
	int max_procs;
# if ENABLE_PLATFORM_MINGW32
	HANDLE *procs;
# endif
#endif
#if ENABLE_PLATFORM_MINGW32
	pid_t pid;
#endif
	smalluint xargs_exitcode;
#if ENABLE_FEATURE_XARGS_SUPPORT_QUOTES
#define NORM      0
#define QUOTE     1
#define BACKSLASH 2
#define SPACE     4
	smalluint process_stdin__state;
	char process_stdin__q;
#endif
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	IF_FEATURE_XARGS_SUPPORT_REPL_STR(G.repl_str = "{}";) \
	IF_FEATURE_XARGS_SUPPORT_REPL_STR(G.eol_ch = '\n';) \
	/* Even zero values are set because we are NOEXEC applet */ \
	G.eof_str = NULL; \
	G.idx = 0; \
	IF_FEATURE_XARGS_SUPPORT_PARALLEL(G.running_procs = 0;) \
	IF_FEATURE_XARGS_SUPPORT_PARALLEL(G.max_procs = 1;) \
	IF_FEATURE_XARGS_SUPPORT_PARALLEL(IF_PLATFORM_MINGW32(G.procs = NULL;)) \
	G.xargs_exitcode = 0; \
	IF_FEATURE_XARGS_SUPPORT_QUOTES(G.process_stdin__state = NORM;) \
	IF_FEATURE_XARGS_SUPPORT_QUOTES(G.process_stdin__q = '\0';) \
} while (0)

/* Correct regardless of combination of CONFIG_xxx */
enum {
	OPTBIT_VERBOSE = 0,
	OPTBIT_NO_EMPTY,
	OPTBIT_UPTO_NUMBER,
	OPTBIT_UPTO_SIZE,
	OPTBIT_EOF_STRING,
	OPTBIT_EOF_STRING1,
	IF_NOT_PLATFORM_MINGW32(              OPTBIT_STDIN_TTY  ,)
	IF_FEATURE_XARGS_SUPPORT_CONFIRMATION(OPTBIT_INTERACTIVE,)
	IF_FEATURE_XARGS_SUPPORT_TERMOPT(     OPTBIT_TERMINATE  ,)
	IF_FEATURE_XARGS_SUPPORT_ZERO_TERM(   OPTBIT_ZEROTERM   ,)
	IF_FEATURE_XARGS_SUPPORT_REPL_STR(    OPTBIT_REPLSTR    ,)
	IF_FEATURE_XARGS_SUPPORT_REPL_STR(    OPTBIT_REPLSTR1   ,)

	OPT_VERBOSE     = 1 << OPTBIT_VERBOSE    ,
	OPT_NO_EMPTY    = 1 << OPTBIT_NO_EMPTY   ,
	OPT_UPTO_NUMBER = 1 << OPTBIT_UPTO_NUMBER,
	OPT_UPTO_SIZE   = 1 << OPTBIT_UPTO_SIZE  ,
	OPT_EOF_STRING  = 1 << OPTBIT_EOF_STRING , /* GNU: -e[<param>] */
	OPT_EOF_STRING1 = 1 << OPTBIT_EOF_STRING1, /* SUS: -E<param> */
	OPT_STDIN_TTY   = IF_NOT_PLATFORM_MINGW32(              (1 << OPTBIT_STDIN_TTY  )) + 0,
	OPT_INTERACTIVE = IF_FEATURE_XARGS_SUPPORT_CONFIRMATION((1 << OPTBIT_INTERACTIVE)) + 0,
	OPT_TERMINATE   = IF_FEATURE_XARGS_SUPPORT_TERMOPT(     (1 << OPTBIT_TERMINATE  )) + 0,
	OPT_ZEROTERM    = IF_FEATURE_XARGS_SUPPORT_ZERO_TERM(   (1 << OPTBIT_ZEROTERM   )) + 0,
	OPT_REPLSTR     = IF_FEATURE_XARGS_SUPPORT_REPL_STR(    (1 << OPTBIT_REPLSTR    )) + 0,
	OPT_REPLSTR1    = IF_FEATURE_XARGS_SUPPORT_REPL_STR(    (1 << OPTBIT_REPLSTR1   )) + 0,
};
#define OPTION_STR "+trn:s:e::E:" \
	IF_NOT_PLATFORM_MINGW32(              "o") \
	IF_FEATURE_XARGS_SUPPORT_CONFIRMATION("p") \
	IF_FEATURE_XARGS_SUPPORT_TERMOPT(     "x") \
	IF_FEATURE_XARGS_SUPPORT_ZERO_TERM(   "0") \
	IF_FEATURE_XARGS_SUPPORT_REPL_STR(    "I:i::") \
	IF_FEATURE_XARGS_SUPPORT_PARALLEL(    "P:+") \
	IF_FEATURE_XARGS_SUPPORT_ARGS_FILE(   "a:")


#if ENABLE_PLATFORM_MINGW32
static BOOL WINAPI ctrl_handler(DWORD dwCtrlType)
{
	if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
# if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
		if (G.max_procs == 1)
# endif
		{
			if (G.pid > 0)
				kill(-G.pid, SIGTERM);
		}
# if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
		else {
			int i;

			for (i = 0; i < G.running_procs; ++i) {
				pid_t pid = GetProcessId(G.procs[i]);
				if (pid > 0)
					kill(-pid, SIGTERM);
			}
		}
# endif
		exit(SIGINT << 24);
		return TRUE;
	}
	return FALSE;
}
#endif

#if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL && ENABLE_PLATFORM_MINGW32
static int wait_for_slot(int *idx)
{
	int i;

	/* if less than max_procs running, set status to 0, return next free slot */
	if (G.running_procs < G.max_procs) {
		*idx = G.running_procs++;
		return 0;
	}

check_exit_codes:
	for (i = G.running_procs - 1; i >= 0; i--) {
		DWORD status = 0;
		if (!GetExitCodeProcess(G.procs[i], &status) ||
				status != STILL_ACTIVE) {
			CloseHandle(G.procs[i]);
			if (i + 1 < G.running_procs)
				G.procs[i] = G.procs[G.running_procs - 1];
			*idx = G.running_procs - 1;
			if (!G.max_procs)
				G.running_procs--;
			return status;
		}
	}

	if (G.running_procs < MAXIMUM_WAIT_OBJECTS)
		WaitForMultipleObjects((DWORD)G.running_procs, G.procs, FALSE,
				INFINITE);
	else {
		/* Fall back to polling */
		for (;;) {
			DWORD nr = i + MAXIMUM_WAIT_OBJECTS > G.running_procs ?
				MAXIMUM_WAIT_OBJECTS : (DWORD)(G.running_procs - i);
			DWORD ret = WaitForMultipleObjects(nr, G.procs + i, FALSE, 100);

			if (ret != WAIT_TIMEOUT)
				break;
			i += MAXIMUM_WAIT_OBJECTS;
			if (i > G.running_procs)
				i = 0;
		}
	}

	goto check_exit_codes;
}
#endif /* SUPPORT_PARALLEL && PLATFORM_MINGW32 */

/*
 * Returns 0 if xargs should continue (but may set G.xargs_exitcode to 123).
 * Else sets G.xargs_exitcode to error code and returns nonzero.
 *
 * If G.max_procs == 0, performs final waitpid() loop for all children.
 */
static int xargs_exec(void)
{
	int status;

#if !ENABLE_PLATFORM_MINGW32
	if (option_mask32 & OPT_STDIN_TTY)
		xdup2(G.fd_tty, STDIN_FILENO);

#if !ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	status = spawn_and_wait(G.args);
#else
	if (G.max_procs == 1) {
		status = spawn_and_wait(G.args);
	} else {
		pid_t pid;
		int wstat;
 again:
		if (G.running_procs >= G.max_procs)
			pid = safe_waitpid(-1, &wstat, 0);
		else
			pid = wait_any_nohang(&wstat);
		if (pid > 0) {
			/* We may have children we don't know about:
			 * sh -c 'sleep 1 & exec xargs ...'
			 * Do not make G.running_procs go negative.
			 */
			if (G.running_procs != 0)
				G.running_procs--;
			status = WIFSIGNALED(wstat)
				? 0x180 + WTERMSIG(wstat)
				: WEXITSTATUS(wstat);
			if (status > 0 && status < 255) {
				/* See below why 123 does not abort */
				G.xargs_exitcode = 123;
				status = 0;
			}
			if (status == 0)
				goto again; /* maybe we have more children? */
			/* else: "bad" status, will bail out */
		} else if (G.max_procs != 0) {
			/* Not in final waitpid() loop,
			 * and G.running_procs < G.max_procs: start more procs
			 */
			status = spawn(G.args);
			/* here "status" actually holds pid, or -1 */
			if (status > 0) {
				G.running_procs++;
				status = 0;
			}
			/* else: status == -1 (failed to fork or exec) */
		} else {
			/* final waitpid() loop: must be ECHILD "no more children" */
			status = 0;
		}
	}
#endif
#endif

#if ENABLE_PLATFORM_MINGW32
	/* Any change to the logic for NOFORK applets must be duplicated
	 * in xargs_main() below. */
# if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	if (G.max_procs == 1) {
# endif
# if ENABLE_FEATURE_PREFER_APPLETS && (NUM_APPLETS > 1)
		int applet = find_applet_by_name(G.args[0]);
		if (applet >= 0 && APPLET_IS_NOFORK(applet)) {
			status = run_nofork_applet(applet, G.args);
		} else
# endif
		{
			G.pid = spawn(G.args);
			status = G.pid < 0 ? -1 : wait4pid(G.pid);
		}
# if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	}
	else {
		int idx;
		status = !G.running_procs && !G.max_procs ? 0 : wait_for_slot(&idx);
		if (G.max_procs) {
			HANDLE p = (HANDLE)mingw_spawn_proc((const char **)G.args);
			if (p < 0)
				status = -1;
			else
				G.procs[idx] = p;
		} else {
			while (G.running_procs) {
				int status2 = wait_for_slot(&idx);
				if (status2 && !status)
					status = status2;
			}
		}
	}
# endif
#endif
	/* Manpage:
	 * """xargs exits with the following status:
	 * 0 if it succeeds
	 * 123 if any invocation of the command exited with status 1-125
	 * 124 if the command exited with status 255
	 *     ("""If any invocation of the command exits with a status of 255,
	 *     xargs will stop immediately without reading any further input.
	 *     An error message is issued on stderr when this happens.""")
	 * 125 if the command is killed by a signal
	 * 126 if the command cannot be run
	 * 127 if the command is not found
	 * 1 if some other error occurred."""
	 */
	if (status < 0) {
		bb_simple_perror_msg(G.args[0]);
		status = (errno == ENOENT) ? 127 : 126;
	}
	else if (status >= 0x180) {
		bb_error_msg("'%s' terminated by signal %u",
			G.args[0], status - 0x180);
		status = 125;
	}
	else if (status != 0) {
		if (status == 255) {
			bb_error_msg("%s: exited with status 255; aborting", G.args[0]);
			status = 124;
			goto ret;
		}
		/* "123 if any invocation of the command exited with status 1-125"
		 * This implies that nonzero exit code is remembered,
		 * but does not cause xargs to stop: we return 0.
		 */
		G.xargs_exitcode = 123;
		status = 0;
	}
 ret:
	if (status != 0)
		G.xargs_exitcode = status;
#if !ENABLE_PLATFORM_MINGW32
	if (option_mask32 & OPT_STDIN_TTY)
		xdup2(G.fd_stdin, STDIN_FILENO);
#endif
	return status;
}

/* In POSIX/C locale isspace is only these chars: "\t\n\v\f\r" and space.
 * "\t\n\v\f\r" happen to have ASCII codes 9,10,11,12,13.
 */
#define ISSPACE(a) ({ unsigned char xargs__isspace = (a) - 9; xargs__isspace == (' ' - 9) || xargs__isspace <= (13 - 9); })

static void store_param(char *s)
{
	/* Grow by 256 elements at once */
	if (!(G.idx & 0xff)) { /* G.idx == N*256? */
		/* Enlarge, make G.args[(N+1)*256 - 1] last valid idx */
		G.args = xrealloc(G.args, sizeof(G.args[0]) * (G.idx + 0x100));
	}
	G.args[G.idx++] = s;
}

/* process[0]_stdin:
 * Read characters into buf[n_max_chars+1], and when parameter delimiter
 * is seen, store the address of a new parameter to args[].
 * If reading discovers that last chars do not form the complete
 * parameter, the pointer to the first such "tail character" is returned.
 * (buf has extra byte at the end to accommodate terminating NUL
 * of "tail characters" string).
 * Otherwise, the returned pointer points to NUL byte.
 * On entry, buf[] may contain some "seed chars" which are to become
 * the beginning of the first parameter.
 */

#if ENABLE_FEATURE_XARGS_SUPPORT_QUOTES
static char* FAST_FUNC process_stdin(int n_max_chars, int n_max_arg, char *buf)
{
#define q     G.process_stdin__q
#define state G.process_stdin__state
	char *s = buf;             /* start of the word */
	char *p = s + strlen(buf); /* end of the word */

	buf += n_max_chars;        /* past buffer's end */

	/* "goto ret" is used instead of "break" to make control flow
	 * more obvious: */

	while (1) {
		int c = getchar();
		if (c == EOF) {
			if (p != s)
				goto close_word;
			goto ret;
		}
		if (state == BACKSLASH) {
			state = NORM;
			goto set;
		}
		if (state == QUOTE) {
			if (c != q)
				goto set;
			q = '\0';
			state = NORM;
		} else { /* if (state == NORM) */
			if (ISSPACE(c)) {
				if (p != s) {
 close_word:
					state = SPACE;
					c = '\0';
					goto set;
				}
			} else {
				if (c == '\\') {
					state = BACKSLASH;
				} else if (c == '\'' || c == '"') {
					q = c;
					state = QUOTE;
				} else {
 set:
					*p++ = c;
				}
			}
		}
		if (state == SPACE) {   /* word's delimiter or EOF detected */
			state = NORM;
			if (q) {
				bb_error_msg_and_die("unmatched %s quote",
					q == '\'' ? "single" : "double");
			}
			/* A full word is loaded */
			if (G.eof_str) {
				if (strcmp(s, G.eof_str) == 0) {
					while (getchar() != EOF)
						continue;
					p = s;
					goto ret;
				}
			}
			store_param(s);
			dbg_msg("args[]:'%s'", s);
			s = p;
			n_max_arg--;
			if (n_max_arg == 0) {
				goto ret;
			}
		}
		if (p == buf) {
			goto ret;
		}
	}
 ret:
	*p = '\0';
	/* store_param(NULL) - caller will do it */
	dbg_msg("return:'%s'", s);
	return s;
#undef q
#undef state
}
#else
/* The variant does not support single quotes, double quotes or backslash */
static char* FAST_FUNC process_stdin(int n_max_chars, int n_max_arg, char *buf)
{
	char *s = buf;             /* start of the word */
	char *p = s + strlen(buf); /* end of the word */

	buf += n_max_chars;        /* past buffer's end */

	while (1) {
		int c = getchar();
		if (c == EOF) {
			if (p == s)
				goto ret;
		}
		if (c == EOF || ISSPACE(c)) {
			if (p == s)
				continue;
			c = EOF;
		}
		*p++ = (c == EOF ? '\0' : c);
		if (c == EOF) { /* word's delimiter or EOF detected */
			/* A full word is loaded */
			if (G.eof_str) {
				if (strcmp(s, G.eof_str) == 0) {
					while (getchar() != EOF)
						continue;
					p = s;
					goto ret;
				}
			}
			store_param(s);
			dbg_msg("args[]:'%s'", s);
			s = p;
			n_max_arg--;
			if (n_max_arg == 0) {
				goto ret;
			}
		}
		if (p == buf) {
			goto ret;
		}
	}
 ret:
	*p = '\0';
	/* store_param(NULL) - caller will do it */
	dbg_msg("return:'%s'", s);
	return s;
}
#endif /* FEATURE_XARGS_SUPPORT_QUOTES */

#if ENABLE_FEATURE_XARGS_SUPPORT_ZERO_TERM
static char* FAST_FUNC process0_stdin(int n_max_chars, int n_max_arg, char *buf)
{
	char *s = buf;             /* start of the word */
	char *p = s + strlen(buf); /* end of the word */

	buf += n_max_chars;        /* past buffer's end */

	while (1) {
		int c = getchar();
		if (c == EOF) {
			if (p == s)
				goto ret;
			c = '\0';
		}
		*p++ = c;
		if (c == '\0') {   /* NUL or EOF detected */
			/* A full word is loaded */
			store_param(s);
			dbg_msg("args[]:'%s'", s);
			s = p;
			n_max_arg--;
			if (n_max_arg == 0) {
				goto ret;
			}
		}
		if (p == buf) {
			goto ret;
		}
	}
 ret:
	*p = '\0';
	/* store_param(NULL) - caller will do it */
	dbg_msg("return:'%s'", s);
	return s;
}
#endif /* FEATURE_XARGS_SUPPORT_ZERO_TERM */

#if ENABLE_FEATURE_XARGS_SUPPORT_REPL_STR
/*
 * Used if -I<repl> was specified.
 * In this mode, words aren't appended to PROG ARGS.
 * Instead, entire input line is read, then <repl> string
 * in every PROG and ARG is replaced with the line:
 *  echo -e "ho ho\nhi" | xargs -I_ cmd __ _
 * results in "cmd 'ho hoho ho' 'ho ho'"; "cmd 'hihi' 'hi'".
 * -n MAX_ARGS seems to be ignored.
 * Tested with GNU findutils 4.5.10.
 */
//FIXME: n_max_chars is not handled the same way as in GNU findutils.
//FIXME: quoting is not implemented.
static char* FAST_FUNC process_stdin_with_replace(int n_max_chars, int n_max_arg UNUSED_PARAM, char *buf)
{
	int i;
	char *end, *p;

	/* Free strings from last invocation, if any */
	for (i = 0; G.args && G.args[i]; i++)
		if (G.args[i] != G.argv[i])
			free(G.args[i]);

	end = buf + n_max_chars;
	p = buf;

	while (1) {
		int c = getchar();
		if (p == buf) {
			if (c == EOF)
				goto ret; /* last line is empty, return "" */
			if (c == G.eol_ch)
				continue; /* empty line, ignore */
			/* Skip leading whitespace of each line: try
			 * echo -e ' \t\v1 2 3 ' | xargs -I% echo '[%]'
			 */
			if (ISSPACE(c))
				continue;
		}
		if (c == EOF || c == G.eol_ch) {
			c = '\0';
		}
		*p++ = c;
		if (c == '\0') {   /* EOL or EOF detected */
			i = 0;
			while (G.argv[i]) {
				char *arg = G.argv[i];
				int count = count_strstr(arg, G.repl_str);
				if (count != 0)
					arg = xmalloc_substitute_string(arg, count, G.repl_str, buf);
				store_param(arg);
				dbg_msg("args[]:'%s'", arg);
				i++;
			}
			p = buf;
			goto ret;
		}
		if (p == end) {
			goto ret;
		}
	}
 ret:
	*p = '\0';
	/* store_param(NULL) - caller will do it */
	dbg_msg("return:'%s'", buf);
	return buf;
}
#endif

#if ENABLE_FEATURE_XARGS_SUPPORT_CONFIRMATION
/* Prompt the user for a response, and
 * if user responds affirmatively, return true;
 * otherwise, return false. Uses "/dev/tty", not stdin.
 */
static int xargs_ask_confirmation(void)
{
#if !ENABLE_PLATFORM_MINGW32
	FILE *tty_stream;
	int r;

	tty_stream = xfopen_for_read(CURRENT_TTY);

	fputs(" ?...", stderr);
	r = bb_ask_y_confirmation_FILE(tty_stream);

	fclose(tty_stream);
#else
	int r, c, savec;

	fputs(" ?...", stderr);
	fflush_all();
	c = savec = getche();
	while (c != EOF && c != '\r')
		c = getche();
	fputs("\n", stderr);
	fflush_all();
	r = (savec == 'y' || savec == 'Y');
#endif

	return r;
}
#else
# define xargs_ask_confirmation() 1
#endif

#if ENABLE_PLATFORM_MINGW32
// Maximum command length (less a few bytes)
# define WIN32_MAX_CHARS (32750)

static size_t quote_len(const char *arg)
{
	char *s = quote_arg(arg);
	size_t len = strlen(s);

	free(s);
	return len;
}
#endif

//usage:#define xargs_trivial_usage
//usage:       "[OPTIONS] [PROG ARGS]"
//usage:#define xargs_full_usage "\n\n"
//usage:       "Run PROG on every item given by stdin\n"
//usage:	IF_FEATURE_XARGS_SUPPORT_ZERO_TERM(
//usage:     "\n	-0	NUL terminated input"
//usage:	)
//usage:	IF_FEATURE_XARGS_SUPPORT_ARGS_FILE(
//usage:     "\n	-a FILE	Read from FILE instead of stdin"
//usage:	)
//usage:	IF_NOT_PLATFORM_MINGW32(
//usage:     "\n	-o	Reopen stdin as /dev/tty"
//usage:	)
//usage:     "\n	-r	Don't run command if input is empty"
//usage:     "\n	-t	Print the command on stderr before execution"
//usage:	IF_FEATURE_XARGS_SUPPORT_CONFIRMATION(
//usage:     "\n	-p	Ask user whether to run each command"
//usage:	)
//usage:     "\n	-E STR,-e[STR]	STR stops input processing"
//usage:	IF_FEATURE_XARGS_SUPPORT_REPL_STR(
//usage:     "\n	-I STR	Replace STR within PROG ARGS with input line"
//usage:	)
//usage:     "\n	-n N	Pass no more than N args to PROG"
//usage:     "\n	-s N	Pass command line of no more than N bytes"
//usage:	IF_FEATURE_XARGS_SUPPORT_PARALLEL(
//usage:     "\n	-P N	Run up to N PROGs in parallel"
//usage:	)
//usage:	IF_FEATURE_XARGS_SUPPORT_TERMOPT(
//usage:     "\n	-x	Exit if size is exceeded"
//usage:	)
//usage:#define xargs_example_usage
//usage:       "$ ls | xargs gzip\n"
//usage:       "$ find . -name '*.c' -print | xargs rm\n"

int xargs_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int xargs_main(int argc UNUSED_PARAM, char **argv)
{
	int initial_idx;
	int i;
	char *max_args;
	char *max_chars;
	char *buf;
	unsigned opt;
	int n_max_chars;
	int n_max_arg;
#if ENABLE_PLATFORM_MINGW32
	int delta = 0;
	int quote = TRUE;
	char *old_buf = NULL;
#endif
#if ENABLE_FEATURE_XARGS_SUPPORT_ZERO_TERM \
 || ENABLE_FEATURE_XARGS_SUPPORT_REPL_STR
	char* FAST_FUNC (*read_args)(int, int, char*) = process_stdin;
#else
#define read_args process_stdin
#endif
	IF_FEATURE_XARGS_SUPPORT_ARGS_FILE(char *opt_a = NULL;)

	INIT_G();

#if ENABLE_PLATFORM_MINGW32
	SetConsoleCtrlHandler(ctrl_handler, TRUE);
#endif
	opt = getopt32long(argv, OPTION_STR,
		"no-run-if-empty\0" No_argument "r",
		&max_args, &max_chars, &G.eof_str, &G.eof_str
		IF_FEATURE_XARGS_SUPPORT_REPL_STR(, &G.repl_str, &G.repl_str)
		IF_FEATURE_XARGS_SUPPORT_PARALLEL(, &G.max_procs)
		IF_FEATURE_XARGS_SUPPORT_ARGS_FILE(, &opt_a)
	);

#if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	if (G.max_procs <= 0) /* -P0 means "run lots of them" */
#if !ENABLE_PLATFORM_MINGW32
		G.max_procs = 100; /* let's not go crazy high */
#else
		G.max_procs = MAXIMUM_WAIT_OBJECTS;
	G.procs = xmalloc(sizeof(G.procs[0]) * G.max_procs);
#endif
#endif

#if ENABLE_FEATURE_XARGS_SUPPORT_ARGS_FILE
	if (opt_a)
		xmove_fd(xopen(opt_a, O_RDONLY), 0);
#endif

	/* -E ""? You may wonder why not just omit -E?
	 * This is used for portability:
	 * old xargs was using "_" as default for -E / -e */
	if ((opt & OPT_EOF_STRING1) && G.eof_str[0] == '\0')
		G.eof_str = NULL;

	if (opt & OPT_ZEROTERM) {
		IF_FEATURE_XARGS_SUPPORT_ZERO_TERM(read_args = process0_stdin;)
		IF_FEATURE_XARGS_SUPPORT_REPL_STR(G.eol_ch = '\0';)
	}

	argv += optind;
	//argc -= optind;
	if (!argv[0]) {
		/* default behavior is to echo all the filenames */
		*--argv = (char*)"echo";
		//argc++;
	}

#if ENABLE_PLATFORM_MINGW32
	/* On Windows the command line may be expanded by the need to quote
	 * arguments, but not if the command is a NOFORK applet.  If the rules
	 * to detect this situation change xargs_exec() above will also need
	 * to be updated. */
# if ENABLE_FEATURE_PREFER_APPLETS && (NUM_APPLETS > 1)
#  if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	if (G.max_procs == 1)
#  endif
	{
		int applet = find_applet_by_name(argv[0]);
		if (applet >= 0 && APPLET_IS_NOFORK(applet)) {
			quote = FALSE;
		}
	}
# endif
#endif

	/*
	 * The Open Group Base Specifications Issue 6:
	 * "The xargs utility shall limit the command line length such that
	 * when the command line is invoked, the combined argument
	 * and environment lists (see the exec family of functions
	 * in the System Interfaces volume of IEEE Std 1003.1-2001)
	 * shall not exceed {ARG_MAX}-2048 bytes".
	 */
	n_max_chars = bb_arg_max();
	if (n_max_chars > 32 * 1024)
		n_max_chars = 32 * 1024;
	/*
	 * POSIX suggests substracting 2048 bytes from sysconf(_SC_ARG_MAX)
	 * so that the process may safely modify its environment.
	 */
	n_max_chars -= 2048;

	if (opt & OPT_UPTO_SIZE) {
		n_max_chars = xatou_range(max_chars, 1, INT_MAX);
	}
	/* Account for prepended fixed arguments */
	{
		size_t n_chars = 0;
		for (i = 0; argv[i]; i++) {
#if ENABLE_PLATFORM_MINGW32
			if (quote)
				n_chars += quote_len(argv[i]) + 1;
			else
#endif
				n_chars += strlen(argv[i]) + 1;
		}
		n_max_chars -= n_chars;
	}
	/* Sanity check */
	if (n_max_chars <= 0) {
		bb_simple_error_msg_and_die("can't fit single argument within argument list size limit");
	}

	buf = xzalloc(n_max_chars + 1);

	n_max_arg = n_max_chars;
	if (opt & OPT_UPTO_NUMBER) {
		n_max_arg = xatou_range(max_args, 1, INT_MAX);
		/* Not necessary, we use growable args[]: */
		/* if (n_max_arg > n_max_chars) n_max_arg = n_max_chars */
	}

#if ENABLE_FEATURE_XARGS_SUPPORT_REPL_STR
	if (opt & (OPT_REPLSTR | OPT_REPLSTR1)) {
		/*
		 * -I<str>:
		 * Unmodified args are kept in G.argv[i],
		 * G.args[i] receives malloced G.argv[i] with <str> replaced
		 * with input line. Setting this up:
		 */
		G.args = NULL;
		G.argv = argv;
		read_args = process_stdin_with_replace;
		/* Make -I imply -r. GNU findutils seems to do the same: */
		/* (otherwise "echo -n | xargs -I% echo %" would SEGV) */
		opt |= OPT_NO_EMPTY;
	} else
#endif
	{
		/* Store the command to be executed, part 1.
		 * We can statically allocate (argc + n_max_arg + 1) elements
		 * and do not bother with resizing args[], but on 64-bit machines
		 * this results in args[] vector which is ~8 times bigger
		 * than n_max_chars! That is, with n_max_chars == 20k,
		 * args[] will take 160k (!), which will most likely be
		 * almost entirely unused.
		 */
		for (i = 0; argv[i]; i++)
			store_param(argv[i]);
	}

#if !ENABLE_PLATFORM_MINGW32
	if (opt & OPT_STDIN_TTY) {
		G.fd_tty = xopen(CURRENT_TTY, O_RDONLY);
		close_on_exec_on(G.fd_tty);
		G.fd_stdin = dup(STDIN_FILENO);
		close_on_exec_on(G.fd_stdin);
	}
#endif

	initial_idx = G.idx;
	while (1) {
		char *rem;
#if ENABLE_PLATFORM_MINGW32
		char **args;
		char **tail = NULL;
		char *saved_arg = NULL;
		size_t n_chars;
#endif

		G.idx = initial_idx IF_PLATFORM_MINGW32(+ delta);
		rem = read_args(n_max_chars, n_max_arg, buf);
		store_param(NULL);

#if ENABLE_PLATFORM_MINGW32
		/* Check if quoting expands the command line.  If it does we
		 * truncate args[] and preserve the tail for processing later. */
		args = G.args;
		if (quote) {
 skip_read:
			n_chars = 0;
			for (i = initial_idx; args[i]; i++) {
				n_chars += quote_len(args[i]) + 1;
				if (n_chars > WIN32_MAX_CHARS) {
					tail = args + i;
					saved_arg = *tail;
					*tail = NULL;
					break;
				}
			}
		}
#endif

		if (!G.args[initial_idx]) { /* not even one ARG was added? */
			if (*rem != '\0')
				bb_simple_error_msg_and_die("argument line too long");
			if (opt & OPT_NO_EMPTY)
				break;
		}
		opt |= OPT_NO_EMPTY;

		if (opt & (OPT_INTERACTIVE | OPT_VERBOSE)) {
			const char *fmt = " %s" + 1;
#if !ENABLE_PLATFORM_MINGW32
			char **args = G.args;
#endif
			for (i = 0; args[i]; i++) {
				fprintf(stderr, fmt, args[i]);
				fmt = " %s";
			}
			if (!(opt & OPT_INTERACTIVE))
				bb_putchar_stderr('\n');
		}

		if (!(opt & OPT_INTERACTIVE) || xargs_ask_confirmation()) {
			if (xargs_exec() != 0)
				break; /* G.xargs_exitcode is set by xargs_exec() */
		}

#if ENABLE_PLATFORM_MINGW32
		delta = 0;
		if (quote && tail) {
			/* The command line was truncated.  Preload args[] with
			 * the tail we saved earlier. */
			*tail = saved_arg;
			n_chars = 0;
			for (i = 0; tail[i]; i++) {
				args[initial_idx + i] = tail[i];
				n_chars += quote_len(tail[i]) + 1;
			}
			args[initial_idx + i] = NULL;
			delta = i;

			/* The command line still overflows after quoting.
			 * Truncate the new args[] and exec it. */
			if (n_chars > WIN32_MAX_CHARS)
				goto skip_read;

			/* The first elements of args[] point to strings in the
			 * current buf, so we need to preserve it.  Allocate a
			 * new buf for future use. */
			free(old_buf);
			old_buf = buf;
			buf = xzalloc(n_max_chars + 1);
		}
#endif
		overlapping_strcpy(buf, rem);
	} /* while */

	if (ENABLE_FEATURE_CLEAN_UP) {
		free(G.args);
		free(buf);
	}
#if ENABLE_FEATURE_CLEAN_UP && ENABLE_PLATFORM_MINGW32
	free(old_buf);
#endif

#if ENABLE_FEATURE_XARGS_SUPPORT_PARALLEL
	G.max_procs = 0;
	xargs_exec(); /* final waitpid() loop */
#endif

	return G.xargs_exitcode;
}


#ifdef TEST

const char *applet_name = "debug stuff usage";

void bb_show_usage(void)
{
	fprintf(stderr, "Usage: %s [-p] [-r] [-t] -[x] [-n max_arg] [-s max_chars]\n",
		applet_name);
	exit_FAILURE();
}

int main(int argc, char **argv)
{
	return xargs_main(argc, argv);
}
#endif /* TEST */
