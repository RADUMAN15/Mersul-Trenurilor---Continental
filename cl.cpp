#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <iostream>
#include <csignal>

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"

/*
char command_logic[1024]={
COMMAND LOGIC:
TRAIN SCHEDULE(24H)  -> command syntax: SCHEDULE : TrainID\n
STATUS ARRIVAL(1H)   -> command syntax: ARRIVAL STATUS : StationID\n
STATUS DEPARTURE(1H) -> command syntax: DEPARTURE STATUS : StationID\n
ARRIVAL LATE         -> command syntax: ARRIVAL LATE : TrainID : StationID : TIME\n
ARRIVAL EARLY        -> command syntax: ARRIVAL EARLY : TrainID : StationID : TIME\n
DEPARTURE LATE       -> command syntax: DEPARTURE LATE : TrainID : StationID : TIME\n
}
*/
char command_logic_v2[1024]="\n--------------------\nTRAIN SCHEDULE(24H)  -> command syntax: 1 : TrainID\nSTATUS ARRIVAL(1H)   -> command syntax: 2 : StationID\nSTATUS DEPARTURE(1H) -> command syntax: 3 : StationID\nARRIVAL LATE         -> command syntax: 4 : TrainID : StationID : TIME\nARRIVAL EARLY        -> command syntax: 5 : TrainID : StationID : TIME\nDEPARTURE LATE       -> command syntax: 6 : TrainID : StationID : TIME\n--------------------\n\n";

/* codul de eroare returnat de anumite apeluri */
// extern int errno;

/* portul de conectare la server*/
int port;

void signalHandler(int signum) {

    //std::cout << "\nCtrl+C detected. Do something before exiting..." << std::endl;
    std::cout <<" doesn't quit client( try 'exit' )\n[client]Introduceti o comanda: ";//In order to quit write 'exit'\n";
    fflush(stdout);
    //std::cout <<"[client]Introduceti o comanda: ";
}

int main(int argc, char *argv[])
{
    int sd;                    // descriptorul de socket
    struct sockaddr_in server; // structura folosita pentru conectare
                               // mesajul trimis
    int nr = 0;
    char buf[50];

    /* exista toate argumentele in linia de comanda? */
    if (argc != 3)
    {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    /* stabilim portul */
    port = atoi(argv[2]);

    /* cream socketul */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare la socket().\n");
        return errno;
    }

    /* umplem structura folosita pentru realizarea conexiunii cu serverul */
    /* familia socket-ului */
    server.sin_family = AF_INET;
    /* adresa IP a serverului */
    server.sin_addr.s_addr = inet_addr(argv[1]);
    /* portul de conectare */
    server.sin_port = htons(port);

    /* ne conectam la server */
    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]Eroare la connect().\n");
        return errno;
    }

    signal(SIGINT, signalHandler);


    while(1){
            
        /* citirea mesajului */
        char c;
        printf("[client]Apasati tasta enter pentru a afisa comenzile: ");
        while(std::cin.get(c))
        {
            if(c == '\n')
            {
                printf("%s%s%s", RESET, command_logic_v2, RESET);
                break;
            }
        }

        printf("[client]Introduceti o comanda: ");
        fflush(stdout);
        bzero(buf, 50);
        read(0, buf, sizeof(buf));
        //std::cin.getline(buf, sizeof(buf));
        buf[strlen(buf)-1]='\0';


        printf("[client]Am citit %s%s%s\n", GREEN, buf, RESET);

        /* trimiterea mesajului la server */
        if (write(sd, &buf, strlen(buf)+1) <= 0)
        {
            perror("[client]Eroare la write() spre server.\n");
            return errno;
        }
        
        
        /* citirea raspunsului dat de server
        (apel blocant pina cind serverul raspunde) */
        char raspuns[1000];
        if (read(sd, &raspuns, sizeof(raspuns)) < 0)
        {
            perror("[client]Eroare la read() de la server.\n");
            return errno;
        }

        /* afisam mesajul primit */
        printf("[client]Mesajul primit este: %s%s%s\n", YELLOW, raspuns, RESET);

        if(strcmp(buf, "exit") == 0)
        {   
            //printf("DISCONNECTING...\n");
            break;
        }
    }
    
    /* inchidem conexiunea, am terminat */
    close(sd);
    return 0;
}