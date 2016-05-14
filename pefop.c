/*
 *  pefop - pofep-like POBox frontend processor
 *           Created by Masahiko Ito <m-ito@myh.no-ip.org>
 *
 *  pefopはpofep(by TAKANO Ryousei <takano@os-omicron.org>)をオリジナルとしています。
 *
 *  本ソースは以下の処理により整形されています。
 *
 *    $ indent -i8 -kr pefop.c
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
#include "server.h"

/*
 * マクロ定義
 */
/* 
 * default key binding(if you want to change, see 'man ascii') 
 */
#if 0
#define FEP_KEY 10		/* default key: cntl-j */
#define FEP_KEY 28		/* default key: cntl-yen */
#else
#define FEP_KEY 15		/* default key: cntl-o */
#endif

#define CLR_KEY  9		/* default key: TAB */
#define ESC_KEY 27
#define CTRL_KEYS 31		/* default control keys */
#define NEXTPAGE_KEY (14)	/* CTRL-N */
#define PREVPAGE_KEY (16)	/* CTRL-P */
#define FORWARD_KEY (6)		/* CTRL-F */
#define BACKWARD_KEY (2)	/* CTRL-B */

#if 0
#define MAXCANDS (50)
#else
#define MAXCANDS (1024)		/* 候補の最大個数 */
#endif
#define MAXCANDLEN (256)	/* 候補の最大長(辞書:EUC-JP) */
#define MAXLINELEN (256)	/* 読みの最大長 */
#define MAXRECVLEN (65536)	/* pbserverからの受信最大長 */

#if defined(UNIX98)
#       define _XOPEN_SOURCE
#       define _GNU_SOURCE
#endif

/*
 * typedef
 */
typedef void (*SIG_PF) (int);

/*
 * 外部関数宣言
 */
extern int getpt(void);
extern char *ptsname(int);
extern int grantpt(int);
extern int unlockpt(int);

/*
 * 関数宣言
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
int put1ch();
static char *Tgetstr(char *, char **);
int readCharFromStdin(unsigned char *);

/*
 * 大局変数
 */
const char *Mode_name[2] = { "[En]", "[Ja]" };

const char *Amsg = "pefop version 0.4.4 by Masahiko Ito.\nToggleKey=^O\n";
const char *Emsg = "pefop done!!\n";

char *Shell;
int Master;
int Slave;
int Child;
int Subchild;
struct termios Tt;
struct termios Stt;
struct winsize Win;
char Line[BUFSIZ];
int Fd_put1ch;
int Rfd;
int Wfd;
int Hs;				/* ステータスラインを持っているかどうか */
int Co;				/* カラム数 */
int Li;				/* ライン数 */
char *So;			/* スタンドアウト (反転) */
char *Se;
char *Us;			/* アンダーライン (下線) */
char *Ue;
char *Sc;			/* カーソル位置の保存，保存した位置への復帰 */
char *Rc;
char *Ce;			/* カーソル位置から行の最後までを削除する */
char *Ts;
char *Fs;
char *Ds;
char *Ku;
char *Kd;
char *Kr;
char *Kl;
int Fpid = 0;
struct termios *Ftt = 0;
char Endmsg[BUFSIZ] = "";
#if 0
unsigned char *Cands[MAXCANDLEN];
#else
unsigned char *Cands[MAXCANDS];
#endif
unsigned char Target[MAXLINELEN];	/* 入力された未確定平仮名文字列 */
unsigned char Candstr[MAXRECVLEN];	/* 侯補文字列 */
int Mode;			/* POBOX_MODE_XXX */
int Ncands;			/* number of candidates */
int Curcand;			/* index of a selecting candidate */
int Curpage;			/* page of modeline candidates */
int Page[MAXCANDS];
int Status;
int Pefop_mode_org;		/* 変換モード 1:曖昧検索 0:完全一致 */
int Pefop_mode;			/* 変換モード 1:曖昧検索 0:完全一致 */
/* スタティックなメンバ(シグナルハンドラ用)の初期化 */
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
 * 主処理
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
}

