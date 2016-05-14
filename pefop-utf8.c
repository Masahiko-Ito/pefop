/*
 *  pefop-utf8 - pofep-like POBox frontend processor for UTF-8
 *           Created by Masahiko Ito <m-ito@myh.no-ip.org>
 *
 *  pefop-utf8��pofep(by TAKANO Ryousei <takano@os-omicron.org>)�򥪥ꥸ�ʥ�Ȥ��Ƥ��ޤ���
 *
 *  UTF-8�б��ϰʲ��Υѥå��򻲹ͤˤ��Ƥ��ޤ���
 *
 *    http://gentoo.osuosl.org/distfiles/canfep_utf8.diff
 *
 *  �ܥ������ϰʲ��ν����ˤ����������Ƥ��ޤ���
 *
 *    $ indent -i8 -kr pefop-utf8.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <termcap.h>		/* #include <term.h> */
#include <errno.h>
#include <iconv.h>
#include "server.h"

/*
 * �ޥ������
 */
/* 
 * default key binding(if you want to change, see 'man ascii') 
 */
#if 0
#define FEP_KEY 15		/* default key: cntl-o */
#define FEP_KEY 28		/* default key: cntl-yen */
#else
#define FEP_KEY 10		/* default key: cntl-j */
#endif

#define CLR_KEY  9		/* default key: TAB */
#define ESC_KEY 27
#define CTRL_KEYS 31		/* default control keys */
#define NEXTPAGE_KEY (14)	/* CTRL-N */
#define PREVPAGE_KEY (16)	/* CTRL-P */

#if 0
#define MAXCANDS (50)
#else
#define MAXCANDS (256)
#endif
#define MAXCANDLEN (64)
#define MAXLINELEN (256)

#if defined(UNIX98)
#       define _XOPEN_SOURCE
#       define _GNU_SOURCE
#endif

/*
 * typedef
 */
typedef void (*SIG_PF) (int);

/*
 * �ؿ����
 */
void Pty();
void dPty();
void setup();
void done();
void fail();
void getmaster();
void getslave();
void fixtty();
void finish();
void POBox();
void dPOBox();
void reset_target();
void update_candlist();
void decide();
void put_cand();
void del_cand();
void modeline();
int select_on_routine(unsigned char);
int select_off_routine(unsigned char);
void loop();
void write_utf8();
char *iconv_string();
int put1ch();
static char *Tgetstr(char *id, char **area);

/*
 * ����ѿ�
 */
const char *Mode_name[2] = { "[En]", "[Ja]" };
const char *Amsg =
    "pefop-utf8 version 0.3 by Masahiko Ito.\nToggleKey=^J\n";
const char *Emsg = "pefop-utf8 done!!\n";

char *Shell;
int Master;
int Slave;
int Child;
int Subchild;
struct termios Tt;
struct termios Stt;
struct winsize Win;
iconv_t Eucjp_to_utf8_cd;
char Line[BUFSIZ];
int Fd_put1ch;
int Rfd;
int Wfd;
int Hs;				/* ���ơ������饤�����äƤ��뤫�ɤ��� */
int Co;				/* ������ */
int Li;				/* �饤��� */
char *So;			/* ������ɥ����� (ȿž) */
char *Se;
char *Us;			/* ��������饤�� (����) */
char *Ue;
char *Sc;			/* ����������֤���¸����¸�������֤ؤ����� */
char *Rc;
char *Ce;			/* ����������֤���ԤκǸ�ޤǤ������� */
char *Ts;
char *Fs;
char *Ds;
int Fpid = 0;
struct termios *Ftt = 0;
char Endmsg[BUFSIZ] = "";
#if 0
unsigned char *Cands[MAXCANDLEN];
#else
unsigned char *Cands[MAXCANDS];
#endif
unsigned char Target[MAXLINELEN];	/* ���Ϥ��줿̤����ʿ��̾ʸ���� */
unsigned char Candstr[8192];	/* ����ʸ���� */
int Mode;			/* POBOX_MODE_XXX */
int Ncands;			/* number of candidates */
int Curcand;			/* index of a selecting candidate */
int Curpage;			/* page of modeline candidates */
int Page[MAXCANDS];
int Status;
int Pefop_mode;			/* �Ѵ��⡼�� 1:ۣ�渡�� 0:�������� */
/* �����ƥ��å��ʥ���(�����ʥ�ϥ�ɥ���)�ν���� */
void (*sig_fp) (void) = NULL;
static char Nullstr[] = "";

