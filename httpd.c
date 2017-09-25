/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
/*	#include <pthread.h> */
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((INT)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
#define STDIN   0
#define STDOUT  1
#define STDERR  2



VOID accept_request(VOID *);
VOID bad_request(INT);
VOID cat(INT, FILE *);
VOID cannot_execute(INT);
VOID DEBUG_ERROR(const CHAR *);
VOID execute_cgi(INT, const CHAR *, const CHAR *, const CHAR *);
INT get_line(INT, CHAR *, INT);
VOID headers(INT, const CHAR *);
VOID not_found(INT);
VOID serve_file(INT, const CHAR *);
INT startup(u_short *);
VOID unimplemented(INT);
ULONG main_init(IN INT port, OUT INT *piSockFd);
ULONG bind_dynamic(IN INT iSockFd, OUT UINT *puiPort);


/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
VOID accept_request(VOID *arg)
{
    INT client = *(INT*)arg;
    CHAR buf[1024];
    size_t numCHARs;
    CHAR method[255];
    CHAR url[255];
    CHAR path[512];
    size_t i, j;
    struct stat st;
    INT cgi = 0;      /* becomes true if server decides this is a CGI
                       * program */
    CHAR *query_string = NULL;

    numCHARs = get_line(client, buf, sizeof(buf));
    i = 0; j = 0;
    while (!ISspace(buf[i]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[i];
        i++;
    }
    j=i;
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    while (ISspace(buf[j]) && (j < numCHARs))
        j++;
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < numCHARs))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprINTf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
    if (stat(path, &st) == -1) {
        while ((numCHARs > 0) && strcmp("\n", buf))  /* read & discard headers */
            numCHARs = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) ||
                (st.st_mode & S_IXGRP) ||
                (st.st_mode & S_IXOTH)    )
            cgi = 1;
        if (!cgi)
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }

    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
