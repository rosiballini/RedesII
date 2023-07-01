#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <arpa/inet.h>

#define _POSIX_C_SOURCE 200809L

#define BUFSIZE 512 // tamaño máximo para recibir los datos del cliente
#define CMDSIZE 4
#define PARSIZE 100

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"
#define MSG_150 "150 Opening BINARY mode data connection for %s (%ld bytes)\r\n"
#define MSG_200 "200 PORT command successful\r\n"


/*
 Función: recv_cmd

 Se encarga de recibir y analizar los comandos enviados por el cliente. 
 Toma el descriptor de socket sd en el que se recibirá el comando, 
 una cadena de caracteres  operation para almacenar el comando y 
 una cadena de caracteres param para almacenar los parámetros del comando (si los hay). 
 La función devuelve true si se recibió y procesó correctamente el comando, y false en caso contrario.
 */

bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // Recibir el comando en el buffer y manejar errores
    if ((recv_s = read(sd, buffer, BUFSIZE)) < 0) {
        warnx("Error reading buffer"); 
        return false;
    }
    // Si el buffer está vacío, se imprime mensaje de advertencia
    if (recv_s == 0) {
        warnx("Empty buffer");
        return false;
    }

    // Eliminar los caracteres de terminación del buffer
    buffer[strcspn(buffer, "\r\n")] = 0;

    // Analizar el buffer para extraer el comando y los parámetros
    // Se verifica si se recibió correctamente el comando. 
    // Si el comando no es válido, se imprime un mensaje de advertencia y se devuelve false.
    token = strtok(buffer, " ");
    if (token == NULL || strlen(token) < 4) {
        warn("not valid ftp command");
        return false;
    } else {
        if (operation[0] == '\0') strcpy(operation, token);
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        if (token != NULL) strcpy(param, token);
    }
    return true;
}


/**
 Función: send_ans 

 Esta función se utiliza para enviar una respuesta al cliente a través del descriptor de socket especificado. 
 Toma el descriptor de socket sd en el que se enviará la respuesta, 
 una cadena de caracteres message que representa la respuesta formateada y 
 variables adicionales para formatear la cadena de caracteres. 
 La función devuelve true si se envió correctamente la respuesta, y false en caso contrario.
 */

bool send_ans(int sd, char *message, ...) {
    char buffer[BUFSIZE];

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);

    // Enviar la respuesta preformateada y verificar errores
    if (write(sd, buffer, strlen(buffer)) < 0) {
        warn("Error sending message");
        return false;
    }

    return true;
}


/*
 Función: retr
 
 Esta función maneja el comando RETR (retrieve) para enviar un archivo al cliente. 
 Abre el archivo, Toma el descriptor de socket sd en el que se enviará el archivo y la ruta del 
 archivo a enviar (file_path)., envía su contenido al cliente y cierra el archivo.
Se declaran: 
    - un puntero a FILE para representar el archivo que se enviará al cliente.
    - bread para almacenar la cantidad de bytes leídos del archivo, 
    - fsize para almacenar el tamaño del archivo y 
    - buffer para almacenar temporalmente los datos del archivo antes de enviarlos al cliente.
 */

void retr(int sd, char *file_path) {
    FILE *file;
    int bread;
    long fsize;
    char buffer[BUFSIZE];

    // Verificar si el archivo existe abriéndolo en modo lectura; si no, informar error al cliente
    file = fopen(file_path, "r");
    if (file == NULL) {
        warn("Error opening file");
        send_ans(sd, MSG_550, file_path);
        return;
    }

    // Enviar un mensaje de éxito con el tamaño del archivo
    fseek(file, 0L, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0L, SEEK_SET);
    send_ans(sd, MSG_299, file_path, fsize);

    // Importante retraso para evitar problemas con el tamaño del búfer
    sleep(1);

    // Se lee el archivo en bloques de tamaño BUFSIZE utilizando la función fread 
    // y se envía cada bloque leído al cliente utilizando la función write si no ocurren problemas
    while ((bread = fread(buffer, 1, BUFSIZE, file)) > 0) {
        if (write(sd, buffer, bread) < 0) {
            warn("Error sending file");
            fclose(file);
            return;
        }
    }

    // Cerrar el archivo
    fclose(file);

    // Enviar un mensaje de transferencia completada
    send_ans(sd, MSG_226);
}


/*
Función: check_credentials

Esta función verifica las credenciales de usuario y contraseña proporcionadas. 
Busca la combinación de usuario y contraseña en un archivo llamado "ftpusers". 
Toma las cadenas de caracteres user y pass que representan el nombre de usuario 
y la contraseña a verificar. 
Devuelve true si las credenciales son válidas, y false en caso contrario.
 */

