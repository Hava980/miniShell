#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stddef.h>
#include <fcntl.h>

#define MAX_ARGS 10
#define MAX_ARG_LENGTH 511 //510 + \0.
#define MAX_LINE_LENGTH 512 //510 + \n + \0

typedef struct node{
    char data[MAX_ARG_LENGTH];
    struct node *next;
}node;
//This function adds a new node to the linked list.
void addNode(char* data);
//This function frees the memory of the linked list.
void freeLinkedList();
//This function checks the case that the user wants to use the variable
char* dollar(char* token,int indexOfDollar,int* lenOfLine,char* varDoesNotE);
//This function does what strTok does
char* myStrTok(register char *s,register const char * delim,int* spacesSkipped);
char * myStrTok_r (char *s, char *delim, char **save_ptr);
int theFirstSemiColon(const char *s);
void freeArgs(char** args);
void myExit();
void myExitSyC();
void sig_child_handler(int);
void sig_stp_handler(int);
node* head = NULL;
int  numOfCmd = 0,numOfArgs = 0,countArgs,status,needToWait=1;
pid_t lastProcess =-1 ,pid = -1;

int main() {
    char varDoesNotE[MAX_ARG_LENGTH],line[MAX_LINE_LENGTH],cmd[MAX_ARG_LENGTH],nameOfFile[MAX_ARG_LENGTH];
    int countEnter = 0,spacesSkipped = 0,needToFreeT = 0,lenOfLine;
    char *args[MAX_ARGS + 1];//10 args + null.
    char *savePtr, *savePtrQ, *folder, *savePipePtr, *token;
    signal(SIGCHLD, sig_child_handler);
    signal(SIGTSTP, sig_stp_handler);

    while (1) {
        nextPrompt:
        folder = getcwd(cmd, sizeof(cmd));//current dir
        if (folder == NULL) {
            myExitSyC();
        }
        printf("#cmd:%d|#args:%d@%s>",numOfCmd,numOfArgs,folder);//the prompt.
        if(fgets(line, MAX_LINE_LENGTH, stdin)==NULL){//input from the user.
            myExit();
        }
        lenOfLine = (int)strlen(line)-1;
        if(line[strlen(line) - 1]!='\n') {//If the length of the command is greater than 510
            printf("ERR\n");
            //Allows the next fGets() to read a new command and not what is left of the previous one.
            while ((getchar()) != '\n');
            continue;
        }
        line[strlen(line) - 1] = '\0'; // remove the newline character

        if(strlen(line) == 0 ){//exit if the user presses enter three times in a row.
            countEnter++;
            if(countEnter==3){
                break;
            }
        }else{
            countEnter=0;
        }
        // Parse the input line into separate commands
        int stdin_copy = dup(STDIN_FILENO);
        char* command = myStrTok_r(line, ";",&savePtr);
        while (command != NULL) {//while there is a command.
            int  numOfPipeCmd=1,numOfPipe=-1,needToWriteToF=0;
            for (int i = 0; command[i] != '\0'; ++i) {//count the cmd between the pipes.
                if (command[i] == '|')
                    numOfPipeCmd++;
            }
            // Parse the command into separate commands without the pipes
            char *pipeT = strtok_r(command, "|", &savePipePtr);
            while (pipeT != NULL) {
                countArgs = 0;
                int fd[2];
                if (numOfPipeCmd > 1) {//there is a pipe, therefor we create a pipe.
                    numOfPipe++;
                    if (pipe(fd) == -1) {
                        myExitSyC();
                    }
                }

                int isQuotationM = 0;
                char string[MAX_ARG_LENGTH];
                string[0] = '\0';

                // Parse the current command into separate arguments
                token = myStrTok(pipeT, " ", &spacesSkipped);
                while (token != NULL && countArgs < MAX_ARGS) {
                    needToFreeT = 0;
                    tokenL:;

                    char *ptrEqual = strchr(token, '=');
                    char *ptrDollar = strchr(token, '$');
                    char *ptrQuotationM = strchr(token, '"');

                    if (ptrDollar != NULL) {//checks the case that the user wants to use a variable
                        //cases that $ is not a variable.
                        if (strcmp(token, "$") == 0 || strcmp(token, "\"$") == 0 || strcmp(token, "$\"") == 0 ||
                            strcmp(token, "\"$\"") == 0 || strcmp(token, "$&") == 0 ||
                            (strlen(token)>1 && token[0]=='$' && token[1]=='>')) {
                            goto next;
                        }
                        int indexOfDollar = (int) (ptrDollar - token);
                        char *temp = dollar(token, indexOfDollar, &lenOfLine, varDoesNotE);
                        //dollar returns null if the var dose not exist
                        if (temp != NULL) {//var exist
                            if(needToFreeT)
                                free(token);
                            token = strdup(temp);
                            needToFreeT = 1;
                        } else if (ptrQuotationM != NULL ||
                                   isQuotationM) {//The variable does not exist, and it is in a quot
                            char *substr = strstr(token, varDoesNotE);
                            if (substr != NULL) {
                                char* tempSubStr = strdup(substr);
                                strcpy(substr, tempSubStr + strlen(varDoesNotE));
                                free(tempSubStr);
                            }
                        } else {//If the variable does not exist, and it is not in the quote
                            free(temp);
                            goto nextToken;// then we will treat it as if it was not written
                        }
                        free(temp);
                        goto tokenL;//checking again the token, maybe there are more dollars in token.
                    }
                    next:
                    if (ptrEqual != NULL) {//When defining a variable
                        char tempToken[MAX_ARG_LENGTH];
                        strcpy(tempToken, token);
                        token = myStrTok(NULL, " ", &spacesSkipped);//finds the next args.
                        if (token != NULL) {//if it is a case like this: var=aa a
                            printf("ERR\n");
                            goto nextCmd;
                        }
                        if (lenOfLine >=
                            MAX_ARG_LENGTH) {//goes into If when the value of a variable causes the command
                            // to be longer than allowed
                            printf("ERR\n");
                            if (needToFreeT) {
                                free(token);
                            }
                            goto nextPrompt;
                        }
                        addNode((char *) tempToken);//adds the var to the linked list
                        goto nextCmd;
                    }
                    if (ptrQuotationM != NULL ||
                        isQuotationM) {//if there is '"' in token OR it is a word inside a '"'
                        while (spacesSkipped > 0) {//Keeps the spaces
                            strcat(string, " \0");
                            spacesSkipped--;
                        }
                        if (!isQuotationM) {//if it is the first qM
                            string[0] = '\0';
                            int countQ = 0;
                            for (int i = 0; token[i] != '\0'; i++) {//count qM
                                if (token[i] == '\"') {
                                    countQ++;
                                }
                            }
                            char *tokenQm = strtok_r(token, "\"", &savePtrQ);
                            while (tokenQm != NULL) {
                                strcat(string, tokenQm);
                                tokenQm = strtok_r(NULL, "\"", &savePtrQ);
                            }
                            if (countQ > 1) {//if it is just one word inside the quotation marks.
                                args[countArgs++] = strdup(string);
                            } else {//we will mark that we in a middle of quotation marks.
                                isQuotationM = 1;
                            }
                        } else if (ptrQuotationM == NULL) {//another word in the quotation marks.
                            strcat(string, token);
                        } else {//the quotation mark in the end.
                            char *tokenQm = strtok_r(token, "\"", &savePtrQ);
                            while (tokenQm != NULL) {
                                strcat(string, tokenQm);
                                tokenQm = strtok_r(NULL, "\"", &savePtrQ);
                            }
                            isQuotationM = 0;
                            args[countArgs++] = strdup(string);
                        }
                        goto nextToken;
                    }

                    //in a regular case
                    args[countArgs++] = strdup(token);
                    nextToken:
                    if (needToFreeT) {
                        free(token);
                    }
                    token = myStrTok(NULL, " ", &spacesSkipped);//finds the next args.
                }
                args[countArgs] = NULL;//the last argument should be NULL
                if (countArgs == 0) {
                    goto nextCmd;
                }
                if (lenOfLine >= MAX_ARG_LENGTH) {//The command is longer than allowed
                    printf("ERR\n");
                    freeArgs(args);
                    goto nextPrompt;
                }
                if(countArgs != 0 && strcmp(args[0], "bg") == 0) {//if the user enter bg
                    if(lastProcess==-1){//if there is no process that stopped, so we ignore bg
                        printf("ERR\n");
                        freeArgs(args);
                        goto nextCmd;
                    } else {//continue the last process.
                        needToWait=0;//the parent won`t wait for this child.
                        kill(lastProcess, SIGCONT);
                        lastProcess=-1;
                    }
                    numOfCmd++ , numOfArgs++;
                    freeArgs(args);
                    goto nextCmd;
                }
                if (countArgs != 0 && strcmp(args[0], "cd") == 0) {//cd command
                    printf("cd not supported\n");
                    freeArgs(args);
                    goto nextCmd;
                } else if (countArgs == MAX_ARGS && token != NULL) {//there are more than ten args
                    if(numOfPipe!=-1){//redirecting stdin to the original stdin, and closing the pipe.
                        if(stdin_copy==-1 || dup2(stdin_copy, STDIN_FILENO)==-1){
                            myExitSyC();
                        }
                        close(fd[0]), close(fd[1]);
                    }
                    printf("ERR\n");
                    freeArgs(args);
                    goto nextCmd;
                }
                needToWait = 1;
                //handler these situations: command&, command &
                if (args[countArgs - 1][strlen(args[countArgs - 1]) - 1] == '&') {
                    if (strcmp(args[countArgs - 1], "&") == 0) {
                        free(args[countArgs - 1]);
                        args[countArgs - 1] = NULL;
                        countArgs--;
                    } else {
                        if(countArgs==MAX_ARGS){
                            printf("ERR\n");
                            freeArgs(args);
                            goto nextCmd;
                        }
                        args[countArgs - 1][strlen(args[countArgs - 1]) - 1] = '\0';
                    }
                    needToWait = 0;
                }
                for (int i = 0; i < countArgs; i++) {//checks if one of the args are includes '>'
                    const char *operator = strchr(args[i], '>');
                    if(operator!=NULL){
                        int locationOfOp = (int)(operator-args[i]);
                        //handler these four situations: command>file, command >file, command> file, command > file
                        if(args[i][strlen(args[i])-1]!='>' && args[i][0]!='>'){
                            if(countArgs>=(MAX_ARGS-1)){
                                printf("ERR\n");
                                freeArgs(args);
                                goto nextCmd;
                            }
                            strcpy(nameOfFile, &args[i][locationOfOp + 1]);
                            args[i][locationOfOp]='\0';
                            needToWriteToF=1; numOfArgs+=2;
                            break;
                        }else if(strcmp(args[i],">")==0){
                            free(args[i]);
                            args[i] = NULL;
                            countArgs -= 2;
                        }else if(args[i][0]=='>'){
                            if(countArgs>=MAX_ARGS){
                                printf("ERR\n");
                                freeArgs(args);
                                goto nextCmd;
                            }
                            strcpy(nameOfFile, &args[i][1]);
                            free(args[i]);
                            args[i]=NULL; needToWriteToF=1; countArgs--; numOfArgs+=2;
                            break;
                        }else{
                            if(countArgs>=MAX_ARGS){
                                printf("ERR\n");
                                freeArgs(args);
                                goto nextCmd;
                            }
                            args[i][strlen(args[i])-1]='\0';
                            countArgs--;
                        }
                        strcpy(nameOfFile, args[i+1]);
                        free(args[i+1]);
                        args[i + 1] = NULL; needToWriteToF=1; numOfArgs+=2;
                        break;
                    }
                }
                if(needToWait==0){//changes the numbers in the prompt before finishing the command.
                    numOfCmd++;
                    numOfArgs += countArgs + 1;
                }
                pid = fork();//Create a child process.
                if (pid == -1) {
                    freeArgs(args);
                    myExitSyC();
                } else if (pid == 0) {
                    if (numOfPipe != -1 && numOfPipe < numOfPipeCmd - 1 ) {//redirecting the stdout to the pipe
                        if(dup2(fd[1], STDOUT_FILENO)==-1){
                            myExitSyC();
                        }
                        close(fd[0]), close(fd[1]);
                    }
                    if (needToWriteToF) {//redirecting the stdout to the file
                        int f = open(nameOfFile, O_WRONLY | O_CREAT | O_TRUNC,0666);
                        if (f<0) {
                            myExitSyC();
                        }
                        if(dup2(f, STDOUT_FILENO)==-1){
                            myExitSyC();
                        }
                        close(f);
                    }
                    // child process
                    if (execvp(args[0], args) == -1) {
                        perror("ERR");
                        freeArgs(args);
                        exit(EXIT_FAILURE);
                    }
                } else {
                    // parent process
                    if (needToWait) {//fg processes
                        waitpid(pid, &status, WUNTRACED);
                        if(status!=0){//if the command failed
                            if(needToWriteToF)//updating numOfArgs.
                                numOfArgs-=2;
                            if(numOfPipe!=-1){//redirecting stdin to the original stdin, and closing the pipe.
                                if(stdin_copy==-1 || dup2(stdin_copy, STDIN_FILENO)==-1){
                                    myExitSyC();
                                }
                                close(fd[0]), close(fd[1]);
                            }
                            freeArgs(args);
                            goto nextCmd;
                        }
                        numOfCmd++;
                        numOfArgs += countArgs;
                    }
                    if (numOfPipe != -1 && numOfPipe < numOfPipeCmd - 1) {//if there is a pipe,
                        // and we are not in the last command, then redirecting stdin to the pipe.
                        if(dup2(fd[0], STDIN_FILENO)==-1){
                            myExitSyC();
                        }
                        close(fd[0]), close(fd[1]);
                    }
                    if (numOfPipe == numOfPipeCmd - 1) {//there is a pipe, and we are in the last command,
                        // therefor redirecting stdin to the original stdin.
                        if(stdin_copy==-1 || dup2(stdin_copy, STDIN_FILENO)==-1){
                            myExitSyC();
                        }
                        close(fd[0]), close(fd[1]);
                    }
                    freeArgs(args);
                }

                pipeT = strtok_r(NULL, "|", &savePipePtr);
            }

            nextCmd:
            if (needToFreeT){
                free(token);
            }
            command = myStrTok_r(NULL, ";",&savePtr);//finds the next command.
        }
    }
    freeLinkedList();//frees the memory of the linked list.
    return 0;
}
void  sig_stp_handler(int sigNum){
    if(pid==-1 || needToWait==0)//when the user enter ctrl-z in the beginning
        return;
    lastProcess=pid;//saving the pid of the last process that stopped.
    kill(lastProcess,SIGSTOP);
}
void sig_child_handler(int sigNum){
    while(waitpid(-1,&status,WNOHANG)>0);//if process was finished then releases the child
}
void freeArgs(char** args){//free the array of args.
    for(int i=0;args[i]!=NULL;i++){
        free(args[i]);
    }
}
void myExit(){//free the list, print err and exit.
    freeLinkedList();
    printf("ERR\n");
    exit(EXIT_FAILURE);
}
void myExitSyC(){//free the list, print err and exit.
    freeLinkedList();
    perror("ERR");
    exit(EXIT_FAILURE);
}
//This function adds a new node to the linked list.
void addNode(char* data){
    node *tempNode = (node*) malloc(sizeof(node));
    if(tempNode==NULL){
        myExit();
    }
    for (int i=0;i <=strlen(data);i++){
        tempNode->data[i]=data[i];
    }
    if(head==NULL){
        tempNode->next = NULL;
    } else{
        tempNode->next = head;
    }
    head=tempNode;
}
//This function frees the memory of the linked list.
void freeLinkedList() {
    while (head != NULL) {
        node* temp = head;
        head = head->next;
        free(temp);
    }
}
//The function takes the token and where the variable is registered it replaces it with its real value if it exists,
//if it doesn't exist then returns null
char* dollar(char* token,int indexOfDollar,int* lenOfLine,char* varDoesNotE){
    int lenOfRealVar;
    char realVar[MAX_ARG_LENGTH];
    char tempChar[MAX_ARG_LENGTH];
    char varChar[strlen(token)+2];
    realVar[0]='\0';
    int lenOfVar=0;

    for(int i=indexOfDollar+1;i<strlen(token)&&token[i]!='"';i++){//Calculating the length of var
        lenOfVar++;
    }
    *lenOfLine=*lenOfLine-lenOfVar-1;//We will subtract the length of $var from the length of line
    //varDoesNotE = "$var"
    strcpy(tempChar,&token[indexOfDollar]);
    tempChar[lenOfVar+1]='\0';
    for (int i=0;i <=strlen(tempChar);i++){
        varDoesNotE[i]=tempChar[i];
    }
    //varChar = "var="
    strcpy(varChar, &token[indexOfDollar+1]);
    varChar[lenOfVar]='=';
    varChar[lenOfVar + 1]='\0';

    //search var in a linked list of a saved variables.
    node* temp=head;
    while (temp!=NULL){
        if(strstr(temp->data,varChar)){
            //If the variable exists then we will replace it with the real variable,
            //if there are quotation marks they will also be added and at the end they will go into realVar
            lenOfRealVar = (int)strlen(temp->data) - lenOfVar - 1;
            *lenOfLine=*lenOfLine+lenOfRealVar;
            strncpy(realVar, token, indexOfDollar); //realVar="
            realVar[indexOfDollar]='\0';
            char tempStr[lenOfRealVar+1];
            strncpy(tempStr, temp->data + lenOfVar + 1, lenOfRealVar);
            tempStr[lenOfRealVar]='\0';
            strcat(realVar,tempStr);
            strcpy(tempStr,&token[ indexOfDollar + lenOfVar + 1]);
            strcat(realVar,tempStr);
            return strdup(realVar);//realVar = The actual value of the variable
        }
        temp=temp->next;
    }
    return NULL;
}

