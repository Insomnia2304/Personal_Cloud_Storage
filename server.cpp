#include "common.h"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/wait.h>
using namespace std;
namespace fs = std::filesystem;
using namespace fs;

void uploadCommand(char* command, int clientFd);
void downloadCommand(char* command, int clientFd);
void loginCommand(char* command, int clientFd);
void logoutCommand(char* command, int clientFd);
void registerCommand(char* command, int clientFd);
void exitCommand(char* command, int clientFd);
void listCommand(char* command, int clientFd);
void deleteCommand(char* command, int clientFd);
void renameCommand(char* command, int clientFd);
void unknownCommand(char* command, int clientFd);
void backupCloudData(const path& original, const path& backup);

char whichUserConnected[COMMAND_MAX];
char currentLocation[COMMAND_MAX];
bool clientIsConnected(int clientFd, struct sockaddr* server, socklen_t clientSize);

sqlite3 *db;
char *errMsg = nullptr;

int main() {
    strcpy(whichUserConnected, "guest");
    strcpy(currentLocation, "cloud");

    int rc = sqlite3_open("utils/server.db", &db);

    if (rc != SQLITE_OK) {
        printf("couldn't open server database: %s\n", sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }
    printf("database opened successfully\n");

    const char *loginTable = "create table if not exists LOGIN(USERNAME varchar(2) primary key not null, PASSWORD varchar(2) not null)";
    rc = sqlite3_exec(db, loginTable, 0, 0, &errMsg);

    if (rc != SQLITE_OK) {
        printf("couldn't open login table: %s\n", errMsg);
        exit(EXIT_FAILURE);
    }
    else {
        printf("login table opened successfully\n");
    }

    struct sockaddr_in server;
    struct sockaddr_in client;

    int socketFd;
    if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    bzero(&server, sizeof(server));
    bzero(&client, sizeof(client));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(socketFd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1) {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(socketFd, 100) == -1) {
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    while (1) {
        printf("waiting at port: %d...\n", PORT);
        fflush(stdout);

        int clientFd;
        socklen_t clientSize = sizeof(client);
        if ((clientFd = accept(socketFd, (struct sockaddr*)&client, &clientSize)) == -1) {
            perror("accept client error");
            continue;
        }
        
        int childPID;
        switch (childPID = fork()) {
            case -1: 
                close(clientFd); perror("fork error"); continue;
            case 0: 
                while (clientIsConnected(clientFd, (struct sockaddr*)&server, clientSize)) {
                    printf("waiting for a command...\n");
                    fflush(stdout);

                    char command[COMMAND_MAX];
                    int bytesRead;

                    if ((bytesRead = read(clientFd, command, COMMAND_MAX - 1)) <= 0)
                    {
                        perror("read from client error / empty buffer");
                        close(clientFd);
                        continue;
                    }
                    command[bytesRead] = 0;
                    printf("[Debug] bytes read: %d, received command: %s\n", bytesRead, command);
                    fflush(stdout);

                    char *whichCommand = strtok(command, " \n");

                    if (strcmp(whichCommand, "upload") == 0) uploadCommand(command, clientFd);
                    else if (strcmp(whichCommand, "download") == 0) downloadCommand(command, clientFd);
                    else if (strcmp(whichCommand, "login") == 0) loginCommand(command, clientFd);
                    else if (strcmp(whichCommand, "logout") == 0) logoutCommand(command, clientFd);
                    else if (strcmp(whichCommand, "register") == 0) registerCommand(command, clientFd);
                    else if (strcmp(whichCommand, "exit") == 0) exitCommand(command, clientFd);
                    else if (strcmp(whichCommand, "list") == 0 || strcmp(whichCommand, "ls") == 0) listCommand(command, clientFd);
                    else if (strcmp(whichCommand, "delete") == 0) deleteCommand(command, clientFd);
                    else if (strcmp(whichCommand, "rename") == 0) renameCommand(command, clientFd);
                    else unknownCommand(command, clientFd);
                    printf("------------------------\n");

                    backupCloudData("cloudData", "cloudDataBackup");
                }

                close(clientFd);
                exit(EXIT_SUCCESS);
            default:
                close(clientFd);
                while (waitpid(-1, NULL, WNOHANG)); //WNOHANG: does not wait for children, used for getting the exit status
                continue;
        }
    }

    close(socketFd);
    return 0;
}

bool clientIsConnected(int clientFd, struct sockaddr* server, socklen_t clientSize) {
    if (getpeername(clientFd, server, &clientSize) == -1) {
        printf("connection terminated\n");
        printf("------------------------\n");
        return 0;
    }
    return 1;
}

void uploadCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") == 0) {
        char errorMessage[] = "to use this command you must login first";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char* filename = strtok(NULL, " ");
    if (filename == nullptr) {
        char errorMessage[] = "{too few arguments error} syntax: upload [filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: upload [filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }
    
    // first, check if directory is existent
    char fileLocation[] = "cloudData/";
    strcat(fileLocation, whichUserConnected);
    
    if (exists(fileLocation) == false)
        if (create_directory(fileLocation) == false) {
            perror("couldn't create user directory");
                return;
        printf("[Debug] user directory created successfully: %s\n", fileLocation);
        }

    strcat(fileLocation, "/");
    strcat(fileLocation, basename(filename));

    if (exists(fileLocation)) {
        char errorMessage[COMMAND_MAX];
        sprintf(errorMessage, "'%s' already exists in your cloud\nrename the existent file with the <rename> command\ndelete the existent with the <delete> command", basename(filename));
            if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0) 
                perror("write to client error / empty buffer");
        return;
    }

    if (write(clientFd, "upload ready", strlen("upload ready")) == -1) {
        perror("upload ready announce error");
        return;
    }

    int bytesRead;
    char clientAnswer[COMMAND_MAX];
    if ((bytesRead = read(clientFd, clientAnswer, COMMAND_MAX - 1)) == -1) {
        perror("client ready status read error");
        return;
    }
    clientAnswer[bytesRead] = 0;

    if (strcmp(clientAnswer, "upload ready") != 0) {
        if (write(clientFd, clientAnswer, strlen(clientAnswer)) == -1)
            perror("write status to client error");
        return;
    }

    // needed so the size wouldn't transmit next to upload announcer
    if (write(clientFd, "send size", strlen("send size")) == -1) {
        perror("size ask announce error");
        return;
    }

    // both parties are ready
    FILE *cloudFile = fopen(fileLocation, "w");
    char sectionOfFile[SECTION_MAX + 1]; //+1 for null terminator

    struct stat fileinfo;
    if (read(clientFd, &fileinfo.st_size, sizeof(fileinfo.st_size)) == -1) {
        perror("file size read error");
        return;
    }

    
    char key[COMMAND_MAX];
    strcpy(key, secret_key);
    int keyIndex = 0;

    int totalBytesRead = 0, i = 1;

    while (totalBytesRead < fileinfo.st_size) {
        while ((bytesRead = read(clientFd, sectionOfFile, SECTION_MAX)) <= 0);
        sectionOfFile[bytesRead] = 0;

        //debug
        printf("<Section %d [%d bytes]: ", i++, bytesRead);
        for (int i = 0; i <= 20; i++) 
             if (sectionOfFile[i])
                 printf("%c", sectionOfFile[i]);
             else printf("?");
        printf(">\n");

        for (int i = 0; i < bytesRead; i++, keyIndex++) {
            sectionOfFile[i] = sectionOfFile[i] + (key[keyIndex % strlen(key)] - '0');
        }

        fwrite(sectionOfFile, 1, bytesRead, cloudFile);

        totalBytesRead += bytesRead;
    }
    fflush(NULL);

    char successfulUpload[] = "successful upload in cloud";
    if (write(clientFd, successfulUpload, strlen(successfulUpload)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", successfulUpload);
}

void downloadCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") == 0) {
        char errorMessage[] = "to use this command you must login first";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char* filename = strtok(NULL, " ");
    if (filename == nullptr) {
        char errorMessage[] = "{too few arguments error} syntax: download [filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: download [filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    // first, check if directory is existent
    char fileLocation[COMMAND_MAX] = "cloudData/";
    strcat(fileLocation, whichUserConnected);

    if (exists(fileLocation) == false) {
        if (create_directory(fileLocation) == false) {
            perror("couldn't create user directory");
                return;
        }
        printf("[Debug] user directory created successfully: %s", fileLocation);
    }

    strcat(fileLocation, "/");
    strcat(fileLocation, filename);

    if (is_directory(fileLocation)) {
        char errorMessage[]= "{access error} specified path is a directory";
        if (write(clientFd, errorMessage, strlen(errorMessage)) == -1)
            perror("write error");
        return;
    }

    if (access(fileLocation, R_OK) == -1) {
        perror("access error");
        char errorMessage[]= "{access error} file doesn't exists / cannot read from it";
        if (write(clientFd, errorMessage, strlen(errorMessage)) == -1)
            perror("write access error");
        return;
    }

    if (write(clientFd, "download ready", strlen("download ready")) == -1) {
        perror("download ready announce error");
        return;
    }

    int bytesRead;
    char clientAnswer[COMMAND_MAX];
    if ((bytesRead = read(clientFd, clientAnswer, COMMAND_MAX - 1)) == -1) {
        perror("client ready status read error");
        return;
    }
    clientAnswer[bytesRead] = 0;

    if (strcmp(clientAnswer, "download ready") != 0) {
        if (write(clientFd, clientAnswer, strlen(clientAnswer)) == -1)
            perror("write status to client error");
        return;
    }

    struct stat fileinfo;
    if (stat(fileLocation, &fileinfo) == -1) {
        perror("file stat error");
        return;
    }

    if (write(clientFd, &fileinfo.st_size, sizeof(fileinfo.st_size)) == -1) {
        perror("file size write error");
        return;
    }

    FILE *cloudFile = fopen(fileLocation, "r");
    char sectionOfFile[SECTION_MAX + 1]; //+1 for null terminator

    printf("[Debug] File size: %ld\n", fileinfo.st_size);
    int totalBytesRead = 0, i = 1;

    char key[COMMAND_MAX];
    strcpy(key, secret_key);
    int keyIndex = 0;

    while (totalBytesRead < fileinfo.st_size) {
        while ((bytesRead = fread(sectionOfFile, 1, SECTION_MAX, cloudFile)) <= 0);
        sectionOfFile[bytesRead] = 0;

        //debug
        printf("<Section %d [%d bytes]: ", i++, bytesRead);
        for (int i = 0; i <= 20; i++) 
             if (sectionOfFile[i])
                 printf("%c", sectionOfFile[i]);
             else printf("?");
        printf(">\n");

        for (int i = 0; i < bytesRead; i++, keyIndex++) {
            sectionOfFile[i] = sectionOfFile[i] - (key[keyIndex % strlen(key)] - '0'); 
           }

        while (write(clientFd, sectionOfFile, bytesRead) <= 0);

        totalBytesRead += bytesRead;
    }
    printf("[Debug] File size: %ld\n", fileinfo.st_size);
    fflush(NULL);

    //needed so the successful download wouldn't transmit as file content
    char donwloadDone[COMMAND_MAX];
    if ((bytesRead = read(clientFd, donwloadDone, COMMAND_MAX - 1)) == -1) {
        perror("read download done error");
        return;
    }
    donwloadDone[bytesRead] = 0;

    char successfulDownload[] = "successful download from cloud";
    if (write(clientFd, successfulDownload, strlen(successfulDownload)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", successfulDownload);
}

void loginCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") != 0) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "already connected as '%s'\nlogout first", whichUserConnected);
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return; 
    }

    char* username = strtok(NULL, " ");
    if (username == nullptr) {
        char errorMessage[] = "{too few arguments error} syntax: login [username]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: login [username]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char sql[COMMAND_MAX * 2];
    sprintf(sql, "select * from login where USERNAME = '%s'", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        printf("couldn't prepare sql statement: %s\n", sqlite3_errmsg(db));
    }
    
    rc = sqlite3_step(stmt);
    //printf("[Debug] rc: %d\n", rc);


    if (rc == SQLITE_DONE) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "username '%s' doesn't exist\nregister first", username);
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }
    sqlite3_finalize(stmt);

    char temp[COMMAND_MAX * 2];
    sprintf(temp, "enter the password for '%s'", username);
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }

    int bytesRead;
    char password[COMMAND_MAX];
    if ((bytesRead = read(clientFd, password, COMMAND_MAX - 1)) == -1) {
        perror("read password error");
        return;
    }
    password[bytesRead] = 0;
    
    sprintf(sql, "select * from login where PASSWORD = '%s'", password);
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        printf("couldn't prepare sql statement: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    rc = sqlite3_step(stmt);
    //printf("[Debug] rc: %d\n", rc);
    
    if (rc == SQLITE_DONE) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "incorrect password for '%s'\n", username);
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }
    sqlite3_finalize(stmt);

    strcpy(whichUserConnected, username);
    sprintf(temp, "logged in successfully as '%s'", username);
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }
}

void logoutCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") == 0) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "already using app as guest");
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return; 
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: logout";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    strcpy(whichUserConnected, "guest");
    char temp[COMMAND_MAX];
    sprintf(temp, "logged out successfully");
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }
}

void registerCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") != 0) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "already connected as '%s'\nlogout first", whichUserConnected);
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return; 
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: register";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }
    
    char temp[COMMAND_MAX * 2];
    sprintf(temp, "enter the username for your account\nit must be alfanunmeric and unique");
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }

    int bytesRead;
    char username[COMMAND_MAX];
    if ((bytesRead = read(clientFd, username, COMMAND_MAX - 1)) == -1) {
        perror("read username error");
        return;
    }
    username[bytesRead] = 0;

    const char* alfanumeric = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int i;
    for (i = 0; username[i]; i++)
        if (strchr(alfanumeric, username[i]) == nullptr)
            break;

    if (!alfanumeric[i]) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "username is not alfanumeric");
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0) 
            perror("write to client error / empty buffer");
        return;
    }


    char sql[COMMAND_MAX * 2 * 2];
    sprintf(sql, "select * from login where USERNAME = '%s'", username);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        printf("couldn't prepare sql statement: %s\n", sqlite3_errmsg(db));
        return;
    }
    
    rc = sqlite3_step(stmt);
    //printf("[Debug] rc: %d\n", rc);
    
    if (rc != SQLITE_DONE) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "username '%s' already exists", username);
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }
    sqlite3_finalize(stmt);

    // username is indeed unique; ask for password
    sprintf(temp, "enter the password for your account\nit must have at least 5 characters, from which at least one numeric");
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }

    char password[COMMAND_MAX];
    if ((bytesRead = read(clientFd, password, COMMAND_MAX - 1)) == -1) {
        perror("read password error");
        return;
    }
    password[bytesRead] = 0;

    if (strlen(password) < 5) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "password doesn't contain at least 5 characters");
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0) 
            perror("write to client error / empty buffer");
        return;
    }

    const char* numeric = "0123456789";
    for (i = 0; numeric[i]; i++)
        if (strchr(password, numeric[i]) != nullptr)
            break;

    if (!numeric[i]) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "password doesn't contain a numeric character");
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0) 
            perror("write to client error / empty buffer");
        return;
    }
    
    sprintf(temp, "confirm your password");
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }

    char passwordConfirmation[COMMAND_MAX];
    if ((bytesRead = read(clientFd, passwordConfirmation, COMMAND_MAX - 1)) == -1) {
        perror("read password confirmation error");
        return;
    }
    passwordConfirmation[bytesRead] = 0;

    if (strcmp(password, passwordConfirmation) != 0) {
        char errorMessage[COMMAND_MAX * 2];
        sprintf(errorMessage, "password doesn't match");
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0) 
            perror("write to client error / empty buffer");
        return;
    }

    sprintf(sql, "insert into LOGIN (USERNAME, PASSWORD) values('%s', '%s');", username, password);
    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);

    if (rc != SQLITE_OK) {
        printf("couldn't insert registered account: %s\n", errMsg);
        return;
    }
    
    strcpy(whichUserConnected, username);
    sprintf(temp, "registered and logged in successfully as '%s'", username);
    if (write(clientFd, temp, strlen(temp)) <= 0) {
        perror("write to client error / empty buffer");
        return;
    }
}

