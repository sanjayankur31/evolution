/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *  Radek Doulik <rodo@ximian.com>
 *
 * Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-debug.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-data-wrapper.h>
#include <camel/camel-stream-fs.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-i18n.h>
#include <mail/em-junk-hook.h>
#include <mail/em-utils.h>
#include <e-util/e-mktemp.h>

#include <gtk/gtk.h>
#include "mail/em-config.h"

#include <gconf/gconf-client.h>

#define d(x) (camel_debug("junk")?(x):0)

static pthread_mutex_t em_junk_sa_init_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t em_junk_sa_report_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t em_junk_sa_preferred_socket_path_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t em_junk_sa_spamd_restart_lock = PTHREAD_MUTEX_INITIALIZER;

int e_plugin_lib_enable (EPluginLib *ep, int enable);
//static const char *em_junk_sa_get_name (void);
gboolean em_junk_sa_check_junk (EPlugin *ep, EMJunkHookTarget *target);
void em_junk_sa_report_junk (EPlugin *ep, EMJunkHookTarget *target);
void em_junk_sa_report_non_junk (EPlugin *ep, EMJunkHookTarget *target);
void em_junk_sa_commit_reports (EPlugin *ep, EMJunkHookTarget *target);
void *em_junk_sa_validate_binary (EPlugin *ep, EMJunkHookTarget *target);
GtkWidget *org_gnome_sa_use_remote_tests (struct _EPlugin *epl, struct _EConfigHookItemFactoryData *data);

static void em_junk_sa_init (void);
static void em_junk_sa_finalize (void);
static void em_junk_sa_kill_spamd (void);


static gboolean em_junk_sa_tested = FALSE;
static gboolean em_junk_sa_spamd_tested = FALSE;
static gboolean em_junk_sa_use_spamc = FALSE;
static gboolean em_junk_sa_available = FALSE;
static gboolean em_junk_sa_system_spamd_available = FALSE;
static gboolean em_junk_sa_new_daemon_started = FALSE;
static gboolean em_junk_sa_checked_spamassassin_version = FALSE;
static guint em_junk_sa_spamassassin_version = 0;
static char *em_junk_sa_socket_path = NULL;
static char *em_junk_sa_spamd_pidfile = NULL;
static char *em_junk_sa_spamc_binary = NULL;
static GConfClient *em_junk_sa_gconf = NULL;

/* volatile so not cached between threads */
static volatile gboolean em_junk_sa_local_only;
static volatile gboolean em_junk_sa_use_daemon;
static char * em_junk_sa_preferred_socket_path;

static char *em_junk_sa_spamc_binaries [3] = {"spamc", "/usr/sbin/spamc", NULL};
static char *em_junk_sa_spamd_binaries [3] = {"spamd", "/usr/sbin/spamd", NULL};

#define SPAMD_RESTARTS_SIZE 8
static time_t em_junk_sa_spamd_restarts [SPAMD_RESTARTS_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0};
static int em_junk_sa_spamd_restarts_count = 0;

char *em_junk_sa_spamc_gconf_binary = NULL;
char *em_junk_sa_spamd_gconf_binary = NULL;

#if 0
static const char *
em_junk_sa_get_name (void)
{
	return _("SpamAssassin (built-in)");
}
#endif

