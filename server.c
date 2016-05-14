/* $Id: server.c,v 1.5 2016/01/08 16:37:21 m-ito Exp $
 * Copyright (c) 2002 pobox-linux project
 *
 * from imopenpobox (yanai-san)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include "server.h"

static char *gServerHost = NULL;
static int gServSock = -1;
static FILE *gRdServ;
static FILE *gWrServ;


int pobox_connect()
{
	int sock;
	struct sockaddr_in hostaddr;
	struct hostent *entry;
	struct servent *serv;
	struct protoent *proto;
	int a1, a2, a3, a4;
	char *hostname;

	if (gServerHost)
		hostname = gServerHost;
	else if ((hostname = getenv("POBOXSERVER")) == NULL) {
#ifdef POBOX_SERVER_HOST
		hostname = POBOX_SERVER_HOST;
#else
		return -1;
#endif
	}
	if (strcmp(hostname, "pbserver") == 0) {
		return -1;	/* specified not to use pbserver */
	}

	serv = getservbyname(POBOX_SERVICENAME, "tcp");
	bzero((char *) &hostaddr, sizeof(struct sockaddr_in));
	if ((proto = getprotobyname("tcp")) == NULL) {
		return -1;
	}

	if ('0' <= *hostname && *hostname <= '9') {
		if (sscanf(hostname, "%d.%d.%d.%d", &a1, &a2, &a3, &a4) != 4) {
			return -1;
		}
		a1 = (a1 << 24) | (a2 << 16) | (a3 << 8) | a4;
		hostaddr.sin_addr.s_addr = htonl(a1);
	} else {
		if ((entry = gethostbyname(hostname)) == NULL) {
			return -1;
		}
		bcopy(entry->h_addr, &hostaddr.sin_addr, entry->h_length);
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, proto->p_proto)) < 0) {
		return -1;
	}
	hostaddr.sin_family = AF_INET;
	hostaddr.sin_port = serv ? serv->s_port : htons(POBOX_PORT_NUMBER);
	if (connect(sock, (struct sockaddr *) &hostaddr, sizeof(struct sockaddr_in)) < 0) {
		close(sock);
		return -1;
	}
	gServSock = sock;
	gRdServ = fdopen(sock, "r");
	gWrServ = fdopen(sock, "w");
	return 0;
}


void pobox_disconnect()
{
	if (gServSock >= 0) {
		fprintf(gWrServ, "0\n");
		fflush(gWrServ);
		fclose(gWrServ);
		fclose(gRdServ);
		close(gServSock);
		gServSock = -1;
	}
}


int pobox_getcands(char *yomi, char *candstr, int len, int mode)
{
	char r;
	int i;
	int ret;

	if (mode) {
		fprintf(gWrServ, "1%s \n", yomi);
	} else {
		fprintf(gWrServ, "1%s\n", yomi);
	}
	ret = fflush(gWrServ);
	if (ret < 0) {
		pobox_disconnect();
		pobox_connect();
		if (mode) {
			fprintf(gWrServ, "1%s \n", yomi);
		} else {
			fprintf(gWrServ, "1%s\n", yomi);
		}
		fflush(gWrServ);
	}
	read(gServSock, &r, 1);
	if (r == '1') {		/* succeeded */
		i = read(gServSock, candstr, len - 1);
		candstr[i] = '\0';
		//printf("pobox_getcands recv =  '%s'\n",candstr);
	} else {
		while (read(gServSock, &r, 1) > 0 && r != '\n');
		return 1;
	}
	return 0;
}


void pobox_selected(int key)
{
	char r[2];
#ifdef BROKEN_DICT
	fprintf(gWrServ, "8%d\n", 0);
#else
	fprintf(gWrServ, "8%d\n", key - 1);
#endif
	fflush(gWrServ);
	read(gServSock, &r, 2);
	//printf("pobox_selected: %d(%c)\n", key, r[0]);
}


void pobox_context(char *str)
{
	char r[2];

	if (!str)
		return;

	fprintf(gWrServ, "4%s\n", str);
	fflush(gWrServ);
	read(gServSock, &r, 2);
}


void pobox_regword(char *word, char *yomi)
{
	char r[2];

	if (!word || !yomi)
		return;

	fprintf(gWrServ, "5%s %s\n", word, yomi);
	fflush(gWrServ);
	read(gServSock, &r, 2);
}


void pobox_delword(char *word, char *yomi)
{
	char r[2];

	if (!word || !yomi)
		return;

	fprintf(gWrServ, "6%s %s\n", word, yomi);
	fflush(gWrServ);
	read(gServSock, &r, 2);
}


void pobox_dicsave()
{
	char r[2];

	fprintf(gWrServ, "%d\n", 7);
	fflush(gWrServ);
	read(gServSock, &r, 2);
}