/*
 * enum
 */
enum {
	POBOX_MODE_ALPHABET = 0,
	POBOX_MODE_KANJI = 1
};

enum {
	POBOX_SELECT_ON = 0,
	POBOX_SELECT_OFF = 1
};

/*
 * �����
 */
int main(int ac, char **av)
{
	Pty(ac, av, Amsg, Emsg, NULL);
	POBox(ac, av, Amsg, Emsg, NULL);
	loop();
	dPOBox();
	dPty();

	return 0;
}

void POBox(int ac, char **av, char *amsg, char *emsg, void (*fp) (void))
{
	int i;
	char *p_lang = getenv("LANG");

	if (!setlocale(LC_ALL, "")) {
		fprintf(stderr, "setlocale() error\n");
		exit(-1);
	}

	/* initialize OpenPOBoxSerever */
	pobox_connect();

	for (i = 0; i < MAXCANDS; i++) {
		Cands[i] = (unsigned char *) malloc(MAXCANDLEN);
	}

	reset_target();
	Page[0] = 0;
	Mode = POBOX_MODE_ALPHABET;
	Status = POBOX_SELECT_OFF;
	Ncands = 0;
	modeline();
	if (p_lang == NULL || strstr(p_lang, "-8")) {
		Eucjp_to_utf8_cd = iconv_open("utf-8", "euc-jp");
	}

}

/* destructer */
void dPOBox()
{
	int i;
	for (i = 0; i < MAXCANDS; i++) {
		free(Cands[i]);
	}
	if (Eucjp_to_utf8_cd != (iconv_t) - 1) {
		iconv_close(Eucjp_to_utf8_cd);
	}

	/* disconnect OpenPOBoxServer */
	pobox_disconnect();
}

void reset_target()
{
	strcpy((char *) Target, "");
	Curcand = -1;
	Curpage = 0;
}

/*
 *  display candidate list
 */
void update_candlist()
{
	int col = 5;
	unsigned char candbuf[256] = "";

	Fd_put1ch = Rfd;
	tputs(Sc, 1, put1ch);
	tputs(Ts, 1, put1ch);
	tputs(Ce, 1, put1ch);
	write(Rfd, (char *) Mode_name[Mode],
	      strlen((char *) Mode_name[Mode]));

	int idx = 1, rev = 1, i;
	for (i = Page[Curpage]
	     /*curcand */
	     /*+1 */
	     ; i < Ncands; i++) {
		if (Cands[i][0] == ESC_KEY) {
			rev = 0;
			continue;
		}
		sprintf((char *) candbuf, "%d:%s", idx, (char *) Cands[i]);
		col += strlen((char *) candbuf) + 1;
		if (Co <= col) {
			break;
		}
		if (i == Curcand && rev) {
			tputs(So, 1, put1ch);
			write_utf8(Rfd, (char *) candbuf,
				   strlen((char *) candbuf));
			tputs(Se, 1, put1ch);
		} else {
			write_utf8(Rfd, (char *) candbuf,
				   strlen((char *) candbuf));
		}
		write(Rfd, " ", 1);
		++idx;
	}
	Page[Curpage + 1] = i;
	tputs(Fs, 1, put1ch);
	tputs(Rc, 1, put1ch);
}

/*
 * output a decided string
 */
void decide(unsigned char *p)
{
	int i;
	if (Curcand < 0) {
		for (i = 0; i < Ncands; i++) {
			if (strcmp((char *) Cands[i], (char *) Target)) {
				Curcand = i;
			}
		}
	}
	if (Curcand >= 0) {
		pobox_selected(Curcand + 1);	/* �ʤ�� +1 ? */
		pobox_context((char *) Cands[Curcand]);
	}
	write_utf8(Wfd, (char *) p, strlen((char *) p));
}

