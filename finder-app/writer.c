#include <stdio.h>
#include <syslog.h>

int main(int argc, char* argv[]) {
    openlog ("writer", LOG_PID, LOG_USER);
    if (argc !=3)  {
        syslog (LOG_ERR, "Not enough parameters for program %s", argv[0]);
        closelog();
        return 1;
    }
    FILE *f = fopen(argv[1],"w");
    if (f==NULL)  {
        syslog (LOG_ERR, "Cannot open file %s", argv[1]);
        closelog();
        return 1;
    }
    fprintf(f,"%s\n",argv[2]);
    syslog (LOG_DEBUG, "Writing %s to %s", argv[2],argv[1]);
    fclose(f);
    closelog();
    return 0;
}