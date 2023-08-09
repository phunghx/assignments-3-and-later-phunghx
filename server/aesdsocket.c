#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#define PORT 9000
bool caught_sigint = false;
bool caught_sigterm = false;
int server_fd, new_socket;

static void signal_handler ( int signal_number )
{
   
    int errno_saved = errno;
    
    if ( signal_number == SIGINT ) {
        caught_sigint = true;
        syslog(LOG_INFO, "Caught signal, exiting");
    } else if ( signal_number == SIGTERM ) {
        caught_sigterm = true;
        syslog(LOG_INFO, "Caught signal, exiting");
    }
    closelog();
    remove("/var/tmp/aesdsocketdata");
    close(new_socket);
    shutdown(server_fd, SHUT_RDWR);
    errno = errno_saved;
}

int runsocket()  {
    
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char * line = NULL;
    size_t len = 0;
    ssize_t readbyte;
    
    struct sigaction new_action;
    bool success = true;
    memset(&new_action,0,sizeof(struct sigaction));
    new_action.sa_handler=signal_handler;
    if( sigaction(SIGTERM, &new_action, NULL) != 0 ) {
        printf("Error %d (%s) registering for SIGTERM",errno,strerror(errno));
        success = false;
    }
    if( sigaction(SIGINT, &new_action, NULL) ) {
        printf("Error %d (%s) registering for SIGINT",errno,strerror(errno));
        success = false;
    }
    if (success==false)  return -1;
    openlog("socket", LOG_PID|LOG_CONS, LOG_USER);
    
    while (caught_sigterm==false && caught_sigint==false)
    {
        server_fd = socket(AF_INET, SOCK_STREAM,0);
        if (server_fd<0)  {
            return -1;
        }
        if (setsockopt(server_fd, SOL_SOCKET,
                    SO_REUSEADDR | SO_REUSEPORT, &opt,
                    sizeof(opt))) {
            perror("setsockopt");
            return -1;
        }
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);
        
        if (bind(server_fd, (struct sockaddr*)&address,
                sizeof(address))
            < 0) {
            return -1;
            
        }
        
        if (listen(server_fd, 3) < 0) {
            perror("listen");
            return -1;
        }
        if ((new_socket
            = accept(server_fd, (struct sockaddr*)&address,
                    (socklen_t*)&addrlen))
            < 0) {
            perror("accept");
            return -1;
        }
        
        
        
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(address.sin_addr));

        //recieve data
        
        
        FILE *fd = fopen("/var/tmp/aesdsocketdata","a");
        int n;
        while ( (n = read(new_socket, buffer, sizeof(buffer)-1)) > 0)
        {
            buffer[n] = 0;
            
            for(int i=0;i<n;i++)   {
                if (buffer[i] == '\n')    {
                    fclose(fd);
                    fd = fopen("/var/tmp/aesdsocketdata","r");
                    while ((readbyte = getline(&line, &len, fd)) != -1) {
                        send(new_socket, line, strlen(line), 0);
                    }
                    fclose(fd);
                    fd = fopen("/var/tmp/aesdsocketdata","a");
                    send(new_socket, buffer, i+1, 0);
                    break;
                }
            }
            fprintf(fd,"%s",buffer);
        }
        
        fclose(fd);
        
        
        close(new_socket);
        shutdown(server_fd, SHUT_RDWR);
        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(address.sin_addr));
        
    }
    //if( caught_sigint || caught_sigterm) {
    //        printf("\nCaught SIGINT!\n");
    //}
    //if( caught_sigterm ) {
    //        printf("\nCaught SIGTERM!\n");
    //}
    remove("/var/tmp/aesdsocketdata");
    
    closelog();
    return 0;
}
int main(int argc, char* argv[])
{
    pid_t process_id = 0;
    pid_t sid = 0;
    if (argc <=1)  {
        runsocket();
        return 1;
    }
    // Create child process
    process_id = fork();
    if (process_id < 0)
    {
    printf("fork failed!\n");
    // Return failure in exit status
        exit(1);
    }
    if (process_id > 0)
    {
        printf("process_id of child process %d \n", process_id);
        exit(0);
    }
    //set new session
    sid = setsid();
    if(sid < 0)
    {
    // Return failure
        exit(1);
    }
    runsocket();
    return (0);
}