/* destructer */
void dPOBox()
{
	int i;
	for (i = 0; i < MAXCANDS; i++) {
		free(Cands[i]);
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
	write(Rfd, Mode_name[Mode], strlen(Mode_name[Mode]));

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
			write(Rfd, candbuf, strlen((char *) candbuf));
			tputs(Se, 1, put1ch);
		} else {
			write(Rfd, candbuf, strlen((char *) candbuf));
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
		pobox_selected(Curcand + 1);	/* なんで +1 ? */
		pobox_context((char *) Cands[Curcand]);
	}
	write(Wfd, p, strlen((char *) p));
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
		write(Rfd, p, len);
		tputs(Se, 1, put1ch);
	} else {
		tputs(Us, 1, put1ch);
		write(Rfd, p, len);
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
	write(Rfd, Mode_name[Mode], strlen((char *) Mode_name[Mode]));
	tputs(Fs, 1, put1ch);

	tputs(Rc, 1, put1ch);
	/* cm *//* cursor_address          cm      行 #1 桁 #2 に移動 */
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
	while (readCharFromStdin(s)) {
		if (*s == FEP_KEY) {	/* toggle mode */
			Mode =
			    (Mode ? POBOX_MODE_ALPHABET :
			     POBOX_MODE_KANJI);
			modeline();
			reset_target();
			length = 0;
			del_cand(length);
			Pefop_mode = Pefop_mode_org;
			continue;
		}
		/* if you input ESC_KEY, goto alphabet input mode (specialize case for vi) */
		if (Mode != POBOX_MODE_ALPHABET
		    && (*s != '\0' && *s != '\b' && *s != '\r'
			&& *s != '\n' && *s != NEXTPAGE_KEY
			&& *s != PREVPAGE_KEY && *s != '\t'
			&& *s != FORWARD_KEY && *s != BACKWARD_KEY)
		    && *s < CTRL_KEYS) {
			Mode = POBOX_MODE_ALPHABET;
			modeline();
			del_cand(length);
			write(Wfd, s, 1);
			continue;
		}

		if (Mode == POBOX_MODE_ALPHABET) {
			write(Wfd, s, 1);
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
			if (Target[0] == '\0') {
				pobox_getcands((char *) Target,
					       (char *) Candstr,
					       sizeof(Candstr) - 1, 1);
			} else {
				pobox_getcands((char *) Target,
					       (char *) Candstr,
					       sizeof(Candstr) - 1,
					       Pefop_mode);
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
	case BACKWARD_KEY:
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
	case FORWARD_KEY:	/* next candidate */
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

	case '\t':		/* TAB */
		Status = POBOX_SELECT_OFF;
		Curcand = -1;
		Curpage = 0;
		if (Pefop_mode == 0) {
			Pefop_mode = 1;
		} else {
			Pefop_mode = 0;
		}
		ret = 1;
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
	case BACKWARD_KEY:
		if ((len = strlen((char *) Target)) > 0) {
			Target[len - 1] = '\0';
			ret = 1;
		} else {
			write(Wfd, s, 1);
		}
		break;

	case '\r':		/* decide pattern */
	case '\n':
		if (Curcand == -1 && *Target == '\0') {
			write(Wfd, s, 1);
		} else {
			Status = POBOX_SELECT_ON;
			Curcand = 0;
			ret = 1;
		}
		break;

	case ' ':
	case NEXTPAGE_KEY:	/* CTRL-N */
	case FORWARD_KEY:	/* next candidate */
		Status = POBOX_SELECT_ON;
		Curcand = 0;
		break;

	case '\t':		/* TAB */
		if (Pefop_mode == 0) {
			Pefop_mode = 1;
		} else {
			Pefop_mode = 0;
		}
		ret = 1;
		break;

	default:
		if (c == '\0') {	/* [CTRL]+[SPACE] => [SPACE] */
			c = ' ';
			if (strlen((char *) Target) == 0) {
				write(Wfd, &c, 1);
				break;
			}
		}
		if (!isprint(c)) {
			write(Wfd, &c, 1);
			break;
		}

		strcat((char *) Target, (char *) s);
		ret = 1;
		break;
	}

	return ret;
}

/* コンストラクタだよん */
void Pty(int ac, char **av, char *amsg, char *emsg, void (*fp) (void))
{
	sig_fp = fp;
	setup(ac, av, amsg, emsg);
}

void setup(int ac, char **av, char *amsg, char *emsg)
{
	/* 環境変数 TERM のエントリを取得 */
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

	Pefop_mode_org = 1;
	pefop_exact = getenv("PEFOP_EXACT");
	if (pefop_exact) {
		if (strcmp(pefop_exact, "y") == 0 ||
		    strcmp(pefop_exact, "yes") == 0 ||
		    strcmp(pefop_exact, "Y") == 0 ||
		    strcmp(pefop_exact, "YES") == 0) {
			Pefop_mode_org = 0;
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

	/* termcap から装飾用のエントリを取ってくる */
	static char funcstr[BUFSIZ];
	char *pt = funcstr;

	/* スタンドアウト (反転) */
	So = Tgetstr("so", &pt);
	Se = Tgetstr("se", &pt);

	/* アンダーライン (下線) */
	Us = Tgetstr("us", &pt);
	Ue = Tgetstr("ue", &pt);

	/* カーソル位置の保存，保存した位置への復帰 */
	Sc = Tgetstr("sc", &pt);
	Rc = Tgetstr("rc", &pt);

	/* カーソル位置から行の最後までを削除する */
	Ce = Tgetstr("ce", &pt);

	/* カーソルキー 上下右左 */
	Ku = Tgetstr("ku", &pt);
	Kd = Tgetstr("kd", &pt);
	Kr = Tgetstr("kr", &pt);
	Kl = Tgetstr("kl", &pt);

	/* カラム数とライン数 */
	Co = tgetnum("co");
	Li = tgetnum("li");

	if (Hs == 0) {
		/* kon と jfbterm ではステータスラインを使わない */
		if (strcmp(term, "kon") == 0
		    || strcmp(term, "jfbterm") == 0) {
			Hs = 0;
		} else {
			/* ステータスラインを持っているかどうか */
			Hs = tgetflag("hs");
		}
	} else {
		if (Hs == 1) {
			Hs = !0;
		} else {
			Hs = 0;
		}
	}

	/* ステータスラインへ移動，戻る */
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
			write(1, Ce, strlen(Ce));
			write(1, cs, strlen(cs));
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

	/* 開始と終了のメッセージ */
	if (amsg && ac == 1)
		write(1, amsg, strlen(amsg));
	if (emsg && ac == 1)
		strcat(Endmsg, emsg);

	/* 使用する仮想端末デバイスの名前 */
	/* XX に [p-s][0-f] が入る */
	strcpy(Line, "/dev/ptyXX");

	/* 仮想端末で起動する shell は何？ */
	char *shell = getenv("SHELL");
	if (shell == NULL)
		shell = "/bin/sh";

	/* マスタデバイスの取得 */
	getmaster();

	/* 端末の初期化 */
	fixtty();

	/* シグナルハンドラの設定 */
	signal(SIGCHLD, (SIG_PF) finish);

	/* フォークします */
	Child = fork();
	if (Child < 0) {
		perror("fork");
		fail();
	}

	/* 子供です */
	if (Child == 0) {
		Subchild = Child = fork();
		if (Child < 0) {
			perror("fork");
			fail();
		}
		if (Child) {
#if defined(__NetBSD__)
/*
 * 起動してもなぜか直に終了してしまう原因はexecしたshellが
 * 起動しきる前に、その疑似端末のマスタ側を読み込もうとし
 * ていたかららしい。
 */
			sleep(1);
#endif
			close(0);
			int cc;
			char obuf[BUFSIZ];
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
#if defined(TIOCNOTTY)
			ioctl(t, TIOCNOTTY, (char *) 0);
#endif
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
 * CTRL-C(SIGINT)等のシグナルが効かなかった原因は、疑似端末の
 * スレーブが制御端末として設定できていなかったかららしい。
 *
 * stdin(=疑似端末のスレーブ)を制御端末とする
 */
		ioctl(0, TIOCSCTTY, 0);
#endif
		if (ac > 1)
			execvp(av[1], &av[1]);
		else
			execl(shell, strrchr(shell, '/') + 1, (char *) 0);
		perror(shell);
		fail();
	}

	/* 親です */
	Rfd = 0;
	Wfd = Master;

	/* シグナルハンドラ用にスタティックな変数に退避させておく */
	Fpid = Child;
	Ftt = &Tt;
}

/* デストラクタだよん */
void dPty()
{
	done();
}

/* 終了時に呼ばれる */
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

/* 何か障害があった場合 */
void fail()
{
	kill(0, SIGTERM);
	done();
}

/* マスタデバイスを取る */
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

/* スレーブデバイスを取る */
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

/* 仮想端末の初期化 */
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

/* シグナル受信時に呼ばれる */
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
 * termcapエントリが貧弱な場合core dumpするのを防ぐ為のtgetstr代替
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

int put1ch(ch)
int ch;
{
	int ret;
	char onech;

	onech = ch;
	ret = write(Fd_put1ch, &onech, 1);
	return ret;
}

int readCharFromStdin(buf)
unsigned char *buf;
{
	static char buf_save[MAXLINELEN];
	static int bs_rp = 0;
	static int bs_wp = 0;
	unsigned char buftmp[2];
	int ret;
	int i;
	int ku_sw, kd_sw, kr_sw, kl_sw;

	if (buf_save[bs_rp] != '\0') {
		*buf = buf_save[bs_rp];
		buf_save[bs_rp] = '\0';
		bs_rp++;
		if (bs_rp >= MAXLINELEN) {
			bs_rp = 0;
		}
		return 1;
	} else {
		if (Mode == POBOX_MODE_ALPHABET) {
			return read(0, buf, 1);
		} else {
			bs_rp = bs_wp = 0;
			buf_save[0] = '\0';
			if ((ret = read(0, buftmp, 1)) <= 0) {
				*buf = '\0';
				return ret;
			} else {
				if (buftmp[0] >= ' ') {
					*buf = buftmp[0];
					return 1;
				} else {
					ku_sw = 1;
					kd_sw = 1;
					kr_sw = 1;
					kl_sw = 1;
					i = 0;
					for (;;) {
						if (ku_sw == 1) {
							if (Ku[i] ==
							    buftmp[0]) {
								if (Ku
								    [i +
								     1] ==
								    '\0') {
									*buf = PREVPAGE_KEY;
									bs_rp
									    =
									    bs_wp
									    =
									    0;
									buf_save
									    [0]
									    =
									    '\0';
									return
									    1;
								}
							} else {
								ku_sw = 0;
							}
						}
						if (kd_sw == 1) {
							if (Kd[i] ==
							    buftmp[0]) {
								if (Kd
								    [i +
								     1] ==
								    '\0') {
									*buf = NEXTPAGE_KEY;
									bs_rp
									    =
									    bs_wp
									    =
									    0;
									buf_save
									    [0]
									    =
									    '\0';
									return
									    1;
								}
							} else {
								kd_sw = 0;
							}
						}
						if (kr_sw == 1) {
							if (Kr[i] ==
							    buftmp[0]) {
								if (Kr
								    [i +
								     1] ==
								    '\0') {
									*buf = FORWARD_KEY;
									bs_rp
									    =
									    bs_wp
									    =
									    0;
									buf_save
									    [0]
									    =
									    '\0';
									return
									    1;
								}
							} else {
								kr_sw = 0;
							}
						}
						if (kl_sw == 1) {
							if (Kl[i] ==
							    buftmp[0]) {
								if (Kl
								    [i +
								     1] ==
								    '\0') {
									*buf = BACKWARD_KEY;
									bs_rp
									    =
									    bs_wp
									    =
									    0;
									buf_save
									    [0]
									    =
									    '\0';
									return
									    1;
								}
							} else {
								kl_sw = 0;
							}
						}
						buf_save[bs_wp++] =
						    buftmp[0];
						if (bs_wp >= MAXLINELEN) {
							bs_wp = 0;
						}
						buf_save[bs_wp] = '\0';
						if (ku_sw == 0 &&
						    kd_sw == 0 &&
						    kr_sw == 0 &&
						    kl_sw == 0) {
							*buf =
							    buf_save
							    [bs_rp];
							buf_save[bs_rp] =
							    '\0';
							bs_rp++;
							if (bs_rp >=
							    MAXLINELEN) {
								bs_rp = 0;
							}
							return 1;
						}
						if ((ret =
						     read(0, buftmp,
							  1)) <= 0) {
							*buf = '\0';
							bs_rp = bs_wp = 0;
							buf_save[0] = '\0';
							return ret;
						}
						i++;
					}
				}
			}
		}
	}
}