void exitCommand(char* command, int clientFd) {
    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: exit";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char msg[COMMAND_MAX];
    sprintf(msg, "exited with no errors");

    if (write(clientFd, msg, strlen(msg)) <= 0) {
        perror("write to client error");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", msg);
}

void listCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") == 0) {
        char errorMessage[] = "to use this command you must login first";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: list";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char fileLocation[COMMAND_MAX] = "cloudData/";
    strcat(fileLocation, whichUserConnected);

    // first, check if directory is existent
    if (exists(fileLocation) == false)
        if (create_directory(fileLocation) == false) {
            perror("couldn't create user directory");
                return;
        }
    printf("[Debug] user directory created successfully: %s", fileLocation);

    char serverAnswer[COMMAND_MAX] ;
    sprintf(serverAnswer, "total of %d file(s) found:\n\n", static_cast<int>(distance(directory_iterator(fileLocation), directory_iterator{})));

    try {
        for (const auto &entry : directory_iterator(fileLocation)) {
            strcat(serverAnswer,  entry.path().filename().c_str());
            strcat(serverAnswer, "   ");
        }   
    } catch (filesystem_error &err) {
        cout << "error while iterating through directory: " << err.what() << '\n';
        return;
    }
    
    if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) {
        perror("write to client error");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", serverAnswer);
}

void deleteCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") == 0) {
        char errorMessage[] = "to use this command you must login first";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char* filename = strtok(NULL, " ");
    if (filename == nullptr) {
        char errorMessage[] = "{too few arguments error} syntax: delete [filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: delete [filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char fileLocation[COMMAND_MAX] = "cloudData/";
    strcat(fileLocation, whichUserConnected);

    // first, check if directory is existent
    if (exists(fileLocation) == false)
        if (create_directory(fileLocation) == false) {
            perror("couldn't create user directory");
                return;
        }
    printf("[Debug] user directory created successfully: %s", fileLocation);

    strcat(fileLocation, "/");
    strcat(fileLocation, filename);

    char serverAnswer[COMMAND_MAX];
    if (exists(fileLocation) == false) {
        sprintf(serverAnswer, "file '%s' is inexistent", filename);
        if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) 
            perror("write to client error");
        return;
    }

    sprintf(serverAnswer, "do you REALLY want to delete '%s' from the cloud?\nthis action can NOT be undone: yes/no", filename);

    if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) {
        perror("write to client error");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", serverAnswer);

    int bytesRead;
    char userAnswer[COMMAND_MAX];
    if ((bytesRead = read(clientFd, userAnswer, COMMAND_MAX - 1)) == -1) {
        perror("read user answer error");
        return;
    }
    userAnswer[bytesRead] = 0;

    if (strcasecmp(userAnswer, "yes") != 0) {
        sprintf(serverAnswer, "file '%s' was not deleted from the cloud", filename);
        if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) 
            perror("write to client error");
        return;
    }

    try {
         remove(fileLocation);
        } catch (filesystem_error &err) {
        cout << "error while trying to delete file: " << err.what() << '\n';
        return;
    }
    
    sprintf(serverAnswer, "file '%s' was deleted successfully", filename);
    if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) {
        perror("write to client error");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", serverAnswer);
}