static int
pipe_to_sa_full (CamelMimeMessage *msg, const char *in, char **argv, int rv_err, int wait_for_termination, GByteArray *output_buffer)
{
	int result, status, errnosav, fds[2], out_fds[2];
	CamelStream *stream;
	char *program;
	pid_t pid;


	if (camel_debug_start ("junk")) {
		int i;

		printf ("pipe_to_sa ");
		for (i = 0; argv[i]; i++)
			printf ("%s ", argv[i]);
		printf ("\n");
		camel_debug_end ();
	}

	program = g_find_program_in_path (argv [0]);
	if (program == NULL) {
		d(printf ("program not found, returning %d\n", rv_err));
		return rv_err;
	}
	g_free (program);

	if (pipe (fds) == -1) {
		errnosav = errno;
		d(printf ("failed to create a pipe (for use with spamassassin: %s\n", strerror (errno)));
		errno = errnosav;
		return rv_err;
	}

	if (output_buffer && pipe (out_fds) == -1) {
		errnosav = errno;
		d(printf ("failed to create a pipe (for use with spamassassin: %s\n", strerror (errno)));
		close (fds [0]);
		close (fds [1]);
		errno = errnosav;
		return rv_err;
	}

	if (!(pid = fork ())) {
		/* child process */
		int maxfd, fd, nullfd;

		nullfd = open ("/dev/null", O_WRONLY);

		if (dup2 (fds[0], STDIN_FILENO) == -1 ||
		    dup2 (nullfd, STDERR_FILENO) == -1 ||
		    (output_buffer == NULL && dup2 (nullfd, STDOUT_FILENO) == -1) ||
		    (output_buffer != NULL && dup2 (out_fds[1], STDOUT_FILENO) == -1))
			_exit (rv_err & 0377);
		close (fds [0]);
		if (output_buffer)
			close (out_fds [1]);

		setsid ();

		maxfd = sysconf (_SC_OPEN_MAX);
		for (fd = 3; fd < maxfd; fd++)
			fcntl (fd, F_SETFD, FD_CLOEXEC);

		execvp (argv[0], argv);
		_exit (rv_err & 0377);
	} else if (pid < 0) {
		errnosav = errno;
		close (fds[0]);
		close (fds[1]);
		if (output_buffer) {
			close (out_fds [0]);
			close (out_fds [1]);
		}
		errno = errnosav;
		return rv_err;
	}

	/* parent process */
	close (fds[0]);
	if (output_buffer)
		close (out_fds [1]);

	if (msg) {
		stream = camel_stream_fs_new_with_fd (fds[1]);

		camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (msg), stream);
		camel_stream_flush (stream);
		camel_stream_close (stream);
		camel_object_unref (stream);
	} else if (in) {
		camel_write (fds[1], in, strlen (in));
		close (fds[1]);
	}

	if (output_buffer) {
		CamelStreamMem *memstream;

		stream = camel_stream_fs_new_with_fd (out_fds[0]);

		memstream = (CamelStreamMem *) camel_stream_mem_new ();
		camel_stream_mem_set_byte_array (memstream, output_buffer);

		camel_stream_write_to_stream (stream, (CamelStream *) memstream);
		camel_object_unref (stream);
		g_byte_array_append (output_buffer, (unsigned char *)"", 1);

		d(printf ("child process output: %s len: %d\n", output_buffer->data, output_buffer->len));
	}

	if (wait_for_termination) {
		d(printf ("wait for child %d termination\n", pid));
		result = waitpid (pid, &status, 0);

		d(printf ("child %d terminated with result %d status %d exited %d exitstatus %d\n", pid, result, status, WIFEXITED (status), WEXITSTATUS (status)));

		if (result == -1 && errno == EINTR) {
			/* child process is hanging... */
			kill (pid, SIGTERM);
			sleep (1);
			result = waitpid (pid, &status, WNOHANG);
			if (result == 0) {
				/* ...still hanging, set phasers to KILL */
				kill (pid, SIGKILL);
				sleep (1);
				result = waitpid (pid, &status, WNOHANG);
			}
		}

		if (result != -1 && WIFEXITED (status))
			return WEXITSTATUS (status);
		else
			return rv_err;
	} else
		return 0;
}

static int
pipe_to_sa (CamelMimeMessage *msg, const char *in, char **argv)
{
	return pipe_to_sa_full (msg, in, argv, -1, 1, NULL);
}

static char *
em_junk_sa_get_socket_path ()
{
	if (em_junk_sa_preferred_socket_path)
		return em_junk_sa_preferred_socket_path;
	else
		return em_junk_sa_socket_path;
}

