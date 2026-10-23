#ifndef SETUP_H
#define SETUP_H

void Global_Init(void);

/* Caractère reçu sur le port série (ST-Link), ou -1 s'il n'y en a pas : ne bloque jamais */
int Console_GetChar(void);

#endif
