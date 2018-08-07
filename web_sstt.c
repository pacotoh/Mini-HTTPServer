#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <regex.h>
#include <time.h>

#define VERSION		24
#define BUFSIZE		8096
#define NSTRINGS      	20
#define ERROR		42
#define LOG		44
#define PROHIBIDO	403
#define NOENCONTRADO	404
#define EXPTIME       	20


struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };


void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

	switch (log_message_type) {
		case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",message, additional_info, errno,getpid());
			break;
		case PROHIBIDO:
			// Enviar como respuesta 403 Forbidden
			(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",message, additional_info);
			break;
		case NOENCONTRADO:
			// Enviar como respuesta 404 Not Found
			(void)sprintf(logbuffer,"NOT FOUND: %s:%s",message, additional_info);
			break;
		case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",message, additional_info, socket_fd); break;
	}

	if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)write(fd,"\n",1);
		(void)close(fd);
	}
	if(log_message_type == ERROR || log_message_type == NOENCONTRADO || log_message_type == PROHIBIDO) exit(3);
}

void process_web_request(int descriptorFichero)
{
	//debug(LOG,"request","Ha llegado una peticion",descriptorFichero);

	//
	// Definir buffer y variables necesarias para leer las peticiones
	//

  char buffer[BUFSIZE+1];
  char cp_buffer[BUFSIZE+1];
  int i, rd, fd;
  char *msg;
  msg = malloc(sizeof(char) * 350);
  int msg_size;
  char *ext;
  int size;
  struct stat st;
  char *sz = malloc(sizeof(char) * 1);

  // Variables para buscar la Cookie dentro de la cabecera
  char *cookie;
  cookie = malloc(sizeof(char) * 1);
  regex_t regex;
  regmatch_t m[1];
  int reti;
  int icookie;
  reti = regcomp(&regex, "sessionToken=", 0);

  // Variables para tratamiento de la fecha de envío de la Cookie
  // y expiración

  time_t rawtime;
  struct tm *info;
  struct tm *info_act;
  char *b_date = malloc(sizeof(char) * 50);
  char *a_date = malloc(sizeof(char) * 50);

	//
	// Leer la petición HTTP
	//

  rd = read(descriptorFichero, buffer, BUFSIZE);


	//
	// Comprobación de errores de lectura
	//

	//
	// Si la lectura tiene datos válidos terminar el buffer con un \0
	//

  if(rd > 0 && rd < BUFSIZE)
		buffer[rd] = '\0';

	//
	// Se eliminan los caracteres de retorno de carro y nueva linea
	//

  for(i = 0; i < BUFSIZE; i++)
    if(buffer[i] == '\n' || buffer[i] == '\r')
      buffer[i] = ' ';


  (void)strcpy(cp_buffer, buffer); // Almacenamos una copia del buffer para modificar

  for(i = 4; i < BUFSIZE; i++) // Nos quedamos con "GET /xxxxx \0"
    if(cp_buffer[i] == ' '){
      cp_buffer[i] = 0;
      break;
    }

    // Si existe una cookie se incrementa en 1. Si no, se inicializa a 1.

    if((reti = regexec(&regex, buffer, 1, m, 0)) == 0){
      strncpy(cookie, buffer+m[0].rm_eo, 1);
      icookie = atoi(cookie) +1;
      cookie[0] = icookie + '0';
    }
    else{
      cookie[0] = '1';
      icookie = 0;
    }

	//
	//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
	//	(Se soporta solo GET)
	//

  //TODO Comprobar si se ha pasado de accesos y denegar en dicho caso

  if(!strncmp(&cp_buffer[0], "GET", 3)){

    debug(LOG, "Ha llegado una petición", cp_buffer, descriptorFichero);

    if(!strncmp(&cp_buffer[0],"GET /\0",6))
		  (void)strcpy(cp_buffer,"GET /index.html");

  //
	//	Cómo se trata el caso de acceso ilegal a directorios superiores de la
	//	jerarquía de directorios
	//	del sistema
	//

    if(!strncmp(&cp_buffer[5], "..", 2)){
      msg = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
      msg_size = strlen(msg);
      (void)write(descriptorFichero, msg, msg_size);

      fd = open("bad_request.html",O_RDONLY);
      while((rd = read(fd, cp_buffer, BUFSIZE)) > 0){
        (void)write(descriptorFichero, cp_buffer, rd);
      }

      debug(ERROR, "error", "Acceso denegado a directorios superiores", descriptorFichero);
    }
    else{

  //
	//	Cómo se trata el caso excepcional de la URL que no apunta a ningún fichero
	//	html
	//

      ext = (char *)0;
    	int buflen = strlen(cp_buffer);
      int len;

    	for(i=0; extensions[i].ext != 0; i++) {
    		len = strlen(extensions[i].ext);
    		if(!strncmp(&cp_buffer[buflen-len], extensions[i].ext, len)) {
    			ext = extensions[i].filetype;
    			break;
    		}
    	}

  //
  //	Evaluar el tipo de fichero que se está solicitando, y actuar en
  //	consecuencia devolviendolo si se soporta o devolviendo el error correspondiente en otro caso
  //

      //No está entre los tipos permitidos
      if(ext == 0){
        msg = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
        msg_size = strlen(msg);
        (void)write(descriptorFichero, msg, msg_size);

        fd = open("bad_request.html",O_RDONLY);
        while((rd = read(fd, cp_buffer, BUFSIZE)) > 0){
          (void)write(descriptorFichero, cp_buffer, rd);
        }

    		debug(ERROR, "error", "Extensión no válida", descriptorFichero);
      }

      //Sí está entre los permitidos
      else{

        if((fd = open(&cp_buffer[5],O_RDONLY)) > 0){

          if(icookie < 5){

            // Calculamos la fecha de expiración de la Cookie
            time(&rawtime);

            info_act = localtime(&rawtime);
            strftime(a_date, 50,"%a, %d %b %Y %H:%M:%S CET", info_act);
            rawtime -= 3600;
            rawtime += EXPTIME;
            info = localtime(&rawtime);
            strftime(b_date, 50,"%a, %d %b %Y %H:%M:%S CET", info);

            // Para calcular la longitud del fichero

            stat(&cp_buffer[5], &st);
            size = st.st_size;
            sprintf(sz, "%d", size);

            // Rellenamos la cabecera
            strcpy(msg, "HTTP/1.1 200 OK\r\nContent-Type: ");
            strcat(msg, ext);
            strcat(msg, "\r\nContent-Length: ");
            strcat(msg, sz);
            strcat(msg, "\r\nSet-Cookie: sessionToken=");
            strcat(msg, cookie);
            strcat(msg, "; Expires=");
            strcat(msg, b_date);
            strcat(msg, "\r\nServer: websstt1371\r\n");
            strcat(msg, "Date: ");
            strcat(msg, a_date);
            strcat(msg, "\r\nConnection: close");
            strcat(msg, "\r\n\r\n");

            msg_size = strlen(msg);
            (void)write(descriptorFichero, msg, msg_size);

  //
  //	En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
  //	correspondiente, y el envio del fichero se hace en blockes de un máximo de  8kB
  //
            while((rd = read(fd, cp_buffer, BUFSIZE)) > 0){
            	(void)write(descriptorFichero, cp_buffer, rd);
            }

          }
          else{
            msg = "HTTP/1.1 429 Too Many Requests\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
            msg_size = strlen(msg);
            (void)write(descriptorFichero, msg, msg_size);

            fd = open("many_requests.html",O_RDONLY);
            while((rd = read(fd, cp_buffer, BUFSIZE)) > 0){
              (void)write(descriptorFichero, cp_buffer, rd);
            }

          }
        }
        //En caso de ser de tipo compatible pero no encontrarse el recurso
        else{
          msg = "HTTP/1.1 404 NOT FOUND\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
          msg_size = strlen(msg);
          (void)write(descriptorFichero, msg, msg_size);

          fd = open("not_found.html",O_RDONLY);
          while((rd = read(fd, cp_buffer, BUFSIZE)) > 0){
            (void)write(descriptorFichero, cp_buffer, rd);
          }

        }
      }
    }
  }

  else{
    // Caso de que no tengamos un GET
    msg = "HTTP/1.1 405 METHOD NOT ALLOWED\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
    msg_size = strlen(msg);
    (void)write(descriptorFichero, msg, msg_size);

    fd = open("method_allowed.html",O_RDONLY);
    while((rd = read(fd, cp_buffer, BUFSIZE)) > 0){
      (void)write(descriptorFichero, cp_buffer, rd);
    }

  }

  free(msg);
  free(cookie);
  free(b_date);
  free(a_date);
  free(sz);
  regfree(&regex);
	close(descriptorFichero);
	exit(1);
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;		// static = Inicializado con ceros
	static struct sockaddr_in serv_addr;	// static = Inicializado con ceros

  // Almacenamos todos los paths prohibidos
	const char *path[] = {"/", "/bin", "/dev", "/etc", "/lib", "/boot", "/sbin"};
	int path_len = sizeof(path)/sizeof(path[0]);

	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor
	//
	//  Verificar que los argumentos que se pasan al iniciar el programa son los esperados
	//

  if(argc != 3){
		(void)printf("USO: web_sstt [PUERTO] [DIRECTORIO]\n");
		exit(4);
	}

  if(atoi(argv[1]) < 8000){
    (void)printf("ERROR: El puerto escogido para el servidor debe ser superior al 8000\n");
    exit(4);
  }

	//
	//  Verificar que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
	//  permisos para ser usado
	//

	if(chdir(argv[2]) == -1){
		(void)printf("ERROR: No se puede cambiar de directorio %s\n",argv[2]);
		exit(4);
	}
	// Hacemos que el proceso sea un demonio sin hijos zombies
	if(fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN); // Ignoramos cuelgues

	debug(LOG,"web server starting...", argv[1] ,getpid());

	/* setup the network socket */
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
		debug(ERROR, "system call","socket",0);

	port = atoi(argv[1]);

	if(port < 0 || port >60000)
		debug(ERROR,"Puerto invalido, prueba un puerto de 1 a 60000",argv[1],0);

	/*Se crea una estructura para la información IP y puerto donde escucha el servidor*/
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); /*Escucha en cualquier IP disponible*/
	serv_addr.sin_port = htons(port); /*... en el puerto port especificado como parámetro*/

	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
		debug(ERROR,"system call","bind",0);

	if( listen(listenfd,64) <0)
		debug(ERROR,"system call","listen",0);

	while(1){
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR,"system call","accept",0);
		if((pid = fork()) < 0) {
			debug(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	// Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			} else { 	// Proceso padre
				(void)close(socketfd);
			}
		}
	}
}