static gboolean
em_junk_sa_test_spamd_running (char *binary, gboolean system)
{
	char *argv[5];
	int i = 0;
	gboolean rv;

	pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);

	d(fprintf (stderr, "test if spamd is running (system %d) or using socket path %s\n", system, em_junk_sa_get_socket_path ()));

	argv[i++] = binary;
	argv[i++] = "-x";

	if (!system) {
		argv[i++] = "-U";
		argv[i++] = em_junk_sa_get_socket_path ();
	}

	argv[i] = NULL;

	rv = pipe_to_sa (NULL, "From test@127.0.0.1", argv) == 0;

	d(fprintf (stderr, "result: %d (%s)\n", rv, rv ? "success" : "failed"));

	pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);

	return rv;
}

static void
em_junk_sa_test_spamassassin (void)
{
	char *argv [3] = {
		"spamassassin",
		"--version",
		NULL,
	};

	if (pipe_to_sa (NULL, NULL, argv) != 0)
		em_junk_sa_available = FALSE;
	else
		em_junk_sa_available = TRUE;

	em_junk_sa_tested = TRUE;
}

#define MAX_SPAMD_PORTS 1

static gboolean
em_junk_sa_run_spamd (char *binary)
{
	char *argv[8];
	int i;
	gboolean rv = FALSE;

	pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);

	d(fprintf (stderr, "looks like spamd is not running\n"));

	i = 0;
	argv[i++] = binary;
	argv[i++] = "--socketpath";
	argv[i++] = em_junk_sa_get_socket_path ();

	if (em_junk_sa_local_only)
		argv[i++] = "--local";

	/* See bug #268852*/
	argv[i++] = "--max-children=1";
	/*argv[i++] = "--daemonize";*/
	argv[i++] = "--pidfile";
	argv[i++] = em_junk_sa_spamd_pidfile;
	argv[i] = NULL;

	d(fprintf (stderr, "trying to run %s with socket path %s\n", binary, em_junk_sa_get_socket_path ()));

	if (!pipe_to_sa_full (NULL, NULL, argv, -1, 0, NULL)) {
		struct timespec time_req;
		struct stat stat_buf;

		d(fprintf (stderr, "success\n"));
		d(fprintf (stderr, "waiting for spamd to come up\n"));

		time_req.tv_sec = 0;
		time_req.tv_nsec = 50000000;

		for (i = 0; i < 100; i ++) {
			if (stat (em_junk_sa_get_socket_path (), &stat_buf) == 0) {
				d(fprintf (stderr, "socket created\n"));
				break;
			}
			nanosleep (&time_req, NULL);
		}
		d(fprintf (stderr, "waiting is over (after %dms)\n", 50*i));

		rv = TRUE;
	}

	pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);

	return rv;
}

static void
em_junk_sa_start_own_daemon ()
{
	int b;

	em_junk_sa_new_daemon_started = FALSE;

	em_junk_sa_socket_path = e_mktemp ("spamd-socket-path-XXXXXX");
	em_junk_sa_spamd_pidfile = e_mktemp ("spamd-pid-file-XXXXXX");

	for (b = 0; em_junk_sa_spamd_binaries [b]; b ++) {
		em_junk_sa_use_spamc = em_junk_sa_run_spamd (em_junk_sa_spamd_binaries [b]);
		if (em_junk_sa_use_spamc) {
			em_junk_sa_new_daemon_started = TRUE;
			break;
		}
	}
}

static void
em_junk_sa_find_spamc ()
{
	if (em_junk_sa_use_spamc && em_junk_sa_new_daemon_started) {
		int b;

		em_junk_sa_use_spamc = FALSE;
		for (b = 0; em_junk_sa_spamc_binaries [b]; b ++) {
			em_junk_sa_spamc_binary = em_junk_sa_spamc_binaries [b];
			if (em_junk_sa_test_spamd_running (em_junk_sa_spamc_binary, FALSE)) {
				em_junk_sa_use_spamc = TRUE;
				break;
			}
		}
	}
}