bool check_credentials(char *user, char *pass) {
    FILE *file;
    char *path = "./ftpusers", *line = NULL, credentials[100];
    size_t line_size = 0;
    bool found = false;

    // Crear la cadena de credenciales
    sprintf(credentials, "%s:%s", user, pass);

    // Verificar si el archivo "ftpusers" está presente abriéndolo en modo lectura
    if ((file = fopen(path, "r")) == NULL) {
        warn("Error opening %s", path);
        return false;
    }

    // Buscar la cadena de credenciales línea por línea. Se lee cada línea y se 
    // compara con la cadena de credenciales
    while (getline(&line, &line_size, file) != -1) {
        strtok(line, "\n");
        if (strcmp(line, credentials) == 0) {
            found = true;
            break;
        }
    }

    // Cerrar el archivo y liberar cualquier puntero necesario
    fclose(file);
    if (line) free(line);

    // Devolver el estado de búsqueda
    return found;
}


/*
 Función: authenticate
 
Esta función se encarga de autenticar al cliente verificando las credenciales proporcionadas. 
Espera recibir los comandos USER y PASS del cliente y los verifica. 
Toma el descriptor de socket sd para comunicarse con el cliente. 
Devuelve true si la autenticación es exitosa, y false en caso contrario.
*/

bool authenticate(int sd) {
    char user[PARSIZE], pass[PARSIZE];

    // Esperar a recibir el comando USER
    if (!recv_cmd(sd, "USER", user)) return false;

    // Solicitar contraseña
    send_ans(sd, MSG_331, user);

    // Esperar a recibir el comando PASS
    if (!recv_cmd(sd, "PASS", pass)) return false;

    // Si las credenciales no son válidas, denegar el inicio de sesión
    if (!check_credentials(user, pass)) {
        send_ans(sd, MSG_530);
        return false;
    }

    // Confirmar inicio de sesión
    send_ans(sd, MSG_230);
    return true;
}


/*
 Función: operate

Maneja la operación principal del servidor FTP. 
Espera recibir comandos del cliente y los procesa en un bucle infinito. 
Soporta los comandos RETR (recuperar archivo) y QUIT (cerrar conexión).
sd: descriptor de socket para comunicarse con el cliente
 */

void operate(int sd) {
    // Almacenar el comando y los parámetros enviados por el cliente
    char op[CMDSIZE], param[PARSIZE];

    while (true) {
        op[0] = param[0] = '\0';

        // Verificar si se reciben comandos del cliente mediante recv_cmd; 
        // si no mediante send_ans, se informa y se sale
        if (!recv_cmd(sd, op, param)) {
            send_ans(sd, MSG_221);
            break;
        }

        //Si el comando recibido es "RETR", se llama a la función retr para manejar 
        // la operación de recuperar un archivo 
        if (strcmp(op, "RETR") == 0) {
            retr(sd, param);
        } else (strcmp(op, "QUIT") == 0); {
            // Enviar mensaje de despedida y cerrar la conexión
            send_ans(sd, MSG_221);
            close(sd);
            break;
        } 
    }
}


/*
Funcion: port
 
Se encarga de extraer la dirección IP y el puerto de los datos del socket enviados 
por el cliente durante el comando PORT y construye una estructura sockaddr_in
que representa esa dirección y puerto. Esta estructura se utilizará más adelante 
para establecer la conexión de datos entre el cliente y el servidor FTP.
*/

// addr de tipo struct sockaddr_in se utilizará para almacenar la dirección IP y el puerto extraídos.
// Se asigna memoria dinámicamente para las variables ip, aux1 y aux2. 

struct sockaddr_in port(int sd, char *socketdata){
    struct sockaddr_in addr;
    int puerto, i, j, count;
    char *ip, *aux1, *aux2;
    ip = (char*)malloc(30*sizeof(char));
    aux1 = (char*)malloc(4*sizeof(char));
    aux2 = (char*)malloc(4*sizeof(char));

    i = j = 0;
    count=0;

    // Se inicia un bucle para extraer la dirección IP y el puerto de los datos 
    // del socket enviados por el cliente que verificará si el carácter actual es una coma (','). 
    // Si es así, se incrementa el contador count.
    while(true){
        if (*(socketdata+i) == ',') count++;

        if (count<=3){
            *(ip+j) = *(socketdata+i);
            if (*(ip+j) == ',') *(ip+j) = '.';
            j++;
        }
        if (*(socketdata+i) == ',' && count==4){
            *(ip+j) = '\0';
            j=0;
            i++;
            continue;
        }

        if(count==4){
            *(aux1+j) = *(socketdata+i);
            j++;
        }

        if (*(socketdata+i) == ',' && count==5){
            *(aux1+j) = '\0';
            j=0;
            i++;
            continue;
        }

        if(count==5){
            *(aux2+j) = *(socketdata+i);
            j++;
        }

        // Si el carácter actual es el carácter nulo ('\0'), se agrega al final del arreglo aux2 
        // para finalizar la cadena y se rompe el bucle while.
        if (*(socketdata+i) == '\0'){
            *(aux2+j) = '\0';
            break;
        }
        i++;
    }
    puerto = 256 * atoi(aux1) + atoi(aux2);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(puerto);

    // Se libera memoria asignada dinámicamente
    free(ip);
    free(aux1);
    free(aux2);

