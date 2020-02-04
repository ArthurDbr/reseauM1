#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS	10
#define LENGTH_SEND 201
#define LENGTH_SEND_ALL 2048


static unsigned int cli_count = 0;
static int id = 10;
static int isPenduStart = 0;
static char mot[255];
static char motTrouve[255];
static char chaine[255];
static int idJoueurLanceurJeu = -1;
static int nbCoupsRestant = 15;

/* Client structure */
typedef struct {
	struct sockaddr_in addr;	/* adresse Client à distance */
	int connfd;					/* Connection file descriptor */
	int id;						/* id du Client unique */
	char name[32];				/* nom du Client */
} client_struct;

client_struct *clients[MAX_CLIENTS];

/* Ajoute le client a la file */
void ajouter_client_queue(client_struct *cl){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(!clients[i]){
			clients[i] = cl;
			return;
		}
	}
}

/* Supprime le client de la file */
void supprimer_client_queue(int id){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->id == id){
				clients[i] = NULL;
				return;
			}
		}
	}
}

/* Envois un message à tous les clients sauf l'emetteur */
void envoie_mess_client(char *s, int id){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->id != id){
				if(write(clients[i]->connfd, s, LENGTH_SEND)<0){
					perror("write");
					exit(-1);
				}
			}
		}
	}
}

/* Envois un message à tous les clients */
void envoie_mess_clients(char *s){
	int j;
	for(j=0;j<MAX_CLIENTS;j++){
		if(clients[j]){
			if(write(clients[j]->connfd, s, LENGTH_SEND)<0){
				perror("write");
				exit(-1);
			}
		}
	}
}

/* Envois un message à l'emetteur */
void envoie_message_a_soi_meme(const char *s, int connfd){
	if(write(connfd, s, strlen(s))<0){
		perror("write");
		exit(-1);
	}
}

/* Envois un message aux clients */
void send_message_client(char *s, int id){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->id == id){
				if(write(clients[i]->connfd, s, strlen(s))<0){
					perror("write");
					exit(-1);
				}
			}
		}
	}
}


/* Enleve les retour chariot */
void strip_newline(char *s){
	while(*s != '\0'){
		if(*s == '\r' || *s == '\n'){
			*s = '\0';
		}
		s++;
	}
}

/* Affiche l'adresse ip */
void affiche_client_addr(struct sockaddr_in addr){
	printf("%d.%d.%d.%d\n",
		addr.sin_addr.s_addr & 0xFF,
		(addr.sin_addr.s_addr & 0xFF00)>>8,
		(addr.sin_addr.s_addr & 0xFF0000)>>16,
		(addr.sin_addr.s_addr & 0xFF000000)>>24);
}

void init_motTrouve(char mot[]){
	for(int x = 0; x < strlen(mot); x++){
		motTrouve[x] = '_';
	}
	motTrouve[strlen(mot)-1] = '\n';
}

