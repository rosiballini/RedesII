#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include<ctype.h>

#define BUFSIZE 512


/*
Función: recv_msg

Esta función recibe un mensaje del servidor FTP y verifica el código de respuesta. 
Toma el descriptor de socket sd para la conexión FTP, 
el código de respuesta esperado code y 
un puntero a un búfer de texto opcional text para almacenar el mensaje recibido. 
Utiliza la función recv() para recibir datos del servidor, analiza el código de respuesta 
y el mensaje recibido y los muestra por pantalla. 
Devuelve true si el código de respuesta coincide con el esperado, de lo contrario devuelve false.
 */

bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // Recibe una respuesta
    recv_s = recv(sd, buffer, BUFSIZE, 0);

    // Verifica si hay errores
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // Analizando el código y el mensaje recibido de la respuesta
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // Copia opcional de parámetros
    if(text) strcpy(text, message);
    // Test booleano para "code"
    return (code == recv_code) ? true : false;
}


/*
Función: send_msg
Esta función envía un mensaje al servidor FTP en el formato adecuado. 
Toma el descriptor de socket sd para la conexión FTP, 
la operación/comando a enviar al servidor llamada operation y 
un parámetro opcional param para la operación. 
Formatea el comando y lo envía al servidor utilizando la función send().

 */
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";

    // Formateo de comandos
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    // Envia comando y verifica si hay errores
    if (send(sd, buffer, strlen(buffer), 0) < 0)
        err(1, "error sending data");

}


/*
Función: read_input

Esta función realiza una lectura simple desde el teclado. 
Lee una línea de entrada del usuario (stdin) utilizando fgets() y 
luego elimina el carácter de salto de línea ("\n") utilizando strtok().
*/

char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}


/*
Función: authenticate
Esta función se encarga del proceso de inicio de sesión en el servidor FTP 
desde el lado del cliente. Solicita al usuario un nombre de usuario y contraseña, 
envía los comandos USER y PASS al servidor y 
espera las respuestas correspondientes. 
Si las respuestas son exitosas (códigos 331 y 230), el proceso de inicio de sesión se considera válido.
*/

void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // Pregunta al usuario
    printf("username: ");
    input = read_input();

    // Envía el comando al servidor
    send_msg(sd, "USER", input);
    
    // Libera memoria
    free(input);

    // Espera a recibir contraseña requerida y verifica si hay errores
    if (!recv_msg(sd, 331, desc))
        errx(1, "unexpected response from server");

    // Pide la contraseña
    printf("passwd: ");
    input = read_input();

     // Envía el comando al servidor
    send_msg(sd, "PASS", input);

    // Libera memoria
    free(input);

    // Espera a recibir respuesta y verifica si hay errores
    if (!recv_msg(sd, 230, desc))
        errx(1, "unexpected response from server");

}

/*
Función: port

Esta función envía el comando PORT al servidor para establecer una conexión 
de datos utilizando un número de puerto y dirección IP específicos. 
Utiliza la función sprintf() para formatear los parámetros del comando y 
luego envía el comando al servidor. 
Espera una respuesta exitosa (código 200) del servidor.
*/

bool port(int sd, char *ip, int port) {
    char desc[BUFSIZE];
    int code;

    // Envía el comando PORT al servidor
    sprintf(desc, "%s,%d,%d", ip, port/256, port%256);
    send_msg(sd, "PORT", desc);

    // Espera por la respuesta y la procesa. Verifica si hay errores
    if (!recv_msg(sd, 200, desc))
        errx(1, "unexpected response from server");

    return true;
}


/*
Función: get

sd: descriptor de socket de la conexión de control
file_name: nombre del archivo a obtener del servidor 

Esta función se encarga de descargar un archivo desde el servidor FTP. 
Utiliza el comando PORT para establecer una conexión de datos y configurar 
un socket para escuchar conexiones entrantes. 
Luego, envía el comando RETR al servidor con el nombre del archivo que se desea descargar. 
Recibe los datos del archivo a través del canal de datos y los escribe en un archivo local. 
Finalmente, cierra los sockets y el archivo y espera la confirmación del servidor.
*/

