#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <utmp.h>
#include <sys/reboot.h>
#include <sys/syslog.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#define CONSOLE		"/dev/console"
#define PATH_DEFAULT   "/sbin:/usr/sbin:/bin:/usr/bin"
#define SHELL "/bin/bash"
#define SYSINIT "/etc/sysinit"
#define LOGIN "/sbin/sulogin"

char *console_dev = CONSOLE;

pid_t login_pid;

static void initlog (char *s);

static int
console_open (int mode)
{
	int f, fd = -1;

	int m;

	/*
	 *  Open device in nonblocking mode.
	 */
	m = mode | O_NONBLOCK;

	/*
	 *  Retry the open five times.
	 */
	for (f = 0; f < 5; f++)
	{
		if ((fd = open (console_dev, m)) >= 0)
			break;
		usleep (10000);
	}

	if (fd < 0)
		return fd;

	/*
	 *  Set original flags.
	 */
	if (m != mode)
		fcntl (fd, F_SETFL, mode);
	return fd;
}

static void
console_stty (void)
{
	struct termios tty;

	int fd;

	if ((fd = console_open (O_RDWR | O_NOCTTY)) < 0)
	{
		initlog ("can't open console!");
		return;
	}

	(void) tcgetattr (fd, &tty);

	tty.c_cflag &= CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD;
	tty.c_cflag |= HUPCL | CLOCAL | CREAD;

	tty.c_cc[VINTR] = CINTR;
	tty.c_cc[VQUIT] = CQUIT;
	tty.c_cc[VERASE] = CERASE;	/* ASCII DEL (0177) */
	tty.c_cc[VKILL] = CKILL;
	tty.c_cc[VEOF] = CEOF;
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VSWTC] = _POSIX_VDISABLE;
	tty.c_cc[VSTART] = CSTART;
	tty.c_cc[VSTOP] = CSTOP;
	tty.c_cc[VSUSP] = CSUSP;
	tty.c_cc[VEOL] = _POSIX_VDISABLE;
	tty.c_cc[VREPRINT] = CREPRINT;
	tty.c_cc[VDISCARD] = CDISCARD;
	tty.c_cc[VWERASE] = CWERASE;
	tty.c_cc[VLNEXT] = CLNEXT;
	tty.c_cc[VEOL2] = _POSIX_VDISABLE;

	/*
	 *  Set pre and post processing
	 */
	tty.c_iflag = IGNPAR | ICRNL | IXON | IXANY;
#ifdef IUTF8					/* Not defined on FreeBSD */
	tty.c_iflag |= IUTF8;
#endif							/* IUTF8 */
	tty.c_oflag = OPOST | ONLCR;
	tty.c_lflag = ISIG | ICANON | ECHO | ECHOCTL | ECHOPRT | ECHOKE;

#if defined(SANE_TIO) && (SANE_TIO == 1)
	/*
	 *  Disable flow control (-ixon), ignore break (ignbrk),
	 *  and make nl/cr more usable (sane).
	 */
	tty.c_iflag |= IGNBRK;
	tty.c_iflag &= ~(BRKINT | INLCR | IGNCR | IXON);
	tty.c_oflag &= ~(OCRNL | ONLRET);
#endif
	/*
	 *  Now set the terminal line.
	 *  We don't care about non-transmitted output data
	 *  and non-read input data.
	 */
	(void) tcsetattr (fd, TCSANOW, &tty);
	(void) tcflush (fd, TCIOFLUSH);
	(void) close (fd);
}

static void
print (char *s)
{
	int fd;

	if ((fd = console_open (O_WRONLY | O_NOCTTY | O_NDELAY)) >= 0)
	{
		write (fd, s, strlen (s));
		close (fd);
	}
}

static void
initlog (char *s)
{
	print ("\rINIT: ");
	print (s);
	print ("\r\n");
}

static void
start_login ()
{
	int fd, status;

	login_pid = fork ();
	if (login_pid == 0)
	{
		setsid ();
		if ((fd = console_open (O_RDWR | O_NOCTTY)) >= 0)
		{
			/* Take over controlling tty by force */
			(void) ioctl (fd, TIOCSCTTY, 1);
			dup (fd);
			dup (fd);
		}

		execv (LOGIN, NULL);
	}
}


static void
sysinit ()
{
	int status, fd;

	if (fork () == 0)
	{
		setsid ();
		if ((fd = console_open (O_RDWR | O_NOCTTY)) >= 0)
		{
			/* Take over controlling tty by force */
			(void) ioctl (fd, TIOCSCTTY, 1);
			dup (fd);
			dup (fd);
		}
		execl (SHELL, "bash", SYSINIT, NULL);
		exit (1);
	}

	wait (&status);

	if (status > 0)
	{
		initlog ("sysinit error!");
	}
}

static void
kill_all_processes (void)
{
	initlog ("The system is going down NOW!");

	/* Allow Ctrl-Alt-Del to reboot system. */
	reboot (RB_ENABLE_CAD);

	/* Send signals to every process _except_ pid 1 */
	initlog ("Sending SIGTERM to all processes");
	kill (-1, SIGTERM);
	sync ();
	sleep (1);

	initlog ("Sending SIGKILL to all processes");
	kill (-1, SIGKILL);
	sync ();
	sleep (1);
	kill (1, SIGTERM);
}

static void
signal_handler (int sig)
{
	kill (login_pid, -9);
	sleep (1);
	kill_all_processes ();
	if (sig == SIGUSR1)
	{
		reboot (RB_AUTOBOOT);
	}
	else
	{
		reboot (RB_POWER_OFF);
	}
}

int
main (void)
{
	struct sigaction sa;

	int f, i, fd, status;

	pid_t pid;;

	umask (022);
	klogctl (6, NULL, 0);

	if ((getpid ()) != 1)
	{
		exit (1);
	}

	/*
	 *  Ignore all signals.
	 */
	for (f = 1; f <= NSIG; f++)
		signal (f, SIG_IGN);
	signal (SIGCHLD, SIG_DFL);
	signal (SIGUSR1, signal_handler);
	signal (SIGUSR2, signal_handler);
	/* Close whatever files are open, and reset the console. */
	close (0);
	close (1);
	close (2);
	console_stty ();
	setsid ();

	/*
	 *  Set default PATH variable.
	 */
	setenv ("PATH", PATH_DEFAULT, 1 /* Overwrite */ );

	/*
	 *  Initialize /var/run/utmp (only works if /var is on
	 *  root and mounted rw)
	 */
	if ((fd = open (UTMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0)
		close (fd);

	/*
	 *  Say hello to the world
	 */
	initlog ("Welcome to \033[34m LFS-Linux\033[0m");
	sysinit ();

	initlog ("starting login ...");
	start_login ();

	while (1)
	{
		pid = wait (&status);
		if (pid == login_pid)
		{
			start_login ();
		}
	}

	return (0);
}
