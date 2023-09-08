#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>
#include "time_functions_shared.h"

#define PORT "9000"
#define BACKLOG 20
#define SOCKET_DATA "/var/tmp/aesdsocketdata"
#define BUF_LEN 1024

bool caught_signal = false;
//bool caught_sigterm = false;
//int server_fd, new_socket;



struct thread_struct 
{
    int accept_fd;
	int thread_complete;
};
struct timer_thread_data
{    
    
    pthread_mutex_t lock;
};

typedef struct slist_data_s slist_data_t;

struct slist_data_s 
{
    pthread_t thread_id;
	struct thread_struct th_data;
	SLIST_ENTRY(slist_data_s) entries;
};
SLIST_HEAD(slisthead,slist_data_s) head = SLIST_HEAD_INITIALIZER(head);
int writer_fd, sock_fd, new_sock_fd;

pthread_mutex_t lock;
/* timer_thread runs every 10 seconds 
* Assumes timer_create has configured for sigval.sival_ptr to point to the
* thread data used for the timer
*
* Thread appends timestamp to SOCKDATA file
*/
static void timer_thread ( union sigval sigval )
{
    struct timer_thread_data *td = (struct timer_thread_data*) sigval.sival_ptr;
    
    if ( pthread_mutex_lock(&td->lock) != 0 ) {
        printf("Error %d (%s) locking thread data!\n",errno,strerror(errno));
    } 
    else 
    {
        time_t curr_time;
        struct tm *curr_localtime;
        char timestamp[128];
        int fd;

        time(&curr_time);
        curr_localtime = localtime(&curr_time);
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z\n", curr_localtime);

        fd = open(SOCKET_DATA, O_APPEND | O_WRONLY);
        if(fd == -1)
        {
            perror("open failure");
            return;
        }
        lseek(fd, 0, SEEK_END);
        if(write(fd, timestamp, strlen(timestamp))== -1)
        {
            perror("write");
            return;
        }

        close(fd);

        if ( pthread_mutex_unlock(&td->lock) != 0 ) {
            printf("Error %d (%s) unlocking thread data!\n",errno,strerror(errno));
        }
    }
}

/**
* Setup the timer at @param timerid (previously created with timer_create)  
* using @param clock_id as the clock reference.
* The time now is saved in @param start_time
* @return true if the timer could be setup successfuly, false otherwise
*/
static bool setup_timer( int clock_id,
                         timer_t timerid,
                         struct timespec *start_time)
{
    bool success = false;
    if ( clock_gettime(clock_id,start_time) != 0 ) 
    {
        syslog(LOG_ERR, "Error %d (%s) getting clock %d time\n",errno,strerror(errno),clock_id);
    } 
    else 
    {
        struct itimerspec itimerspec;
        memset(&itimerspec, 0, sizeof(struct itimerspec));
        itimerspec.it_interval.tv_sec = 10;
        timespec_add(&itimerspec.it_value,start_time,&itimerspec.it_interval);

        if( timer_settime(timerid, TIMER_ABSTIME, &itimerspec, NULL ) != 0 ) 
        {
            syslog(LOG_ERR, "Error %d (%s) setting timer\n",errno,strerror(errno));
        } 
        else 
        {
            success = true;
        }
    }
    return success;
}

void receive_sock(int socket_fd)
{
    int recv_rc;
    char buf[BUF_LEN];
    bool end_flag = false;
    int writer_fd;

    while(!end_flag)
    {
        recv_rc = recv(socket_fd, buf, BUF_LEN, 0);
        if(recv_rc == -1)
        {
            perror("recv failure");
            syslog(LOG_ERR, "recv failure");
            return;
        }

        if(pthread_mutex_lock(&lock) != 0){
        perror("mutex lock fail");}

        writer_fd = open(SOCKET_DATA, O_APPEND | O_WRONLY);
        if(writer_fd == -1)
        {
            perror("open failure");
            return;
        }

        if(write(writer_fd, buf, recv_rc)== -1)
        {
            perror("write");
            syslog(LOG_ERR, "write failure");
            return;
        }

        close(writer_fd);

        if(pthread_mutex_unlock(&lock) != 0){
        perror("mutex lock fail");}


        if(strchr(buf, '\n') != NULL)
        {
            end_flag = true;
        }
    }

}

