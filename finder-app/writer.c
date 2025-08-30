#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
    // Open syslog with LOG_USER facility
    openlog("writer", 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: expected 2, got %d", argc - 1);
        closelog();
        return EXIT_FAILURE;
    }

    const char *writefile = argv[1];
    const char *writestr  = argv[2];

    // Log debug message
    syslog(LOG_DEBUG, "Writing \"%s\" to %s", writestr, writefile);

    // Open file for writing
    FILE *fp = fopen(writefile, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Error opening file %s", writefile);
        closelog();
        return EXIT_FAILURE;
    }

    // Write string to file
    if (fputs(writestr, fp) == EOF) {
        syslog(LOG_ERR, "Error writing to file %s", writefile);
        fclose(fp);
        closelog();
        return EXIT_FAILURE;
    }

    // Close file
    if (fclose(fp) != 0) {
        syslog(LOG_ERR, "Error closing file %s", writefile);
        closelog();
        return EXIT_FAILURE;
    }

    closelog();
    return EXIT_SUCCESS;
}

