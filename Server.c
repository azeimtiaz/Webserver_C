/***
 * This is a simple webserver developed in C.
 * The server renders, various file types including images, gifs, html as well as php files,
 * and supports dynamic rendering.
 * 
 * @author Azma Imtiaz - 19020368
 * 
 * To run:  Compile and run the file, and the program will print the port number on the terminal
 *          Visit http://127.0.0.1:<PORT_NUMBER>/ to view the index.html file in the root folder
 *          http://127.0.0.1:<PORT_NUMBER>/Project1/index.html to view files in Project1 folder within root
 ***/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>
#include <strings.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define ISspace(x) isspace((int)(x))
#define BUF_SIZE 2048
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

typedef struct {
 char *ext;
 char *mediatype;
} extn;

size_t total_bytes_sent = 0;

//Possible media types
extn extensions[] ={
 {"gif", "image/gif" },
 {"txt", "text/plain" },
 {"jpg", "image/jpg" },
 {"jpeg","image/jpeg"},
 {"png", "image/png" },
 {"ico", "image/ico" },
 {"zip", "image/zip" },
 {"gz",  "image/gz"  },
 {"tar", "image/tar" },
 {"htm", "text/html" },
 {"html","text/html" },
 {"php", "text/html" },
 {"pdf","application/pdf"},
 {"zip","application/octet-stream"},
 {"rar","application/octet-stream"},
 {0,0} };

void error(const char *);
int init_connect(unsigned short *);
void accept_req(int);
int read_line(int, char *);
void cannot_implement(int);
void not_found(int);
void send_file(int , const char *);
void exec_cgi(int, const char *, const char *, const char *);
void http_header(int, const char *);
void cat(int, FILE *);
void bad_request(int);
void cannot_exec(int);
int get_file_size(int);
void php_cgi(const char *, int);
void php_headers(int, const char *);

/***
 * Prints error and terminates program
 * **/
void error(const char *msg) {
    //interprets error no and outputs the error description using sterror
    perror(msg); 
    exit(1);
}

/***
 * This function initiates a connection to this webserver through the specified port
 * @param pointer to the variable containing the port number
 * @return the socket
 ***/
int init_connect(unsigned short *port) {
    int sockfd=0;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        error("Error opening socket\n");
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(*port);

    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("bind");
    
    if(*port == 0) {
        int servlen = sizeof(serv_addr);
        if(getsockname(sockfd, (struct sockaddr *)&serv_addr, &servlen) == -1)
            error("getsockname");
        *port = ntohs(serv_addr.sin_port);
    }

    if(listen(sockfd, 5) < 0)
        error("listen");
    
    return(sockfd);
}

/***
 * This function accepts & processes requests from clients
 * @param the client socket
 ***/