void get(int sd, char *file_name) {
   char buffer[BUFSIZE];
    long f_size, recv_s, r_size = BUFSIZE;
    FILE *file;
    // Toma de canal de datos
    int dsd, dsda;
    struct sockaddr_in addr, addr2;
    socklen_t addr_len = sizeof(addr);
    socklen_t addr2_len = sizeof(addr2);
    int puerto;
    char *ip;
    ip = (char*)malloc(13*sizeof(char));


    // Obtener la dirección IP local y el número de puerto asociados al descriptor de socke
    getsockname(sd, (struct sockaddr *) &addr, &addr_len);
    ip = inet_ntoa(addr.sin_addr);

    // Se genera aleatoriamente un número de puerto entre 1024 y 65535, y se asigna a la variable
    puerto = rand()%60000+1024;

    if(!port(sd, ip, puerto)) {
       printf("Invalid server answer\n");
       return;
    }

    // Escuchar el canal de datos (default idem port)
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(2, "Cannot create socket");
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(puerto);
        if (bind(dsd, (struct sockaddr *) &addr2, sizeof(addr2)) < 0) errx(4,"Cannot bind");
        if (listen(dsd,1) < 0) errx(5, "Listen data channel error");

    // Envía el comando RETR al servidor con el nombre del archivo que se desea descargar
    send_msg(sd, "RETR", file_name);
    // Chequea la respuesta
    if(!recv_msg(sd, 299, buffer)) {
       close(dsd);
       return;
    }

    // Acepta nueva conexión
    dsda = accept(dsd, (struct sockaddr*)&addr2, &addr2_len);
    if (dsda < 0) {
       errx(6, "Accept data channel error");
    }

    // Analiza el tamaño del archivo de la respuesta recibida
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %ld bytes", &f_size);

    // Abre el archivo para escribirlo
    file = fopen(file_name, "w");

    // Recibe el archivo
    while(true) {
       if (f_size < BUFSIZE) r_size = f_size;
       recv_s = read(dsda, buffer, r_size);
       if(recv_s < 0) warn("receive error");
       fwrite(buffer, 1, r_size, file);
       if (f_size < BUFSIZE) break;
       f_size = f_size - BUFSIZE;
    }

    // Cierra el canal de datos
    close(dsda);

    // Cierra el archivo
    fclose(file);

    // Recibe el okey por parte del servidor
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // Cierra el socket 
    close(dsd);

    return;

}


/*
 Función: put 

 Esta función se encarga de enviar un archivo al servidor FTP. 
 Verifica si el archivo existe y obtiene su tamaño. 
 Al igual que en la función get, establece una conexión de datos utilizando 
 el comando PORT y configurar un socket para escuchar conexiones entrantes. 
 Envía el comando STOR al servidor junto con el nombre del archivo y su tamaño. 
 Acepta una conexión entrante, lee el archivo y envía los datos al servidor a través del canal de datos. 
 Cierra los sockets y archivos utilizados y espera la confirmación del servidor.
 */

void put(int sd, char *file_name) {
    char buffer[BUFSIZE];
    long f_size;
    FILE *file;
    // Toma de canal de datos
    int dsd, dsda;
    struct sockaddr_in addr, addr2;
    socklen_t addr_len = sizeof(addr);
    socklen_t addr2_len = sizeof(addr2);
    int puerto, bread;
    char *ip;
    char *file_data, *file_size;
    file_data = (char*)malloc(50*sizeof(char));
    file_size = (char*)malloc(25*sizeof(char));

    // Chequea si el archivo existe abriéndolo en modo lectura
    file = fopen(file_name, "r");
    if (file == NULL){
        printf("El archivo no existe.\n");
        return;
    }

    //Tamaño del archivo
    fseek(file, 0L, SEEK_END);
    f_size = ftell(file);
    rewind(file);
    sprintf(file_size, "//%ld",f_size);


    ip = (char*)malloc(13*sizeof(char));
    getsockname(sd, (struct sockaddr *) &addr, &addr_len);
    ip = inet_ntoa(addr.sin_addr);
    puerto = rand()%60000+1024;

    if(!port(sd, ip, puerto)) {
       printf("Invalid server answer\n");
       return;
    }

    // Escucha el canal de datos
    dsd = socket(AF_INET, SOCK_STREAM, 0);
    if (dsd < 0) errx(1, "Cannot create socket");
    addr2.sin_family = AF_INET;
    addr2.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_port = htons(puerto);
    if (bind(dsd, (struct sockaddr *) &addr2, sizeof(addr2)) < 0) errx(4,"Cannot bind");
    if (listen(dsd,1) < 0) errx(5, "Listen data channel error");

    file_data=strcat(file_name,file_size);
    // Envia el comando STOR al servidor 
    send_msg(sd, "STOR", file_data);
    // Verifica la respuesta
    if(!recv_msg(sd, 150, buffer)) {
       close(dsd);
       return;
    }

    // Acepta nuevas conexiones
    dsda = accept(dsd, (struct sockaddr*)&addr2, &addr2_len);
    if (dsda < 0) {
       errx(6, "Accept data channel error");
    }

    // Envía el archivo
    while(!feof(file)) {
        bread = fread(buffer, 1, BUFSIZE, file);
        if (write(dsda, buffer, bread) < 0) warn("Error sending data");
    }

    // Cierra el canal de datos
    close(dsda);

    // Cierra el archivo 
    fclose(file);

    // Recibe OK del servidor 
    if(!recv_msg(sd, 226, NULL)) warn("Abnormally RETR terminated");

    // Cierra el socket
    close(dsd);

    return;
}