static void
em_junk_sa_test_spamd (void)
{
	char *argv[4];
	int i, b;
	gboolean try_system_spamd = TRUE;

	if (em_junk_sa_spamc_gconf_binary) {
		em_junk_sa_spamc_binaries [0] = em_junk_sa_spamc_gconf_binary;
		em_junk_sa_spamc_binaries [1] = NULL;
	}

	if (em_junk_sa_spamd_gconf_binary) {
		em_junk_sa_spamd_binaries [0] = em_junk_sa_spamd_gconf_binary;
		em_junk_sa_spamd_binaries [1] = NULL;
		try_system_spamd = FALSE;
  	}

	em_junk_sa_use_spamc = FALSE;

	if (em_junk_sa_local_only && try_system_spamd) {
		   i = 0;
		   argv [i++] = "/bin/sh";
		   argv [i++] = "-c";
		   argv [i++] = "ps ax|grep -v grep|grep -E 'spamd.*(\\-L|\\-\\-local)'|grep -E -v '\\ \\-p\\ |\\ \\-\\-port\\ '";
		   argv[i] = NULL;

		   if (pipe_to_sa (NULL, NULL, argv) != 0) {
			   try_system_spamd = FALSE;
			   d(fprintf (stderr, "there's no system spamd with -L/--local parameter running\n"));
		   }
	}

	/* try to use sytem spamd first */
	if (try_system_spamd) {
		for (b = 0; em_junk_sa_spamc_binaries [b]; b ++) {
			em_junk_sa_spamc_binary = em_junk_sa_spamc_binaries [b];
			if (em_junk_sa_test_spamd_running (em_junk_sa_spamc_binary, TRUE)) {
				em_junk_sa_use_spamc = TRUE;
				em_junk_sa_system_spamd_available = TRUE;
				break;
			}
		}
	}

	/* if there's no system spamd running, try to use user one with user specified socket */
	if (!em_junk_sa_use_spamc && em_junk_sa_preferred_socket_path) {
		for (b = 0; em_junk_sa_spamc_binaries [b]; b ++) {
			em_junk_sa_spamc_binary = em_junk_sa_spamc_binaries [b];
			if (em_junk_sa_test_spamd_running (em_junk_sa_spamc_binary, FALSE)) {
				em_junk_sa_use_spamc = TRUE;
				em_junk_sa_system_spamd_available = FALSE;
				break;
			}
		}
	}

	/* unsuccessful? try to run one ourselfs */
	if (!em_junk_sa_use_spamc)
		em_junk_sa_start_own_daemon ();

	/* new daemon started => let find spamc binary */
	em_junk_sa_find_spamc ();

	d(fprintf (stderr, "use spamd: %s\n", em_junk_sa_use_spamc ? "yes" : "no"));

	em_junk_sa_spamd_tested = TRUE;
}

static gboolean
em_junk_sa_is_available (void)
{
	pthread_mutex_lock (&em_junk_sa_init_lock);

	if (!em_junk_sa_tested)
		em_junk_sa_test_spamassassin ();

	if (em_junk_sa_available && !em_junk_sa_spamd_tested && em_junk_sa_use_daemon)
		em_junk_sa_test_spamd ();

	pthread_mutex_unlock (&em_junk_sa_init_lock);

	return em_junk_sa_available;
}

static gboolean
em_junk_sa_check_respawn_too_fast ()
{
	time_t time_now = time (NULL);
	gboolean rv;

	pthread_mutex_lock (&em_junk_sa_spamd_restart_lock);

	if (em_junk_sa_spamd_restarts_count >= SPAMD_RESTARTS_SIZE) {
		/* all restarts in last 5 minutes */
		rv = (time_now - em_junk_sa_spamd_restarts [em_junk_sa_spamd_restarts_count % SPAMD_RESTARTS_SIZE] < 5*60);
	} else
		rv = FALSE;

	em_junk_sa_spamd_restarts [em_junk_sa_spamd_restarts_count % SPAMD_RESTARTS_SIZE] = time_now;
	em_junk_sa_spamd_restarts_count ++;

	pthread_mutex_unlock (&em_junk_sa_spamd_restart_lock);

	d(printf ("em_junk_sa_check_respawn_too_fast: %d\n", rv));

	return rv;
}