/*
 * output a first candidate(non decided) string
 */
void put_cand(unsigned char *p, int len, int s)
{
	Fd_put1ch = Rfd;
	tputs(Sc, 1, put1ch);
	if (!s) {
		tputs(So, 1, put1ch);
		write_utf8(Rfd, (char *) p, len);
		tputs(Se, 1, put1ch);
	} else {
		tputs(Us, 1, put1ch);
		write_utf8(Rfd, (char *) p, len);
		tputs(Ue, 1, put1ch);
	}
	tputs(Rc, 1, put1ch);
}

/*
 * delete a candidate string on the terminal
 */
void del_cand(int len)
{
	int i;
	Fd_put1ch = Rfd;
	if (len != 0) {
		tputs(Sc, 1, put1ch);
		for (i = 0; i < len; i++) {
			write(Rfd, " ", 1);
		}
		tputs(Rc, 1, put1ch);
	}
}

/*
 * display status on the mode line
 */
void modeline()
{
	Fd_put1ch = Rfd;
	tputs(Sc, 1, put1ch);

	tputs(Ts, 1, put1ch);
	tputs(Ce, 1, put1ch);
	write(Rfd, (char *) Mode_name[Mode],
	      strlen((char *) Mode_name[Mode]));
	tputs(Fs, 1, put1ch);

	tputs(Rc, 1, put1ch);
	/* cm *//* cursor_address          cm      �� #1 �� #2 �˰�ư */
}

/*
 * main loop
 */
