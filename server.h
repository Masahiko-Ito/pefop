/* $Id: server.h,v 1.1.1.1 2012/11/05 16:25:54 m-ito Exp $
 * Copyright (c) 2002 pobox-linux project
 *
 * from imopenpobox (yanai-san)
*/
#define POBOX_SERVER_HOST "localhost"
#define POBOX_SERVICENAME "pbserver"
#define POBOX_PORT_NUMBER 1178

int pobox_connect();
void pobox_disconnect();
int pobox_getcands(char *yomi, char *candstr, int len, int mode);
void pobox_selected(int key);
void pobox_context(char *str);
void pobox_regword(char *word, char *yomi);
void pobox_delword(char *word, char *yomi);
void pobox_dicsave();