//This code is the code of the normal strTok, only I added a variable that will count the spaces.
char* myStrTok(register char *s,register const char * delim,int* spacesSkipped){
    register char *delimA;
    register int sc;
    register int c;
    char *tok;
    static char *last;
    *spacesSkipped=0;
    if (s == NULL && (s = last) == NULL)
        return (NULL);
    /*
     * Skip (span) leading delimiters (s += strSpn(s, delim), sort of).
     */
    cont:
    c = (unsigned char)(*s++);
    for (delimA = (char *)delim; (sc = (unsigned char)(*delimA++)) != 0;) {
        *spacesSkipped+=1;
        if (c == sc)
            goto cont;
    }
    if (c == 0) {        /* no non-delimiter characters */
        last = NULL;
        return (NULL);
    }
    tok = s - 1;
    /*
     * Scan token (scan for delimiters: s += strCSpn(s, delim), sort of).
     * Note that delim must have one NUL; we stop if we see that, too.
     */
    for (;;) {
        c =(unsigned char)(*s++);
        delimA = (char *)delim;
        do {
            if ((sc = (unsigned char)(*delimA++)) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                last = s;
                return (tok);
            }
        } while (sc != 0);
    }
}
//This code is the code of the normal strTok_r, only I make the function to skip ';' which is in a quote.
char * myStrTok_r (char *s,char *delim, char **save_ptr){
    char *end;
    if (s == NULL){
        s = *save_ptr;
    }
    int first=theFirstSemiColon(s);
    if (*s == '\0'){
        *save_ptr = s;
        return NULL;
    }
    /* Scan leading delimiters.  */
    s += strspn(s, delim);
    if (*s == '\0'){
        *save_ptr = s;
        return NULL;
    }
    /* Find the end of the token.  */
    end = s + first;
    if (*end == '\0'){
        *save_ptr = end;
        return s;
    }
    /* Terminate the token and make *SAVE_PTR point past it.  */
    *end = '\0';
    *save_ptr = end + 1;
    return s;
}
int theFirstSemiColon(const char *s){//finds the first ';' which is not inside a quote.
    int inside_quotes=0;
    for (int i=0;s[i]!='\0';i++) {
        if (s[i] == '"') {
            inside_quotes = !inside_quotes;
        } else if (s[i] == ';' && !inside_quotes) {
            return i;
        }
    }
    return (int)strlen(s);
}