void accept_req(int client) {
    
    char buffer[BUF_SIZE];
    int numChars;
    char requestMethod[255];
    char url[255];
    char filePath[512];

    size_t i, j;
    struct stat st;
    int cgi = 0;
    //becomes true(1) if the server decides this is a CGI script/program.

    char *queryString = NULL;

    numChars = read_line(client, buffer);
    i = 0; 
    j = 0;

    while(!ISspace(buffer[j]) && (i < sizeof(requestMethod) - 1))
    {
        requestMethod[i] = buffer[j];
        i++;
        j++;
    }

    requestMethod[i] = '\0';

    if(strcasecmp(requestMethod, "GET") && strcasecmp(requestMethod, "POST"))
    {
        cannot_implement(client); //send unimplemented header
        return;
    }

    if(strcasecmp(requestMethod, "POST") == 0)
    {
        cgi = 1;
    }

    i = 0;
    while(ISspace(buffer[j]) && (i < sizeof(buffer)))
    {
        j++;
    }
    
    while(!ISspace(buffer[j]) && (i < sizeof(url) - 1) && (j < sizeof(buffer)))
    {
        url[i] = buffer[j];
        i++;
        j++;
    }

    url[i] = '\0';

    if(strcasecmp(requestMethod, "GET") == 0)
    {
        queryString = url;
        while((*queryString != '?') && (*queryString != '\0'))
        {
            queryString++;
        }
        if(*queryString == '?')
        {
            cgi = 1;
            *queryString = '\0';
            queryString++;
        }
    }

    sprintf(filePath, "htdocs%s", url);

    if(filePath[strlen(filePath) - 1] == '/')
    {
        strcat(filePath,"index.html");
    }

    if(stat(filePath, &st) == -1)
    {
        while((numChars > 0) && strcmp("\n", buffer)) //read and discard headers
        {
            numChars = read_line(client, buffer);
        }
        not_found(client); //send not found header
    }
    else
    {
        if((st.st_mode & S_IFMT) == S_IFDIR)
        {
            strcat(filePath, "/index.html");
        }

        if((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
        {
            cgi = 1;
        }

        if(!cgi)
        {
            send_file(client, filePath);
        }
        else
        {
            exec_cgi(client, filePath, requestMethod, queryString);
        }
    }
    close(client);
}

/***
 * This function reads a line from the socket until it reaches a newline character
 * If no newline character is found before the 
 * @param the client socket, pointer to the buffer
 * @return number of bytes stored
 ***/
int read_line(int client, char *buffer) {
    int i = 0;
    char c = '\0';
    int n;

    while((i < BUF_SIZE-1) && (c != '\n')) {
        n = recv(client, &c, 1, 0);
        if(n > 0) {
            if(c == '\r') {
                n = recv(client, &c, 1, MSG_PEEK);
                if((n > 0) && (c == '\n'))
                    recv(client, &c, 1, 0);
                else
                    c = '\n';
            }
            buffer[i] = c;
            i++;
        } else 
            c ='\n';
    }
    buffer[i] = '\0';

    return(i);
}

/***
 * Informs the client that the requested method has not been implemented 
 * @param the client socket
 ***/
void cannot_implement(int client) {
    char buffer[BUF_SIZE];

    sprintf(buffer, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, SERVER_STRING);
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "</TITLE></HEAD>\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "</BODY></HTML>\r\n");
    send(client, buffer, strlen(buffer), 0);
}

/***
 * Informs the client that the requested file has not been found (404) 
 * @param the client socket
 ***/
void not_found(int client) {
    char buffer[BUF_SIZE];

    sprintf(buffer, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, SERVER_STRING);
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-Type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<HTML><TITLE>Not found</TITLE>\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<BODY><P>The server could not fulfill\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "your request because the resource specified\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "is unavailable or nonexistent.\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "</BODY></HTML>\r\n");
    send(client, buffer, strlen(buffer), 0);
}

/***
 * This function opens the file to be read
 * @param the client socket, pointer to the filename
 ***/
void send_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buffer[2048];
    

    memset(buffer, 0, 2048);
    read(client, buffer, 2047);
    printf("%s\n", buffer);

    int fd1, length;
    fd1 = open(filename, O_RDONLY);

    if(fd1 == -1) {
        not_found(client);
    } else {
        int len = strlen(filename);
        const char *file_ext = &filename[len-4];
        printf("\n%s\n", file_ext);

        char * s = strchr(filename, '.');
        printf("\n%s\n", s+1);

        for(int i=0; extensions[i].ext != NULL; i++) {
            if(strcmp(s+1, extensions[i].ext) == 0) {
                if(strcmp(extensions[i].ext, "php") == 0) {
                printf("PHP file\n");
                php_cgi(filename, client);
                sleep(1);
                close(fd1);
                } else if(strcmp(extensions[i].ext, "html") == 0) {
                    http_header(client, filename);
                }
            }
        }

        if((length = get_file_size(fd1)) == -1) {
            printf("Error in getting file size.\n");
        }

        ssize_t bytes_sent;

        char buffer[length];

        if((bytes_sent = sendfile(client, fd1, NULL, length)) <= 0) {
            perror("sendfile");
        }
        printf("File %s, Sent %i\n", filename, bytes_sent);
        close(fd1);
    }
}

/***
 * Executes a CGI script 
 * @param the client socket, path to CGI script, pointer to method, pointer to query string
 ***/