    send_ans(sd, MSG_200);

    return addr;
}


/*
 Función: stor
 
 Se encarga de recibir un archivo enviado por el cliente a través de una conexión de datos. *
 sd: descriptor de socket de la conexión de control.
 addr: la estructura sockaddr_in que contiene la información de la conexión de datos.
 file_data: Los datos del archivo que se van a recibir.
 */

void stor(int sd, struct sockaddr_in addr, char *file_data) {
    FILE *file;
    long f_size, recv_s, r_size = BUFSIZE;
    char buffer[BUFSIZE];
    int srcsd;
    char *file_path, *file_size, *aux;

    // Reserva memoria para las variables auxiliares que contienen nombre de archivo y su tamaño
    file_path = (char*)malloc(50*sizeof(char));
    file_size = (char*)malloc(25*sizeof(char));

    // Extrae el nombre del archivo y su tamaño de los datos del archivo
    aux = strtok(file_data, "//");
    strcpy(file_path, aux);
    aux = strtok(NULL, "//");
    strcpy(file_size, aux);
    f_size = atoi(file_size);

    // Envía una respuesta al cliente indicando que el servidor está listo para recibir el archivo
    send_ans(sd, MSG_150, file_path);

    // Abre una conexión al cliente a través del socket de datos
    srcsd = socket(AF_INET, SOCK_STREAM, 0);
    if (srcsd < 0) errx(2, "Cannot create socket");

    if (connect(srcsd, (struct sockaddr *) &addr, sizeof(addr)) < 0) errx(3, "Error on connect to data channel");

    // Abre el archivo en modo escritura para escribir en él
    file = fopen(file_path, "w");

    // Recibe el archivo en bloques y escribe los datos en el archivo local
    while (true) {
        if (f_size < BUFSIZE) r_size = f_size;

        // Lee los datos del socket de datos
        recv_s = read(srcsd, buffer, r_size);
        if (recv_s < 0) warn("receive error");

        // Escribe los datos recibidos en el archivo
        fwrite(buffer, 1, r_size, file);

        if (f_size < BUFSIZE) break;
        f_size = f_size - BUFSIZE;
    }

    // Cierra la conexión al cliente
    close(srcsd);

    // Envía un mensaje indicando que la transferencia del archivo se ha completado
    send_ans(sd, MSG_226);

    // Libera la memoria reservada
    free(file_path);
    free(file_size);

    return;
}


/*
Función: direccion_puerto

Verifica si una cadena de caracteres representa un número de puerto 
válido a través de un valor booleano.
return true si la cadena representa un número de puerto válido, false de lo contrario.
 */

bool direccion_puerto(char *string){
    bool verificacion = true;
    int i=0;
    while(*(string+i)!='\0'){
        if(!isdigit(*(string+i))) verificacion = false;
        i++;
    }
    if(atoi(string)<0 || atoi(string)>65535) verificacion = false;
    return verificacion;
}


/*
Función: sig_handler

Manejador de señales para la señal SIGCHLD.
sig El número de la señal recibida.
 */

void sig_handler(int sig){
    if(sig == SIGCHLD){
        waitpid(-1, NULL, WNOHANG);
    }
}


int main(int argc, char *argv[]) {
    // Verificación de argumentos
    if (argc < 2) {
        errx(1, "Port expected as argument");
    } else if (argc > 2) {
        errx(1, "Too many arguments");
    }

    // Reservar espacio para sockets y variables
    int master_sd, slave_sd;
    struct sockaddr_in master_addr, slave_addr;

    // Crear el socket del servidor y comprobar errores
    if ((master_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        err(1, "Error creating socket");
    }

    // Establecer las opciones del socket maestro
    int optval = 1;
    if (setsockopt(master_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        err(1, "Error setting socket options");
    }

    // Asignar dirección al socket maestro y comprobar errores
    memset(&master_addr, 0, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(atoi(argv[1]));
    master_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(master_sd, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
        err(1, "Error binding socket");
    }

    // Establecer el socket en modo de escucha
    if (listen(master_sd, SOMAXCONN) < 0) {
        err(1, "Error listening on socket");
    }

    // Bucle principal
    while (true) {
        pid_t pid;
        // Aceptar conexiones secuencialmente y comprobar errores
        socklen_t slave_addr_len = sizeof(slave_addr);
        if ((slave_sd = accept(master_sd, (struct sockaddr *)&slave_addr, &slave_addr_len)) < 0) {
            err(1, "Error accepting connection");
        }


        signal(SIGCHLD, sig_handler);

        pid = fork();
        if(pid == 0){
            close(master_sd);
            break;
        }

        // Enviar saludo al cliente
        send_ans(slave_sd, MSG_220);

        // Autenticar al cliente
        if (authenticate(slave_sd)) {
            // Operar solo si la autenticación es exitosa
            operate(slave_sd);
        }

        // Cerrar el socket del cliente
        close(slave_sd);
    }

    // Cerrar el socket del servidor
    close(master_sd);

    return 0;
}