VOID bad_request(INT client)
{
    CHAR buf[1024];

    sprINTf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprINTf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprINTf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprINTf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprINTf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE poINTer for the file to cat */
/**********************************************************************/
VOID cat(INT client, FILE *resource)
{
    CHAR buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
VOID cannot_execute(INT client)
{
    CHAR buf[1024];

    sprINTf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* PrINT out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
VOID DEBUG_ERROR(const CHAR *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
VOID execute_cgi(INT client, const CHAR *path,
        const CHAR *method, const CHAR *query_string)
{
    CHAR buf[1024];
    INT cgi_output[2];
    INT cgi_input[2];
    pid_t pid;
    INT status;
    INT i;
    CHAR c;
    INT numCHARs = 1;
    INT content_length = -1;

    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        while ((numCHARs > 0) && strcmp("\n", buf))  /* read & discard headers */
            numCHARs = get_line(client, buf, sizeof(buf));
    else if (strcasecmp(method, "POST") == 0) /*POST*/
    {
        numCHARs = get_line(client, buf, sizeof(buf));
        while ((numCHARs > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numCHARs = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }
    else/*HEAD or other*/
    {
    }


    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ( (pid = fork()) < 0 ) {
        cannot_execute(client);
        return;
    }
    sprINTf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    if (pid == 0)  /* child: CGI script */
    {
        CHAR meth_env[255];
        CHAR query_env[255];
        CHAR length_env[255];

        dup2(cgi_output[1], STDOUT);
        dup2(cgi_input[0], STDIN);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprINTf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprINTf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            sprINTf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, NULL);
        exit(0);
    } else {    /* parent */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null CHARacter.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last CHARacter of the
 * string will be a linefeed and the string will be terminated with a
 * null CHARacter.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
INT get_line(INT sock, CHAR *buf, INT size)
{
    INT i = 0;
    CHAR c = '\0';
    INT n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG prINTf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG prINTf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    return(i);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to prINT the headers on
 *             the name of the file */
/**********************************************************************/
VOID headers(INT client, const CHAR *filename)
{
    CHAR buf[1024];
    (VOID)filename;  /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
VOID not_found(INT client)
{
    CHAR buf[1024];

    sprINTf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a poINTer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
VOID serve_file(INT client, const CHAR *filename)
{
    FILE *resource = NULL;
    INT numCHARs = 1;
    CHAR buf[1024];

    buf[0] = 'A'; buf[1] = '\0';
    while ((numCHARs > 0) && strcmp("\n", buf))  /* read & discard headers */
        numCHARs = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: poINTer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
INT startup(u_short *port)
{
    INT httpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        DEBUG_ERROR("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        DEBUG_ERROR("bind");
    if (*port == 0)  /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            DEBUG_ERROR("getsockname");
        *port = ntohs(name.sin_port);
    }
    if (listen(httpd, 5) < 0)
        DEBUG_ERROR("listen");
    return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
VOID unimplemented(INT client)
{
    CHAR buf[1024];

    sprINTf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprINTf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

INT main(VOID)
{
	ULONG	ulRet = ERROR_FAILED;
    INT server_sock = -1;
    u_short port = 4000;
    INT client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t  client_name_len = sizeof(client_name);
    /* pthread_t newthread;*/

	ulRet = main_init((UINT *)&port, server_sock);
	if (ulRet < 0){
		DEBUG_ERROR("main_init failed");
	}
	
    while (1)
    {
        client_sock = accept(server_sock,
                (struct sockaddr *)&client_name,
                &client_name_len);
        if (client_sock == -1)
            DEBUG_ERROR("accept");
        accept_request(client_sock); 
		/*
        if (pthread_create(&newthread , NULL, (VOID *)accept_request, (VOID *)&client_sock) != 0)
            perror("pthread_create");
        */
    }

    close(server_sock);

    return(0);
}


/***************************************************************
* httpd 初始化函数，创建socket，并监听端口。
***************************************************************/
ULONG main_init(INOUT UINT *puiPort, OUT INT *piSockFd)
{
	ULONG ulRet = ERROR_FAILED;
	INT  iSockFd; 
	int  uiPort = *puiPort;
	struct sockaddr_in stChildFd;

	memset(&stChildFd, 0, sizeof(stChildFd));
    stChildFd.sin_family = AF_INET;
    stChildFd.sin_port = htons(*puiPort);
    stChildFd.sin_addr.s_addr = htonl(INADDR_ANY);
	
    iSockFd = socket(PF_INET, SOCK_STREAM, 0);
    if (-1 == iSockFd){
		DEBUG_ERROR("socket");
	}      

	ulRet = bind(iSockFd, (struct sockaddr *)&stChildFd, sizeof(stChildFd));
    if (ulRet < 0){
		DEBUG_ERROR("bind");
	}
        
	
    if (0 == uiPort){
    	ulRet = bind_dynamic(iSockFd, &uiPort);    
    }

	ulRet = listen(iSockFd, 5);
    if (ulRet < 0){
		DEBUG_ERROR("listen");
	}

	printf("httpd listen on port : %d \n\r",uiPort);
	
	*puiPort = uiPort;
   	return ulRet;	
}


/***************************************************************
* 动态绑定端口
***************************************************************/
ULONG bind_dynamic(IN INT iSockFd, OUT UINT *puiPort)
{
	ULONG  ulRet = ERROR_FAILED;
	struct sockaddr_in stChildFd;
	UINT uiSockLen = sizeof(stChildFd);
	
	memset(&stChildFd, 0, sizeof(stChildFd));
    stChildFd.sin_family = AF_INET;
    stChildFd.sin_port = htons(0);
    stChildFd.sin_addr.s_addr = htonl(INADDR_ANY);

	ulRet = getsockname(iSockFd, (struct sockaddr *)&stChildFd, &uiSockLen)
	if (ulRet < 0){
		DEBUG_ERROR("getsockname");
	}

	*puiPort = ntohs(stChildFd.sin_port);

	return ulRet;	
}