static gboolean
em_junk_sa_respawn_spamd ()
{
	d(printf ("em_junk_sa_respawn_spamd\n"));
	if (em_junk_sa_test_spamd_running (em_junk_sa_spamc_binary, em_junk_sa_system_spamd_available)) {
		/* false alert */
		d(printf ("false alert, spamd still running\n"));

		return FALSE;
	}

	d(printf ("going to kill old spamd and start new one\n"));
	em_junk_sa_kill_spamd ();

	if (em_junk_sa_check_respawn_too_fast ()) {
		g_warning ("respawning of spamd too fast => fallback to use spamassassin directly");

		em_junk_sa_use_spamc = em_junk_sa_use_daemon = FALSE;
		return FALSE;
	}

	em_junk_sa_start_own_daemon ();
	em_junk_sa_find_spamc ();

	d(printf ("%s\n", em_junk_sa_use_spamc ? "success" : "failed"));

	return em_junk_sa_use_spamc;
}

gboolean
em_junk_sa_check_junk(EPlugin *ep, EMJunkHookTarget *target)
{
	GByteArray *out = NULL;
	char *argv[7], *to_free = NULL;
	int i = 0, socket_i;
	gboolean rv;
	CamelMimeMessage *msg = target->m;

	d(fprintf (stderr, "em_junk_sa_check_junk\n"));

	if (!em_junk_sa_is_available ())
		return FALSE;

	if (em_junk_sa_use_spamc && em_junk_sa_use_daemon) {
		out = g_byte_array_new ();
		argv[i++] = em_junk_sa_spamc_binary;
		argv[i++] = "-c";
		argv[i++] = "-t";
		argv[i++] = "60";
		if (!em_junk_sa_system_spamd_available) {
			argv[i++] = "-U";

			pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);
			socket_i = i;
			argv[i++] = to_free = g_strdup (em_junk_sa_get_socket_path ());
			pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);
		}
	} else {
		argv [i++] = "spamassassin";
		argv [i++] = "--exit-code";
		if (em_junk_sa_local_only)
			argv [i++] = "--local";
	}

	argv[i] = NULL;

	rv = pipe_to_sa_full (msg, NULL, argv, 0, 1, out) != 0;

	if (!rv && out && !strcmp ((const char *)out->data, "0/0\n")) {
		/* an error occurred */
		if (em_junk_sa_respawn_spamd ()) {
			g_byte_array_set_size (out, 0);

			pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);
			g_free (to_free);
			argv [socket_i] = to_free = g_strdup (em_junk_sa_get_socket_path ());
			pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);

			rv = pipe_to_sa_full (msg, NULL, argv, 0, 1, out) != 0;
		} else if (!em_junk_sa_use_spamc)
			/* in case respawning were too fast we fallback to spamassassin */
			rv = em_junk_sa_check_junk (ep, target);
	}

	g_free (to_free);

	d(fprintf (stderr, "em_junk_sa_check_junk rv = %d\n", rv));

	if (out)
		g_byte_array_free (out, TRUE);

	return rv;
}

static guint
get_spamassassin_version ()
{
	GByteArray *out = NULL;
	int i;

	char * argv[3] = {
		"sa-learn",
		"--version",
		NULL
	};

	if (!em_junk_sa_checked_spamassassin_version){
		out = g_byte_array_new ();

		if (pipe_to_sa_full (NULL, NULL, argv, -1, 1, out) != 0){
			if(out)
				g_byte_array_free (out, TRUE);
			return em_junk_sa_spamassassin_version;
		}

		if(out->len > 0){
			for(i = 0; i < out->len; i++){
				if(g_ascii_isdigit (out->data[i])){
					em_junk_sa_spamassassin_version = (out->data[i] - '0');
					em_junk_sa_checked_spamassassin_version = TRUE;
					break;
				}
			}
		}


		if(out)
			g_byte_array_free (out, TRUE);
	}

	return em_junk_sa_spamassassin_version;
}