/* Gere la communication avec tous les clients */
void *handle_client(void *arg){
	char buff_out[2048];
	char buff_in[1024];
	int rlen;

	cli_count++;
	client_struct *client= (client_struct *)arg;


	printf("<<Nouveau client sur \r\n");
	affiche_client_addr(client->addr);
	printf("id client %d\r\n", client->id);

	sprintf(buff_out, "<<Client : %s connectee. \n\r", client->name);
	envoie_mess_clients(buff_out);

	/* Recois les donnees du client */
	while((rlen = read(client->connfd, buff_in, sizeof(buff_in)-1)) > 0){
	        buff_in[rlen] = '\0';
	        buff_out[0] = '\0';
		strip_newline(buff_in);

		/* Ignore les buffer vide */
		if(!strlen(buff_in)){
			continue;
		}
		/* Options specials */
		if(buff_in[0] == '\\'){
			char *command, *param;
			command = strtok(buff_in," ");
			if(!strcmp(command, "\\QUIT")){
				break;
			}else if(!strcmp(command, "\\PING")){
				envoie_message_a_soi_meme("<<PONG\r\n", client->connfd);
			}else if(!strcmp(command, "\\RENAME")){
				param = strtok(NULL, " ");
				if(param){
					char *old_name = strdup(client->name);
					strcpy(client->name, param);
					sprintf(buff_out, "<<RENAME, %s EN %s\r\n", old_name, client->name);
					free(old_name);
					envoie_mess_clients(buff_out);
				}else{
					envoie_message_a_soi_meme("<<Le nom ne peut etre null\r\n", client->connfd);
				}
			}else if(!strcmp(command, "\\LIST")){
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(clients[i]){
						sprintf(buff_out, "<<Id : %d Nom : %s\r\n", clients[i]->id, clients[i]->name);
						envoie_message_a_soi_meme(buff_out, client->connfd);
					}
				}
				strcat(buff_out, "\r\n");
			}else if(!strcmp(command, "\\HELP")){
				strcat(buff_out, "\\HELP     Liste commandes\r\n");
				strcat(buff_out, "\\QUIT     Quitte le chatroom\r\n");
				strcat(buff_out, "\\PING     Interroge le serveur\r\n");
				strcat(buff_out, "\\RENAME   <name> Change le surnom\r\n");
				strcat(buff_out, "\\PENDU    Lancer le jeu\r\n");
				// strcat(buff_out, "\\PRIVATE  <id dest> <msg> message prive\r\n");
				strcat(buff_out, "\\LIST     liste clients actifs\r\n");
				envoie_message_a_soi_meme(buff_out, client->connfd);
			}else if(!strcmp(command,"\\PENDU")){
				strcat(chaine, "<<Le joueur ");
				strcat(chaine, client->name);
				strcat(chaine," a lancé le jeu \r\n" );
				idJoueurLanceurJeu = client->id;
				envoie_mess_client(chaine, client->id);
				envoie_message_a_soi_meme("<<Veuillez choisir un mot \r\n", client->connfd);
				isPenduStart = 1;
			}
			else{
				envoie_message_a_soi_meme("<<commande inconnu \r\n", client->connfd);
			}
		}else{
			/*Le pendu est lancé*/
			if(isPenduStart >= 1){
				if(isPenduStart == 1){

					strcpy(mot, buff_in);
					mot[strlen(mot)] = '\n';
					// Initialise le mot à troue
					init_motTrouve(mot);
					isPenduStart++;
					memset (chaine, 0, sizeof (chaine));
					strcat(chaine, "<<Le joueur ");
					strcat(chaine, client->name);
					
					strcat(chaine," a choisi le mot à vous de jouer ! Coups restant : " );
					sprintf(chaine, "%s%d",chaine, nbCoupsRestant);
					strcat(chaine, "\r\n");
					envoie_mess_client(chaine, client->id);
					
				}else if(isPenduStart == 2){
					// condition A exporter dans le client
					if(strlen(buff_in) > 1){
						envoie_message_a_soi_meme("<<Vous ne pouvez pas dépasser 1 caractère !\r\n", client->connfd);
					}else{
						// Regarde si ce n'est pas le lanceur du jeu qui joue
						if(idJoueurLanceurJeu != client->id){
							// Test si on a encore des coups à jouer
							if(nbCoupsRestant >= 0){
								// Ajoute une lettre au mot à troue
							int lettreTrouve = 0;
							nbCoupsRestant--;
							for(int x = 0; x < strlen(mot); x++){
								if(mot[x] == buff_in[0] ){
									lettreTrouve = 1;
									motTrouve[x] = mot[x];
									
									memset (chaine, 0, sizeof (chaine));
									strcat(chaine, "Lettre : ");
									strncat(chaine, &buff_in[0], 1);
									strcat(chaine, " trouvée \r\n");
									envoie_mess_clients(chaine);
								}	
							} 
							if(lettreTrouve == 0){
								memset (chaine, 0, sizeof (chaine));
								strcat(chaine, "La lettre : ");
								strncat(chaine, &buff_in[0], 1);
								strcat(chaine, " n'est pas présente dans le mot \r\n");
								envoie_mess_clients(chaine);
							}
							memset (chaine, 0, sizeof (chaine));
							strcat(chaine, "Coups restant : ");
							sprintf(chaine, "%s%d",chaine, nbCoupsRestant);
							strcat(chaine, "\r\n");
							envoie_mess_clients(chaine);
							if(strcmp(motTrouve, mot) == 0){
								envoie_mess_clients("mot trouvé ! Vous pouvez relancer une partie \r\n");
								isPenduStart = 0;
								idJoueurLanceurJeu = -1;
								nbCoupsRestant = 15;
							}
							envoie_mess_clients(motTrouve );
							}else{
								isPenduStart = 0;
								idJoueurLanceurJeu = -1;
								nbCoupsRestant = 15;
								envoie_mess_clients("Partie perdue ! \n\r");
							}
							
						}else{
							envoie_message_a_soi_meme("Veuillez attendre que les autres aient trouvés votre mot ! \r\n", client->connfd);
						}

					}
				}
				if(isPenduStart == 3){
					envoie_mess_client("Le jeu est terminé, vous pouvez le relancer is vous souhaitez !\r\n", client->connfd);
				}

			}else{
				/* Envois un message */
				snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", client->name, buff_in);
				envoie_mess_client(buff_out, client->id);	
			}
		}
	}

	/* Ferme les connections */
	sprintf(buff_out, "<<A plus ! %s\r\n", client->name);
	envoie_mess_clients(buff_out);
	close(client->connfd);

	/* Supprime un client de la file et supprime le thread */
	supprimer_client_queue(client->id);
	printf("<<Aurevoir ");
	affiche_client_addr(client->addr);
	printf(" id client %d\r\n", client->id);
	free(client);
	cli_count--;
	pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char *argv[]){
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	/* Paramètre des sockets */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(5000);

	/* Ignore les pipe signals */
	signal(SIGPIPE, SIG_IGN);

	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
		perror("Echec de l'association de socket");
		return 1;
	}

	/* Ecoute */
	if(listen(listenfd, 10) < 0){
		perror("Echec de l'ecoute de socket");
		return 1;
	}

	printf("<[SERVEUR DEMARRE]>\n");

	/* Accepter les clients */
	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Regarde si on peut encore accepter des clients */
		if((cli_count+1) == MAX_CLIENTS){
			printf("<<CLIENTS MAX ATTEINT\n\r");
			printf("<<REJETE \n\r");
			affiche_client_addr(cli_addr);
			printf("\n");
			close(connfd);
			continue;
		}

		/* Parametre des clients */
		client_struct *client= (client_struct *)malloc(sizeof(client_struct));
		client->addr = cli_addr;
		client->connfd = connfd;
		client->id = id++;
		sprintf(client->name, "%d", client->id);

		/* ajoute le client à la file et fork un thread */
		ajouter_client_queue(client);
		pthread_create(&tid, NULL, &handle_client, (void*)client);

		/* Reduit l'usage du CPU */
		sleep(1);
	}
}