/**
 Función: quit

Esta función toma como argumento el descriptor de socket sd, 
que representa la conexión del cliente con el servidor FTP. Luego, se llama a la función 
send_msg para enviar el comando QUIT al servidor FTP a través del descriptor de socket sd. 
 **/

void quit(int sd) {
    // Envía el comando "QUIT" al servidor FTP para finalizar 
     send_msg(sd, "QUIT", NULL);
    // Recibe respuesta del servidor
    if (!recv_msg(sd, 221, NULL))
        errx(1, "unexpected response from server");

}


/*
Función: operate
sd: descriptor de socket de la conexión de control

Esta función establece un bucle continuo en el que el usuario puede ingresar comandos. 
Dependiendo del comando ingresado, se ejecuta la operación correspondiente 
(por ejemplo, “get” para descargar un archivo) o se finaliza la conexión con el servidor (comando "quit").
*/

void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // evitar input vacío
        op = strtok(input, " ");
            if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        } else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
            }
            else {
            // Nuevas operaciones en el futuro
            printf("TODO: unexpected command\n");
            }
        free(input);
    }
    free(input);
}


/*
Función: dirección_IP

Verifica si una cadena de caracteres. Representa una dirección IP válida 
siguiendo ciertas reglas, como la presencia de cuatro subcadenas separadas
por puntos y cada subcadena debe estar en el rango numérico de 0 a 255. 
La función devuelve un valor booleano que indica si la dirección IP es válida o no.
 */

bool direccion_IP(char *string){
    char *token;
    bool verificacion = true;
    int contador=0,i;
    token = (char *) malloc(strlen(string)*sizeof(char));
    strcpy(token, string);
    token = strtok(token,".");

    while(token!=NULL){
        contador++;
        i=0;
        while(*(token+i)!='\0'){
            if(!isdigit(*(token+i))) verificacion = false;
            i++;
        }
        if(atoi(token)<0||atoi(token)>255) verificacion = false;
        token=strtok(NULL,".");
    }
    if(contador!=4) verificacion = false;
    free(token);

    return verificacion;
}


/*
Función: dirección_puerto

Verifica si una cadena de caracteres representa un número de puerto válido. 
Verifica si la cadena solo contiene dígitos numéricos y si el número entero 
resultante está en el rango válido de 0 a 65535. La función devuelve un valor 
booleano que indica si el número de puerto es válido o no.
 */

bool direccion_puerto(char *string){
    bool verificacion = true;
    int i=0;
    while(*(string+i)!='\0'){
        if(!isdigit(*(string+i))) verificacion = false;
        i++;
    }
    if(atoi(string)<0||atoi(string)>65535) verificacion = false;
    return verificacion;
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    int sd;
    struct sockaddr_in addr;

    // Chequeo de argumentos
        if(argc!=3){
        errx(1, "Error in arguments number");
    }
    if(!direccion_IP(argv[1]))
        errx(1, "Invalidad IP");
    if(!direccion_puerto(argv[2]))
        errx(1, "Invalidad Port");


    // Crea el socket y verifica si hay errores
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0)
        err(1, "socket failed");
    
    // Setea los datos del socket  
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_addr.s_addr = inet_addr(argv[1]);  

    // Conecta y verifica si hay errores
    if (connect(sd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        err(1, "connect failed");
    }


    // Si recibe "hello" procede con "autenticate" y "operate" si no hay errores
    if (!recv_msg(sd, 220, NULL))
        errx(1, "unexpected response from server");
    else {
        authenticate(sd);
        operate(sd);
    }

    // Cierra el socket 
    close(sd);

    return 0;
}