void send_sock(int local_accept_rc)
{
    FILE *file;
    int next_char;
    char c;

    if(pthread_mutex_lock(&lock) != 0){
        perror("mutex lock fail");}

    file = fopen(SOCKET_DATA, "rb"); //open file for appending packets
    if(file == NULL)
    {
        perror("fopen failure");
        return;
    }

    while(1) //individually print characters of all previous packets
    {
        next_char = fgetc(file);
        if(next_char == EOF)
        {
            break;
        }
        c = next_char;
        if(send(local_accept_rc, &c, 1, 0) == -1)
        {
            perror("send failure");
            syslog(LOG_ERR, "recv failure");
            return;
        }
    }
    fclose(file);

    if(pthread_mutex_unlock(&lock) != 0){
        perror("mutex lock fail");}

    return;
}

/*
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

*/
static void signal_handler(int sig_num)
{
    if(sig_num == SIGINT)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
    }
    else if(sig_num == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
    }
    caught_signal = true;
    
}

void *socket_thread(void *input_args)
{
	struct thread_struct *in_args = input_args;

	receive_sock(in_args->accept_fd);//receive data on socket
	send_sock(in_args->accept_fd); //send all received data
	in_args->thread_complete = 1; //set complete flag
	return input_args;
}

int process_entry(int accept_fd)
{
	slist_data_t *entry=NULL;
	slist_data_t *current_entry;

	entry  = malloc(sizeof(slist_data_t));

	entry->th_data.accept_fd = accept_fd;
	entry->th_data.thread_complete = 0;

	pthread_create(&entry->thread_id,NULL,socket_thread,(void *)&entry->th_data);

	if(SLIST_EMPTY(&head) != 0)
    {
		SLIST_INSERT_HEAD(&head, entry, entries);
	}
	else
    {
		SLIST_FOREACH(current_entry, &head, entries){
			if(current_entry->entries.sle_next == NULL){
				SLIST_INSERT_AFTER(current_entry, entry, entries);
				break;
			}
		}
	}
	return 0;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
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
    FILE *fd;
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
        
        
        fd = fopen("/var/tmp/aesdsocketdata","a");
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
    fclose(fd);
    
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
*/
int main(int argc, char *argv[])
{

    
    int opt = 1;
    struct addrinfo hints; //hints for bind()
    struct addrinfo *servinfo; //for point results of bind()
    struct sockaddr_storage client_addr;

    socklen_t addr_size;
    char client_address[INET6_ADDRSTRLEN];
	slist_data_t *entry;

    struct timer_thread_data ttd;
    struct sigevent sev;
    timer_t timerid;

	openlog(NULL,0,LOG_USER);
	syslog(LOG_DEBUG,"starting aesdsocket");	
	writer_fd = creat(SOCKET_DATA, 0777);
    close(writer_fd);

	//Initialize SLIST Head
	SLIST_INIT(&head);

	//Setup signal handler
    struct sigaction new_action; //set up signal and handlers
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler=signal_handler;

    if(sigaction(SIGINT, &new_action, NULL) != 0)
    {
        perror("sigaction failure");
        exit(1);
    }
    if(sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        perror("sigaction failure");
        exit(1);
    }

    //clear and set hints struct
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    //populate servinfo with hints set above
    if(getaddrinfo(NULL, PORT, &hints, &servinfo) != 0)
    {
        perror("getaddinfo error");
        syslog(LOG_ERR, "getaddinfo error");
        return -1;
    }

    //create socket
    sock_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sock_fd == -1)
    {
        perror("socket creation fail");
        syslog(LOG_ERR, "socket creation fail");
        return -1;
    }

    if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1)
    {
        perror("setsockopt failure");
        syslog(LOG_ERR, "setsockopt failure");
        return -1;
    }

    //bind socket
    if(bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen))
    {
        close(sock_fd);
        perror("bind failure");
        syslog(LOG_ERR, "bind failure");
        return -1;
    }

    freeaddrinfo(servinfo);

    //listen
    if(listen(sock_fd, BACKLOG) < 0)
    {
        perror("listen failure");
        syslog(LOG_ERR, "listen failure");
        return -1;
    }

    if(argc > 1 && strcmp(argv[1], "-d")==0)
    {
        if(daemon(0, 0)==-1)
        {
            perror("daemon failure");
            syslog(LOG_ERR, "daemon failure");
            exit(1);
        }
    }

    void * p_ttd = memset(&ttd,0,sizeof(struct timer_thread_data));
    if ( pthread_mutex_init(&ttd.lock,NULL) != 0 ) {
        printf("Error %d (%s) initializing thread mutex!\n",errno,strerror(errno));
    } 
    else 
    {
        int clock_id = CLOCK_MONOTONIC;
        void* p_sev = memset(&sev,0,sizeof(struct sigevent));
        /**
        * Setup a call to timer_thread passing in the ttd structure as the sigev_value
        * argument
        */
        sev.sigev_notify = SIGEV_THREAD;
        sev.sigev_value.sival_ptr = &ttd;
        sev.sigev_notify_function = timer_thread;
        if ( timer_create(clock_id,&sev,&timerid) != 0 ) {
            printf("Error %d (%s) creating timer!\n",errno,strerror(errno));
        } 
        else 
        {
            struct timespec start_time;

            if ( setup_timer(clock_id, timerid, &start_time) ) 
            {
                while(1)
                {
                    //check to see if signal occured
                    if(caught_signal == true)
                    {
                        timer_delete(timerid);
                        SLIST_FOREACH(entry, &head, entries)
                        {
                            if(entry->th_data.thread_complete == 1)
                            {
                                pthread_join(entry->thread_id,NULL);
                            }
                            //close connection
                            close(entry->th_data.accept_fd);
                        }
                        //free SLIST
                        while (!SLIST_EMPTY(&head))
                        {
                            entry = SLIST_FIRST(&head);
                            close(entry->th_data.accept_fd);
                            SLIST_REMOVE_HEAD(&head, entries);
                            free(entry);
                        }

                        close(sock_fd);
                        unlink(SOCKET_DATA);
                        free(p_sev);
                        free(p_ttd);
                        closelog();
                        return 0;
                    }

                    new_sock_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &addr_size);
                    if(new_sock_fd == -1)
                    {
                        perror("accept failure");
                        syslog(LOG_ERR, "accept failure");
                        return -1;
                    }
                    else //print client ip to syslog per step 2 part d
                    {
                        //get client address and store in string client_address
                        inet_ntop(client_addr.ss_family,
                        get_in_addr((struct sockaddr *)&client_addr),
                        client_address, sizeof client_address);
                    }

                    process_entry(new_sock_fd);
                    //join threads that are completed
                    SLIST_FOREACH(entry, &head, entries)
                    {
                        if(entry->th_data.thread_complete == 1)
                        {
                            syslog(LOG_DEBUG,"SLIST: Attempting to join thread pointed to by %i",entry->th_data.accept_fd);
                            pthread_join(entry->thread_id,NULL);
                            close(entry->th_data.accept_fd);	
                        }
                        entry->th_data.thread_complete = 0;
                    }
                }
            }
            else
            {
                syslog(LOG_ERR, "Failed to setup timer");
                close(sock_fd);
                        unlink(SOCKET_DATA);
                        free(p_sev);
                        free(p_ttd);
                        closelog();
                        
                exit(1);
            }

            if (timer_delete(timerid) != 0) 
            {
                printf("Error %d (%s) deleting timer!\n",errno,strerror(errno));
            }
        }
    }
    
    syslog(LOG_DEBUG, "ending aesdsocket");
	return 0;
}
