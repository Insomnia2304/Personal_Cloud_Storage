#include "common.h"
#include "TextBox.h"
#include <filesystem>
#include <SFML/Graphics.hpp>
#include <string>
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
using namespace std;
namespace fs = std::filesystem;
using namespace fs;

void uploadCommand(char* command, int socketFd);
void downloadCommand(char* command, int socketFd);
void checkServerAnswer(char* serverAnswer, int socketFd);

char whichUserConnected[COMMAND_MAX];
char currentLocation[COMMAND_MAX];
char cmdReplacement[COMMAND_MAX];
char ansReplacement[COMMAND_MAX];

char commandsHistory[COMMAND_MAX][COMMAND_MAX];
int currentCommand = 0;
int numberOfCommands = 0;

int main() {
    strcpy(whichUserConnected, "guest");
    strcpy(currentLocation, "cloud");
    strcpy(cmdReplacement, "");
    strcpy(ansReplacement, "");

    for (int i = 0; i < COMMAND_MAX; i++)
        commandsHistory[i][0] = 0; 

    uint modeWidth = 1280;
    uint modeHeigth = 720;
    sf::RenderWindow window(sf::VideoMode(modeWidth, modeHeigth), "Personal Cloud Storage");
 
    sf::Texture backgroundImage;
    if (!backgroundImage.loadFromFile("utils/clouds.jpg")) {
       printf("couldn't load background image\n");
    }

    sf::Sprite background(backgroundImage);
    background.setScale((float)modeWidth / backgroundImage.getSize().x, (float)modeHeigth / backgroundImage.getSize().y); //scale to fit window

    sf::Color backgroundColor = background.getColor();
    backgroundColor.a = 200; //opacity
    background.setColor(backgroundColor);

    float offset = 4.f / 100 * modeHeigth;
    sf::Color rectangleColor = sf::Color(93, 182, 193, 150); 

    sf::RectangleShape ansRectangle;
    ansRectangle.setPosition(offset, offset);
    ansRectangle.setSize(sf::Vector2f(modeWidth - 2 * offset, 68.f / 100 * modeHeigth));
    ansRectangle.setFillColor(rectangleColor);
 
    sf::RectangleShape cmdRectangle;
    cmdRectangle.setPosition(offset, offset + ansRectangle.getSize().y + offset);
    cmdRectangle.setSize(sf::Vector2f(modeWidth - 2 * offset, modeHeigth - offset - cmdRectangle.getPosition().y));
    cmdRectangle.setFillColor(rectangleColor);
 
    sf::Font font;
    if (!font.loadFromFile("utils/SpaceMono-Regular.ttf")) {
       printf("couldn't load text font\n");
    }
 
    TextBox ansText(font, 32, ansRectangle.getPosition(), ansRectangle.getSize().x, ansRectangle.getSize().y);
    TextBox cmdText(font, 32, cmdRectangle.getPosition(), cmdRectangle.getSize().x, cmdRectangle.getSize().y);
    
    ansText.setText("Welcome to Personal Cloud Storage 1.0!\nUse the application to save your data across your devices.\nFor application guidance use <help> command.\nFor further instructions use <help [command name]>.");

    struct sockaddr_in server;
    int socketFd;
    if ((socketFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    bzero(&server, sizeof(server));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("0");
    server.sin_port = htons(PORT);

    if (connect(socketFd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1) {
        perror("connect error");
        exit(EXIT_FAILURE);
    }

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            switch (event.type) {
            case sf::Event::Closed:
                window.close(); break;
            case sf::Event::KeyPressed:
                // arrow up
                if (event.key.code == sf::Keyboard::Up && currentCommand > 0) {
                    cout << "*****" << endl;
                    if (currentCommand == numberOfCommands) strcpy(commandsHistory[numberOfCommands], cmdText.getText().c_str());
                    currentCommand--;
                    cmdText.setText(commandsHistory[currentCommand]);
                }
                // arrow down
                if (event.key.code == sf::Keyboard::Down && currentCommand < numberOfCommands) {
                    cout << "*****" << endl;
                    currentCommand++;
                    cmdText.setText(commandsHistory[currentCommand]);
                }
                break;
            case sf::Event::TextEntered:
                // cout << event.text.unicode << '\n';
                // escape
                if (event.text.unicode == 27)
                    window.close();
                // backspace
                if (event.text.unicode == 8 && cmdText.getText().size() > 0) {
                    string temp = cmdText.getText();
                    temp.pop_back();
                    cmdText.setText(temp);
                }
                // printable characters
                if (32 <= event.text.unicode && event.text.unicode <= 126) {
                    string temp = cmdText.getText();
                    temp += event.text.unicode;
                    cmdText.setText(temp);
                }
                // CTRL + V
                if (event.text.unicode == 22 && sf::Keyboard::isKeyPressed(sf::Keyboard::LControl)) {
                    string temp = cmdText.getText() + sf::Clipboard::getString();
                    cmdText.setText(temp);
                }
                // enter
                if (event.text.unicode == 13 && cmdText.getText().size() > 0) {
                    if (cmdReplacement[0] == 0) {
                        strcpy(commandsHistory[numberOfCommands++], cmdText.getText().c_str());
                        currentCommand = numberOfCommands;
                    }
                    
                    strcpy(cmdReplacement, "");
                    strcpy(ansReplacement, "");

                    char command[COMMAND_MAX];
                    strcpy(command, cmdText.getText().c_str());
                    cmdText.setText("");
                    printf("[Debug] command: %s\n\n", command);

                    if (write(socketFd, command, strlen(command)) == -1) {
                        perror("write command to server error");
                        continue;
                    }

                    char serverAnswer[COMMAND_MAX];
                    checkServerAnswer(serverAnswer, socketFd);
                    if (strcmp(serverAnswer, "upload ready") == 0) {
                        uploadCommand(command, socketFd);
                        checkServerAnswer(serverAnswer, socketFd);
                    }
                    if (strcmp(serverAnswer, "download ready") == 0) {
                        downloadCommand(command, socketFd);
                        checkServerAnswer(serverAnswer, socketFd);
                    }
                    if (strncmp("enter the password", serverAnswer, strlen("enter the password")) == 0) {
                        strcpy(cmdReplacement, "password");
                    }
                    if (strncmp("logged in successfully", serverAnswer, strlen("logged in successfully")) == 0 ||
                        strncmp("registered", serverAnswer, strlen("registered")) == 0) {
                        strcpy(whichUserConnected, strchr(serverAnswer, '\'') + 1);
                        // remove second apostrophe
                        whichUserConnected[strlen(whichUserConnected) - 1] = 0;
                    }
                    if (strncmp("logged out successfully", serverAnswer, strlen("logged out successfully")) == 0) {
                        strcpy(whichUserConnected, "guest");
                    }
                    if (strncmp("enter the username", serverAnswer, strlen("enter the username")) == 0) {
                        strcpy(cmdReplacement, "username");
                    }
                    if (strncmp("confirm", serverAnswer, strlen("confirm")) == 0) {
                        strcpy(cmdReplacement, "password-confirmation");
                    }
                    if (strncmp("total of", serverAnswer, strlen("total of")) == 0) {
                        strcpy(ansReplacement, "list");
                    }
                    if (strcmp("exited with no errors", serverAnswer) == 0)
                        window.close();

                    printf("server answer: %s\n", serverAnswer);
                    printf("------------------------\n");

                    ansText.setText(serverAnswer);
                }
                break;
            }
        }

        window.clear();
        window.draw(background);

        window.draw(ansRectangle);
        ansText.draw(window);

        window.draw(cmdRectangle);
        string temp = cmdText.getText();
        // for + overloading with string and char*
        string nothing("");
        if (cmdReplacement[0]) 
            cmdText.setText(nothing + ">" + cmdReplacement + ": " + temp + "_");
        else    
            cmdText.setText(nothing + ">" + whichUserConnected + "@" + currentLocation + ": " + temp + "_");
        
        if (strncmp(cmdReplacement, "password", strlen("password")) == 0)
            cmdText.draw(window, true);
        else 
            cmdText.draw(window);

        window.display();
        cmdText.setText(temp);
    }

    close(socketFd);
    return 0;
}

void uploadCommand(char* command, int socketFd) {
    // server's done the syntax checking
    char *whichCommand = strtok(command, " ");
    char *filename = strtok(NULL, " ");

    if (is_directory(filename)) {
        char errorMessage[]= "{access error} specified path is a directory";
        if (write(socketFd, errorMessage, strlen(errorMessage)) == -1)
            perror("write error");
        return;
    }

    if (access(filename, R_OK) == -1) {
        perror("access error");
        char errorMessage[]= "{access error} file doesn't exists / cannot read from it";
        if (write(socketFd, errorMessage, strlen(errorMessage)) == -1)
            perror("write access error");
        return;
    }

    if (write(socketFd, "upload ready", strlen("upload ready")) == -1) {
        perror("upload ready announce error");
        return;
    }

    // needed so the size wouldn't transmit next to upload announcer
    int bytesRead;
    char temp[COMMAND_MAX];
    if (read(socketFd, temp, COMMAND_MAX - 1) == -1) {
        perror("size ask read error");
        return;
    }

    // both parties are ready
    FILE *localFile = fopen(filename, "r");
    char sectionOfFile[SECTION_MAX + 1]; //+1 for null terminator

    struct stat fileinfo;
    if (stat(filename, &fileinfo) == -1) {
        perror("file stat error");
        return;
    }

    if (write(socketFd, &fileinfo.st_size, sizeof(fileinfo.st_size)) == -1) {
        perror("file size write error");
        return;
    }

    int totalBytesRead = 0, i = 1;

    while (totalBytesRead < fileinfo.st_size) {
        while ((bytesRead = fread(sectionOfFile, 1, SECTION_MAX, localFile)) <= 0);
        sectionOfFile[bytesRead] = 0;

        //debug
        printf("<Section %d [%d bytes]: ", i++, bytesRead);
        for (int i = 0; i <= 20; i++) 
             if (sectionOfFile[i])
                 printf("%c", sectionOfFile[i]);
             else printf("?");
        printf(">\n");

        while (write(socketFd, sectionOfFile, bytesRead) <= 0);

        totalBytesRead += bytesRead;
    }

    printf("[Debug] File size: %ld\n", fileinfo.st_size);
}

void downloadCommand(char* command, int socketFd) {
    // server's done the syntax checking
    char fileLocation[COMMAND_MAX] = "cloudDownloads";
    if (exists(fileLocation) == false)
        if (create_directory(fileLocation) == false) {
            perror("couldn't create user directory");
                return;
        }
    printf("[Debug] user directory created successfully: %s", fileLocation);

    if (write(socketFd, "download ready", strlen("download ready")) == -1) {
        perror("upload ready announce error");
        return;
    }

    char *whichCommand = strtok(command, " ");
    char *filename = strtok(NULL, " ");

    strcat(fileLocation, "/");
    strcat(fileLocation, filename);
    FILE *localFile = fopen(fileLocation, "w");
    char sectionOfFile[SECTION_MAX + 1]; //+1 for null terminator

    struct stat fileinfo;
    if (read(socketFd, &fileinfo.st_size, sizeof(fileinfo.st_size)) == -1) {
        perror("file size read error");
        return;
    }

    printf("[Debug] File size: %ld\n", fileinfo.st_size);
    int bytesRead, totalBytesRead = 0, i = 1;

    while (totalBytesRead < fileinfo.st_size) {
        while ((bytesRead = read(socketFd, sectionOfFile, SECTION_MAX)) <= 0);
        sectionOfFile[bytesRead] = 0;

        //debug
        printf("<Section %d [%d bytes]: ", i++, bytesRead);
        for (int i = 0; i <= 20; i++) 
             if (sectionOfFile[i])
                 printf("%c", sectionOfFile[i]);
             else printf("?");
        printf(">\n");

        fwrite(sectionOfFile, 1, bytesRead, localFile);

        totalBytesRead += bytesRead;
    }
    fflush(NULL);

    printf("[Debug] File size: %ld\n", fileinfo.st_size);

    if (write(socketFd, "upload done", strlen("upload done")) == -1) {
        perror("write upload done error");
        return;
    }
} 

void checkServerAnswer(char* serverAnswer, int socketFd) {
    int bytesRead;

    if ((bytesRead = read(socketFd, serverAnswer, COMMAND_MAX - 1)) <= 0) {
        perror("read from server error / empty buffer");
        return;
    }
    serverAnswer[bytesRead] = 0;
    printf("[Debug] bytesRead: %d, received message: %s\n", bytesRead, serverAnswer);
}