void
em_junk_sa_report_junk (EPlugin *ep, EMJunkHookTarget *target)
{
	char *sync_op = ((get_spamassassin_version () >= 3) ? "--no-sync": "--no-rebuild");
	char *argv[6] = {
		"sa-learn",
		sync_op,
		"--spam",
		"--single",
		NULL,
		NULL
	};
	gchar *sub = NULL;
	CamelMimeMessage *msg = target->m;

	sub = g_strdup (camel_mime_message_get_subject (msg));
	g_print ("\nreport junk?? %s\n", sub);

	d(fprintf (stderr, "em_junk_sa_report_junk\n"));

	if (em_junk_sa_is_available ()) {
		if (em_junk_sa_local_only)
			argv[4] = "--local";

		pthread_mutex_lock (&em_junk_sa_report_lock);
		pipe_to_sa (msg, NULL, argv);
		pthread_mutex_unlock (&em_junk_sa_report_lock);
	}
}

void
em_junk_sa_report_non_junk (EPlugin *ep, EMJunkHookTarget *target)
{
	char *sync_op = ((get_spamassassin_version () >= 3) ? "--no-sync": "--no-rebuild");
	char *argv[6] = {
		"sa-learn",
		sync_op,
		"--ham",
		"--single",
		NULL,
		NULL
	};
	CamelMimeMessage *msg = target->m;

	d(fprintf (stderr, "em_junk_sa_report_notjunk\n"));

	if (em_junk_sa_is_available ()) {
		if (em_junk_sa_local_only)
			argv[4] = "--local";

		pthread_mutex_lock (&em_junk_sa_report_lock);
		pipe_to_sa (msg, NULL, argv);
		pthread_mutex_unlock (&em_junk_sa_report_lock);
	}
}

void
em_junk_sa_commit_reports (EPlugin *ep, EMJunkHookTarget *target)
{
	char *sync_op = ((get_spamassassin_version () >= 3) ? "--sync": "--rebuild");
	char *argv[4] = {
		"sa-learn",
		sync_op,
		NULL,
		NULL
	};

	d(fprintf (stderr, "em_junk_sa_commit_reports\n"));

	if (em_junk_sa_is_available ()) {
		if (em_junk_sa_local_only)
			argv[2] = "--local";

		pthread_mutex_lock (&em_junk_sa_report_lock);
		pipe_to_sa (NULL, NULL, argv);
		pthread_mutex_unlock (&em_junk_sa_report_lock);
	}
}

void *
em_junk_sa_validate_binary (EPlugin *ep, EMJunkHookTarget *target)
{
	return em_junk_sa_is_available () ? "1" : NULL;
}

static void
em_junk_sa_setting_notify(GConfClient *gconf, guint cnxn_id, GConfEntry *entry, void *data)
{
	GConfValue *value;
	char *tkey;

	g_return_if_fail (gconf_entry_get_key (entry) != NULL);

	if (!(value = gconf_entry_get_value (entry)))
		return;

	tkey = strrchr(entry->key, '/');
	g_return_if_fail (tkey != NULL);

	if (!strcmp(tkey, "local_only"))
		em_junk_sa_local_only = gconf_value_get_bool(value);
	else if (!strcmp(tkey, "use_daemon"))
		em_junk_sa_use_daemon = gconf_value_get_bool(value);
	else if (!strcmp(tkey, "socket_path")) {
		pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);
		g_free (em_junk_sa_preferred_socket_path);
		em_junk_sa_preferred_socket_path = g_strdup (gconf_value_get_string(value));
		pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);
	}
}

int
e_plugin_lib_enable (EPluginLib *ep, int enable)
{
	em_junk_sa_init();

	return 0;
}