void exec_cgi(int client, const char *path, const char *method, const char *queryStr) {
    char buffer[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int cont_len = -1;

    buffer[0] = 'A'; buffer[1] = '\0';

    if(strcasecmp(method, "GET") == 0)      // if GET method
        while ((numchars > 0) && strcmp("\n", buffer))          // read and discard headers
            numchars = read_line(client, buffer);
    else {                                  // if POST method
        numchars = read_line(client, buffer);
        while((numchars > 0) && strcmp("\n", buffer)) {
            buffer[15] = '\0';
            if (strcasecmp(buffer, "Content-Length:") == 0)
                cont_len = atoi(&(buffer[16]));
            numchars = read_line(client, buffer);
        }
        if(cont_len == -1) {
            bad_request(client);
            return;
        }
    }

    sprintf(buffer, "HTTP/1.0 200 OK\r\n");
    send(client, buffer, strlen(buffer), 0);

    if(pipe(cgi_output) < 0) {      // if output pipe is not created
        cannot_exec(client);
        return;
    }
    if(pipe(cgi_input) < 0) {       // if input pipe is not created
        cannot_exec(client);
        return;
    }

    if((pid = fork()) < 0 ) {       // if couldn't create child process
        cannot_exec(client);
        return;
    }

    if(pid == 0) {     // Child CGI script
        char method_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);     // replacing stdout_fd with output_fd[1] (writing data in pipe)
        dup2(cgi_input[0], 0);      // replacing stdin_fd with input_fd[0] (read data in pipe)
        
        close(cgi_output[0]);       // close read_fd in output pipe
        close(cgi_input[1]);        // close write_fd in input pipe
        
        sprintf(method_env, "REQUEST_METHOD=%s", method);       // create environment variable for method
        putenv(method_env);

        if(strcasecmp(method, "GET") == 0) {   // if GET
            sprintf(query_env, "QUERY_STRING=%s", queryStr);        // create environment variable for query
            putenv(query_env);
        } else {                                // if POST
            sprintf(length_env, "CONTENT_LENGTH=%d", cont_len);     // create environment variable for content length
            putenv(length_env);
        }
     
        putenv("REDIRECT_STATUS=true");
        execl("/usr/bin/php-cgi", "php-cgi", NULL);     // execute shell script
        exit(0);
    } else {            // Parent CGI script
        close(cgi_output[1]);
        close(cgi_input[0]);
        if(strcasecmp(method, "POST") == 0)
            for (i = 0; i < cont_len; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while(read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

void http_header(int client, const char *filename) {
    char buf[1024];
    (void)filename;
    strcpy(buf, "HTTP/1.1 200 OK\r\n");
    send(client, buf, strlen(buf),0);
    strcpy(buf, "Content-Type: text/html; charset=UTF-8\r\n\r\n");
    send(client, buf, strlen(buf),0);
    strcpy(buf, "<!DOCTYPE html>\r\n");
    send(client, buf, strlen(buf),0);
}

void cat(int client, FILE *resource) {
    char buffer[1024];

    fgets(buffer, 1024, resource);
    while(!feof(resource)) {
        send(client, buffer, strlen(buffer), 0);
        fgets(buffer, 1024, resource);
    }
}

void bad_request(int client) {
    char buffer[1024];
 
    sprintf(buffer, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buffer, 1024, 0);
    sprintf(buffer, "Content-type: text/html\r\n");
    send(client, buffer, 1024, 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, 1024, 0);
    sprintf(buffer, "<P>Your browser sent a bad request, ");
    send(client, buffer, 1024, 0);
    sprintf(buffer, "such as a POST without a Content-Length.\r\n");
    send(client, buffer, 1024, 0);
}

void cannot_exec(int client) {
    char buffer[1024];

    sprintf(buffer, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "Content-type: text/html\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "\r\n");
    send(client, buffer, strlen(buffer), 0);
    sprintf(buffer, "<P>Error prohibited CGI execution.\r\n");
    send(client, buffer, strlen(buffer), 0);
}

int get_file_size(int fd) {
    struct stat stat_struct;
    if(fstat(fd, &stat_struct) == -1)
        return (1);
    return (int) stat_struct.st_size;
}

void php_cgi(const char *path, int fd) {
    printf("PHP Script\n");
    php_headers(fd, path);
    dup2(fd, STDOUT_FILENO);
    char script[500];
    strcpy(script, "SCRIPT_FILENAME=");
    strcat(script, path);
    putenv("GATEWAY_INTERFACE=CGI/1.1");
    putenv(script);
    putenv("QUERY_STRING=");
    putenv("REQUEST_METHOD=GET");
    putenv("REDIRECT_STATUS=true");
    putenv("SERVER_PROTOCOL=HTTP/1.1");
    putenv("REMOTE_HOST=127.0.0.1");
    execl("/usr/bin/php-cgi", "php-cgi", NULL);
}

void php_headers(int client, const char *filename) {
    char buf[1024];
    (void)filename;
    strcpy(buf, "HTTP/1.1 200 OK\n Server: Web Server in C\n Connection: close\n");
    send(client, buf, strlen(buf),0);
}


int main(void) {

    int serv_sock = -1;
    int cli_sock = -1;
    unsigned short port = 0;
    struct sockaddr_in client_addr;
    socklen_t clilen;

    serv_sock = init_connect(&port);

    printf("Server running on port %d\n", port);
    
    while(1) {
        cli_sock = accept(serv_sock, (struct sockaddr *) &client_addr, &clilen);
        if(cli_sock < 0)
            error("accept");
        accept_req(cli_sock);
    }
    
    close(serv_sock);
    return 0;
}
 