void renameCommand(char* command, int clientFd) {
    if (strcmp(whichUserConnected, "guest") == 0) {
        char errorMessage[] = "to use this command you must login first";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char* filename = strtok(NULL, " ");
    if (filename == nullptr) {
        char errorMessage[] = "{too few arguments error} syntax: rename [old filename] [new filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char* newFilename = strtok(NULL, " ");
    if (newFilename == nullptr) {
        char errorMessage[] = "{too few arguments error} syntax: rename [old filename] [new filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char *tooManyArgs = strtok(NULL, " ");
    if (tooManyArgs != nullptr) {
        char errorMessage[] = "{too many arguments error} syntax: rename [old filename] [new filename]";
        if (write(clientFd, errorMessage, strlen(errorMessage)) <= 0)
            perror("write to client error / empty buffer");
        return;
    }

    char fileLocation[COMMAND_MAX] = "cloudData/";
    char newFileLocation[COMMAND_MAX];
    strcat(fileLocation, whichUserConnected);

    // first, check if directory is existent
    if (exists(fileLocation) == false)
        if (create_directory(fileLocation) == false) {
            perror("couldn't create user directory");
                return;
        }
    printf("[Debug] user directory created successfully: %s", fileLocation);

    strcat(fileLocation, "/");
    strcpy(newFileLocation, fileLocation);

    strcat(fileLocation, filename);
    strcat(newFileLocation, newFilename);

    char serverAnswer[COMMAND_MAX];
    if (exists(fileLocation) == false) {
        sprintf(serverAnswer, "file '%s' is inexistent", filename);
        if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) 
            perror("write to client error");
        return;
    }

    if (exists(newFileLocation) == true) {
        sprintf(serverAnswer, "new file '%s' is already existent", newFilename);
        if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) 
            perror("write to client error");
        return;
    }

    try {
         rename(fileLocation, newFileLocation);
        } catch (filesystem_error &err) {
        cout << "error while trying to rename file: " << err.what() << '\n';
        return;
    }
    
    sprintf(serverAnswer, "file '%s' was renamed successfully to '%s'", filename, newFilename);
    if (write(clientFd, serverAnswer, strlen(serverAnswer)) <= 0) {
        perror("write to client error");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", serverAnswer);
}

void unknownCommand(char* command, int clientFd) {
    char unknownCommandMessage[COMMAND_MAX];
    sprintf(unknownCommandMessage, "{name error} command \"%s\" is unknown", command);
    //sprintf(unknownCommandMessage, "requested command is not yet implemented: \"%s\"", command);

    if (write(clientFd, unknownCommandMessage, strlen(unknownCommandMessage)) <= 0) {
        perror("write to client error");
        return;
    }
    printf("[Debug] message sent successfully: %s\n", unknownCommandMessage);
}

void backupCloudData(const fs::path& original, const fs::path& backup) {
    try {
        if (exists(original) == false) {
            create_directory(original);
        }
        if (exists(backup) == false) {
            create_directory(backup);
        }

        for (const auto& entry : fs::directory_iterator(original)) {
            const fs::path entry_path = entry.path();
            const fs::path new_entry_path = backup / entry_path.filename();

            // recursively copy directories
            if (is_directory(entry_path)) 
                backupCloudData(entry_path, new_entry_path);

            // copy files
            if (is_regular_file(entry_path)) 
                copy_file(entry_path, new_entry_path, copy_options::overwrite_existing);   
        }
    }
    catch (const exception& err) {
        cout << "cloud data backup error: " << err.what() << std::endl;
    }
}