static void
em_junk_sa_init (void)
{
	pthread_mutex_lock (&em_junk_sa_init_lock);

	if (!em_junk_sa_gconf) {
		em_junk_sa_gconf = gconf_client_get_default();
		gconf_client_add_dir (em_junk_sa_gconf, "/apps/evolution/mail/junk/sa", GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

		em_junk_sa_local_only = gconf_client_get_bool (em_junk_sa_gconf, "/apps/evolution/mail/junk/sa/local_only", NULL);
		em_junk_sa_use_daemon = gconf_client_get_bool (em_junk_sa_gconf, "/apps/evolution/mail/junk/sa/use_daemon", NULL);

		pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);
		g_free (em_junk_sa_preferred_socket_path);
		em_junk_sa_preferred_socket_path = gconf_client_get_string (em_junk_sa_gconf, "/apps/evolution/mail/junk/sa/socket_path", NULL);
		pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);

		gconf_client_notify_add(em_junk_sa_gconf, "/apps/evolution/mail/junk/sa",
					(GConfClientNotifyFunc)em_junk_sa_setting_notify,
					NULL, NULL, NULL);

		em_junk_sa_spamc_gconf_binary = gconf_client_get_string (em_junk_sa_gconf, "/apps/evolution/mail/junk/sa/spamc_binary", NULL);
		em_junk_sa_spamd_gconf_binary = gconf_client_get_string (em_junk_sa_gconf, "/apps/evolution/mail/junk/sa/spamd_binary", NULL);
	}

	pthread_mutex_unlock (&em_junk_sa_init_lock);

	atexit (em_junk_sa_finalize);
}

static void
em_junk_sa_kill_spamd (void)
{
	pthread_mutex_lock (&em_junk_sa_preferred_socket_path_lock);
	g_free (em_junk_sa_preferred_socket_path);
	em_junk_sa_preferred_socket_path = NULL;
	pthread_mutex_unlock (&em_junk_sa_preferred_socket_path_lock);

	if (em_junk_sa_new_daemon_started) {
		int fd = open (em_junk_sa_spamd_pidfile, O_RDONLY);

		if (fd != -1) {
			char pid_str [16];
			int bytes;

			bytes = read (fd, pid_str, 15);
			if (bytes > 0) {
				int pid;

				pid_str [bytes] = 0;
				pid = atoi (pid_str);

				if (pid > 0) {
					kill (pid, SIGTERM);
					d(fprintf (stderr, "em_junk_sa_finalize send SIGTERM to daemon with pid %d\n", pid));
					waitpid (pid, NULL, 0);
				}
			}

			close (fd);
		}
	}
}

static void
em_junk_sa_finalize (void)
{
	d(fprintf (stderr, "em_junk_sa_finalize\n"));

	g_object_unref(em_junk_sa_gconf);
	em_junk_sa_kill_spamd ();
}

static void
use_remote_tests_cb (GtkWidget *widget, gpointer data)
{
	gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
   	gconf_client_set_bool (em_junk_sa_gconf, data, !active, NULL);
}

GtkWidget *
org_gnome_sa_use_remote_tests (struct _EPlugin *epl, struct _EConfigHookItemFactoryData *data)
{
   	GtkWidget *check, *vbox, *label;
	char *text = g_strdup_printf ("    <small>%s</small>", _("This will make SpamAssassin more reliable, but slower"));
	guint i = ((GtkTable *)data->parent)->nrows;

	if (data->old)
                return data->old;

	check = gtk_check_button_new_with_mnemonic (_("I_nclude remote tests"));
	label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (label), text);
	g_free (text);
	vbox = gtk_vbox_new (FALSE, 2);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (check), FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (label), FALSE, FALSE, 0);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), !em_junk_sa_local_only);
	g_signal_connect (GTK_TOGGLE_BUTTON (check), "toggled", G_CALLBACK (use_remote_tests_cb), "/apps/evolution/mail/junk/sa/local_only");
	gtk_table_attach((GtkTable *)data->parent, vbox, 0, 1, i, i+1, 0, 0, 0, 0);
	gtk_widget_show_all (vbox);
	return (GtkWidget *)vbox;
}