void loop()
{
	unsigned char s[2];
	int length = 0;
	int search;

	Fd_put1ch = Wfd;
	while (read(0, s, 1)) {
		if (*s == FEP_KEY) {	/* toggle mode */
			Mode =
			    (Mode ? POBOX_MODE_ALPHABET :
			     POBOX_MODE_KANJI);
			modeline();
			reset_target();
			length = 0;
			del_cand(length);
			continue;
		}
		/* if you input ESC_KEY, goto alphabet input mode (specialize case for vi) */
		if (Mode != POBOX_MODE_ALPHABET
		    && (*s != '\0' && *s != '\b' && *s != '\r'
			&& *s != '\n' && *s != NEXTPAGE_KEY
			&& *s != PREVPAGE_KEY) && *s < CTRL_KEYS) {
			Mode = POBOX_MODE_ALPHABET;
			modeline();
			del_cand(length);
			write(Wfd, (char *) s, 1);
			continue;
		}

		if (Mode == POBOX_MODE_ALPHABET) {
			write(Wfd, (char *) s, 1);
			continue;
		}

		/* main flow */
		search = (Status == POBOX_SELECT_ON) ?
		    select_on_routine(*s) : select_off_routine(*s);
		del_cand(length);
		if (Curcand == -1) {
			length = strlen((char *) Target);
			put_cand(Target, length, Status);
		} else {
			length = strlen((char *) Cands[Curcand]);
			put_cand(Cands[Curcand], length, Status);
		}

		if (search) {
			char *p;
			if (Target[0] == '\0'){
				pobox_getcands((char *) Target, (char *) Candstr,
				       sizeof(Candstr) - 1, 1);
			}else{
				pobox_getcands((char *) Target, (char *) Candstr,
				       sizeof(Candstr) - 1, Pefop_mode);
			}
			Ncands = 0;
			p = strtok((char *) Candstr, "\t\r\n");	/* skip */
#if 0
			while (p) {
#else
			while (p && Ncands < MAXCANDS) {
#endif
				strncpy((char *) Cands[Ncands++], p,
					MAXCANDLEN - 1);
				p = strtok(NULL, "\t\r\n");
			}
		}
		update_candlist();
	}
}

int select_on_routine(unsigned char c)
{
	int ret = 0;

	Fd_put1ch = Wfd;
	switch (c) {
#if 0
	case CLR_KEY:
		reset_target();
		break;
#endif

	case '\b':		/* previous candidate */
	case 0x7f:
		if (Curcand > 0) {
			Curcand--;
			if (Curpage > 0 && Curcand < Page[Curpage]) {
				--Curpage;
			}
		} else {
			Status = POBOX_SELECT_OFF;
			Curcand = -1;
			ret = 1;
			write(Wfd, "\b", 1);
		}
		break;

	case PREVPAGE_KEY:	/* CTRL-P */
		if (Curpage > 0) {
			Curcand = Page[Curpage] - 1;
			Curpage--;
		}
		break;

	case '\r':		/* decide pattern */
	case '\n':
		if (Curcand < 0) {
			decide(Target);
		} else {
			decide(Cands[Curcand]);
		}
		reset_target();
		Status = POBOX_SELECT_OFF;
		ret = 1;
		break;

	case ' ':		/* next candidate */
		if (Curcand < Ncands - 1) {
			Curcand++;
		}
		if (Curcand >= 0 && Curcand >= Page[Curpage + 1]) {
			++Curpage;
		}

		break;

	case NEXTPAGE_KEY:	/* CTRL-N */
		if (Page[Curpage + 1] > Page[Curpage]
		    && Page[Curpage + 1] < Ncands) {
			Curpage++;
			Curcand = Page[Curpage];
		}
		break;

	default:
		if (Curcand < 0) {
			decide(Target);
		} else {
			decide(Cands[Curcand]);
		}
		reset_target();
		Status = POBOX_SELECT_OFF;
		ret = select_off_routine(c);
		break;
	}
#if 0
	if (Status == POBOX_SELECT_ON && Curcand >= 0
	    && Curcand >= Page[Curpage + 1]) {
		++Curpage;
	}

	if (Status == POBOX_SELECT_ON && Curcand >= 0 && Curpage > 0
	    && Curcand < Page[Curpage]) {
		--Curpage;
	}
#endif
	return ret;
}

int select_off_routine(unsigned char c)
{
	int ret = 0;
	int len;
	unsigned char s[2];

	Fd_put1ch = Wfd;
	s[0] = c;
	s[1] = '\0';
	switch (c) {
	case '\b':		/* back space */
	case 0x7f:
		if ((len = strlen((char *) Target)) > 0) {
			Target[len - 1] = '\0';
			ret = 1;
		} else {
			write(Wfd, (char *) s, 1);
		}
		break;

	case '\r':		/* decide pattern */
	case '\n':
		if (Curcand == -1 && *Target == '\0') {
			write(Wfd, (char *) s, 1);
		} else {
			Status = POBOX_SELECT_ON;
			Curcand = 0;
			ret = 1;
		}
		break;

	case ' ':
	case NEXTPAGE_KEY:	/* CTRL-N */
		Status = POBOX_SELECT_ON;
		Curcand = 0;
		break;

	default:
		if (c == '\0') {	/* [CTRL]+[SPACE] => [SPACE] */
			c = ' ';
			if (strlen((char *) Target) == 0) {
				write(Wfd, (char *) &c, 1);
				break;
			}
		}
		if (!isprint(c)) {
			write(Wfd, (char *) &c, 1);
			break;
		}

		strcat((char *) Target, (char *) s);
		ret = 1;
		break;
	}

	return ret;
}

/* ���󥹥ȥ饯������� */
void Pty(int ac, char **av, char *amsg, char *emsg, void (*fp) (void))
{
	sig_fp = fp;
	setup(ac, av, amsg, emsg);
}

void setup(int ac, char **av, char *amsg, char *emsg)
{
	/* �Ķ��ѿ� TERM �Υ���ȥ����� */
	char buff[BUFSIZ];
	char *term;
	char *pefop_hs;
	char *pefop_exact;

	Fd_put1ch = 1;
	Hs = 0;
	pefop_hs = getenv("PEFOP_HS");
	if (pefop_hs) {
		if (strcmp(pefop_hs, "y") == 0 ||
		    strcmp(pefop_hs, "yes") == 0 ||
		    strcmp(pefop_hs, "Y") == 0 ||
		    strcmp(pefop_hs, "YES") == 0) {
			Hs = 1;
		} else {
			Hs = 2;
		}
	}

	Pefop_mode = 1;
	pefop_exact = getenv("PEFOP_EXACT");
	if (pefop_exact){
		if (strcmp(pefop_exact, "y") == 0 ||
		    strcmp(pefop_exact, "yes") == 0 ||
		    strcmp(pefop_exact, "Y") == 0 ||
		    strcmp(pefop_exact, "YES") == 0) {
			Pefop_mode = 0;
		}
	}

	term = getenv("TERM");
	if (!term)
		term = "vt100";
	int ret = tgetent(buff, term);
	if (ret != 1) {
		tgetent(buff, "vt100");
		putenv("TERM=vt100");
	}

	/* termcap ���������ѤΥ���ȥ���äƤ��� */
	static char funcstr[BUFSIZ];
	char *pt = funcstr;

	/* ������ɥ����� (ȿž) */
	So = Tgetstr("so", &pt);
	Se = Tgetstr("se", &pt);

	/* ��������饤�� (����) */
	Us = Tgetstr("us", &pt);
	Ue = Tgetstr("ue", &pt);

	/* ����������֤���¸����¸�������֤ؤ����� */
	Sc = Tgetstr("sc", &pt);
	Rc = Tgetstr("rc", &pt);

	/* ����������֤���ԤκǸ�ޤǤ������� */
	Ce = Tgetstr("ce", &pt);

	/* �������ȥ饤��� */
	Co = tgetnum("co");
	Li = tgetnum("li");

	if (Hs == 0) {
		/* kon �� jfbterm �Ǥϥ��ơ������饤���Ȥ�ʤ� */
		if (strcmp(term, "kon") == 0
		    || strcmp(term, "jfbterm") == 0) {
			Hs = 0;
		} else {
			/* ���ơ������饤�����äƤ��뤫�ɤ��� */
			Hs = tgetflag("hs");
		}
	} else {
		if (Hs == 1) {
			Hs = !0;
		} else {
			Hs = 0;
		}
	}

	/* ���ơ������饤��ذ�ư����� */
	if (Hs) {
		Ts = tgoto(Tgetstr("ts", &pt), 0, 0);
		Fs = Tgetstr("fs", &pt);
		Ds = Tgetstr("ds", &pt);
		if (Ds) {
			strcat(Endmsg, Ds);
			strcat(Endmsg, Ce);
		}
	} else {
		char *cs = tgoto(Tgetstr("cs", &pt), Li - 2, 0);
		if (cs) {
			tputs(Ce, 1, put1ch);
			tputs(cs, 1, put1ch);
		}
		char *cl = Tgetstr("cl", &pt);
		if (cl) {
			tputs(cl, 1, put1ch);
			strcat(Endmsg, cl);
		}
		Ds = tgoto(Tgetstr("cs", &pt), Li - 1, 0);
		if (Ds) {
			strcat(Endmsg, Ds);
			strcat(Endmsg, Ce);
		}
		Ts = tgoto(Tgetstr("cm", &pt), 0, Li - 1);
		Fs = Rc;
	}

	/* ���ϤȽ�λ�Υ�å����� */
	if (amsg && ac == 1)
		write(1, amsg, strlen(amsg));
	if (emsg && ac == 1)
		strcat(Endmsg, emsg);

	/* ���Ѥ��벾��ü���ǥХ�����̾�� */
	/* XX �� [p-s][0-f] ������ */
	strcpy(Line, "/dev/ptyXX");

	/* ����ü���ǵ�ư���� shell �ϲ��� */
	char *shell = getenv("SHELL");
	if (shell == NULL)
		shell = "/bin/sh";

	/* �ޥ����ǥХ����μ��� */
	getmaster();

	/* ü���ν���� */
	fixtty();

	/* �����ʥ�ϥ�ɥ������ */
	signal(SIGCHLD, (SIG_PF) finish);

	/* �ե��������ޤ� */
	Child = fork();
	if (Child < 0) {
		perror("fork");
		fail();
	}

	/* �Ҷ��Ǥ� */
	if (Child == 0) {
		Subchild = Child = fork();
		if (Child < 0) {
			perror("fork");
			fail();
		}
		if (Child) {
#if defined(__NetBSD__)
/*
 * ��ư���Ƥ�ʤ���ľ�˽�λ���Ƥ��ޤ�������exec����shell��
 * ��ư���������ˡ����ε���ü���Υޥ���¦���ɤ߹��⤦�Ȥ�
 * �Ƥ�������餷����
 */
			sleep(1);
#endif
			close(0);
			int cc;
			char obuf[BUFSIZ];
			char buff[BUFSIZ];
			while (1) {
				cc = (int) read(Master, obuf, BUFSIZ);
				if (cc <= 0)
					break;
				write(1, obuf, cc);
			}
			done();
		}
		int t = open("/dev/tty", O_RDWR);
		if (t >= 0) {
			ioctl(t, TIOCNOTTY, (char *) 0);
			close(t);
		}
		getslave();
		close(Master);
		dup2(Slave, 0);
		dup2(Slave, 1);
		dup2(Slave, 2);
		close(Slave);
#if defined(__NetBSD__)
/*
 * CTRL-C(SIGINT)���Υ����ʥ뤬�����ʤ��ä������ϡ�����ü����
 * ���졼�֤�����ü���Ȥ�������Ǥ��Ƥ��ʤ��ä�����餷����
 *
 * stdin(=����ü���Υ��졼��)������ü���Ȥ���
 */
		ioctl(0, TIOCSCTTY, 0);
#endif
		if (ac > 1)
			execvp(av[1], &av[1]);
		else
			execl(shell, strrchr(shell, '/') + 1, 0);
		perror(shell);
		fail();
	}

	/* �ƤǤ� */
	Rfd = 0;
	Wfd = Master;

	/* �����ʥ�ϥ�ɥ��Ѥ˥����ƥ��å����ѿ������򤵤��Ƥ��� */
	Fpid = Child;
	Ftt = &Tt;
}

/* �ǥ��ȥ饯������� */
void dPty()
{
	done();
}

/* ��λ���˸ƤФ�� */
void done()
{
	if (Subchild) {
		close(Master);
		exit(0);
	}
#if 0
	tcsetattr(0, TCSAFLUSH, &Tt);
#else
	tcsetattr(0, TCSAFLUSH, &Stt);
#endif
	exit(0);
}

/* �����㳲�����ä���� */
void fail()
{
	kill(0, SIGTERM);
	done();
}

/* �ޥ����ǥХ������� */
void getmaster()
{
#if defined(UNIX98)
	Master = getpt();
	tcgetattr(0, &Tt);
	Stt = Tt;
	Tt.c_iflag &= ~ISTRIP;
	ioctl(0, TIOCGWINSZ, (char *) &Win);
#else
	struct stat stb;

	char *pty = &line[strlen("/dev/ptyp")];
	for (char *p = "pqrs"; *p; p++) {
		line[strlen("/dev/pty")] = *p;
		*pty = '0';
#if 0
		if (stat(line, &stb) < 0)
			break;
#endif
		for (char *s = "0123456789abcdef"; *s; s++) {
			*pty = *s;
			Master = open(line, O_RDWR);
			if (Master < 0)
				continue;
			char *t = &line[strlen("/dev/")];
			*t = 't';
			int ok = access(line, R_OK | W_OK) == 0;
			*t = 'p';
			if (ok) {
				tcgetattr(0, &Tt);
				Stt = Tt;
				Tt.c_iflag &= ~ISTRIP;
				ioctl(0, TIOCGWINSZ, (char *) &Win);
				return;
			}
			close(Master);
		}
	}

	printf("Out of pty's\n");
	fail();
#endif
}

/* ���졼�֥ǥХ������� */
void getslave()
{
#if defined(UNIX98)
	char *pty;

	pty = ptsname(Master);
	grantpt(Master);
	unlockpt(Master);
	Slave = open(pty, O_RDWR);
	tcsetattr(Slave, TCSAFLUSH, &Tt);
	if (!Hs) {
		Win.ws_row--;
	}
	ioctl(Slave, TIOCSWINSZ, (char *) &Win);
	setsid();
#else
	line[strlen("/dev/")] = 't';
	Slave = open(line, O_RDWR);
	if (Slave < 0) {
		perror(line);
		fail();
	}
	tcsetattr(Slave, TCSAFLUSH, &Tt);
	if (!Hs)
		Win.ws_row--;
	ioctl(Slave, TIOCSWINSZ, (char *) &Win);
	setsid();
	close(Slave);
	Slave = open(line, O_RDWR);
	if (Slave < 0) {
		perror(line);
		fail();
	}
#endif
}

/* ����ü���ν���� */
void fixtty()
{
	struct termios rtt;
	rtt = Tt;
	rtt.c_iflag &= ~(INLCR | IGNCR | ICRNL | IXON | IXOFF | ISTRIP);
	rtt.c_lflag &= ~(ISIG | ICANON | ECHO);
	rtt.c_oflag &= ~OPOST;
	rtt.c_cc[VMIN] = 1;
	rtt.c_cc[VTIME] = 0;
	tcsetattr(0, TCSAFLUSH, &rtt);
}

/* �����ʥ�������˸ƤФ�� */
void finish()
{
	int status;
	int pid;
	int die = 0;
	while ((pid = wait3(&status, WNOHANG, 0)) > 0) {
		if (pid == Fpid)
			die = 1;
	}
	if (die) {
#if 0
		tcsetattr(0, TCSAFLUSH, Ftt);
#else
		tcsetattr(0, TCSAFLUSH, &Stt);
#endif
		if (strlen(Endmsg) != 0)
			printf("%s", Endmsg);
		if (sig_fp != NULL)
			(*sig_fp) ();
		exit(0);
	}
}

/*
 * termcap����ȥ꤬�ϼ�ʾ��core dump����Τ��ɤ��٤�tgetstr����
 */
static char *Tgetstr(char *id, char **area)
{
	static char *str;

	str = tgetstr(id, area);
	if (str == (char *) NULL) {
		return (Nullstr);
	} else {
		return (str);
	}
}

/*
 * for UTF-8
 */
void write_utf8(int fd, char *p, int len)
{
	if (Eucjp_to_utf8_cd == (iconv_t) - 1) {
		write(fd, p, strlen(p));
	} else {
		char *putf8 = iconv_string(Eucjp_to_utf8_cd, p, len);
		write(fd, putf8, strlen(putf8));
		free(putf8);
	}
}

char *iconv_string(iconv_t fd, char *str, int slen)
{
	char *from;
	size_t fromlen;
	char *to;
	size_t tolen;
	size_t len = 0;
	size_t done = 0;
	char *result = NULL;
	char *p;

	from = (char *) str;
	fromlen = slen;
	for (;;) {
		if (len == 0 || errno == E2BIG) {
			/* Allocate enough room for most conversions.  When re-allocating
			 * increase the buffer size. */
			len = len + fromlen * 2 + 40;
			p = (char *) malloc((unsigned) len);
			if (p != NULL && done > 0)
				memcpy(p, result, done);
			free(result);
			result = p;
			if (result == NULL)	/* out of memory */
				break;
		}

		to = (char *) result + done;
		tolen = len - done - 2;
		/* Avoid a warning for systems with a wrong iconv() prototype by
		 * casting the second argument to void *. */
		if (iconv(fd, &from, &fromlen, &to, &tolen) !=
		    (size_t) - 1) {
			/* Finished, append a NUL. */
			*to = 0;
			break;
		}
		/* Check both ICONV_EILSEQ and EILSEQ, because the dynamically loaded
		 * iconv library may use one of them. */
		if (errno == EILSEQ || errno == EILSEQ) {
			/* Can't convert: insert a '?' and skip a character.  This assumes
			 * conversion from 'encoding' to something else.  In other
			 * situations we don't know what to skip anyway. */
			*to++ = *from++;
			fromlen -= 1;
		} else if (errno != E2BIG) {
			/* conversion failed */
			free(result);
			result = NULL;
			break;
		}
		/* Not enough room or skipping illegal sequence. */
		done = to - (char *) result;
	}
	return result;
}

int put1ch(ch)
int ch;
{
	int ret;
	char onech;

	onech = ch;
	ret = write(Fd_put1ch, &onech, 1);
	return ret;
}