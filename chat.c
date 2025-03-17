/*
    CHAT APPLICATION
    @date: March, 14, 2025
    @ngokienhoang
*/

/**************************************************************** INCLUDE LIBRARY ****************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h> // Để sử dụng inet_pton
#include <netinet/in.h> // Để sử dụng sockaddr_in
#include <sys/socket.h> // Để sử dụng socket
#include <unistd.h>
#include <pthread.h>

/**************************************************************** DEFINE ****************************************************************/
#define COMMAND_LENGTH  (30)
#define BUFFER_SIZE     (1024)
#define MAX_CONNECTIONS (3)
#define BOOL char
#define TRUE            (1)
#define FALSE           (0)

#define handle_error(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)

typedef struct {
    int socket_fd;
    struct sockaddr_in address;
    pthread_t thread_id;
    int index;
    BOOL bIsClient; // if connection is Client, bIsClient is TRUE
} Connection;

Connection C_PEERS[MAX_CONNECTIONS];

/**************************************************************** PROTOTYPES ****************************************************************/
void print_menu(void);
void print_command_manual(void);
void error_code_handler(uint8_t u8ErrorCode);
void command_handler(char param_cCommand[]);
void client_handler(int p_socket_fd);
static void *create_server_handler(void *args);

/**************************************************************** GLOBAL VARIABES ****************************************************************/
int server_socket = 0;
int connection_cnt = 0;

/**************************************************************** USEFULL FUNCTIONS ****************************************************************/
void remove_connection(int index)
{
    close(C_PEERS[index].socket_fd);

    for (int i = index; i < connection_cnt-1; i++)
    {
        C_PEERS[i]  = C_PEERS[i+1];
        C_PEERS[i].index = i;
    }
    connection_cnt--;
}

void notify_peer_disconnection(int socket_fd)
{
    /* Gửi gói tin Terminate cho socket cần ngắt kết nối */
    const char *terminate_msg = "TERMINATE";
    send(socket_fd, terminate_msg, strlen(terminate_msg), 0);
}

void cmd_list_handler(void)
{
    printf("------------------ ACTIVE CONNECTIONS ------------------\n");
    printf("ID                     IP Address               Port No.\n");
    
    if (0 == connection_cnt)
    {
        printf("* NOTE: Your connections list is empty!\n");
    }
    else
    {
        for (int i = 0; i<connection_cnt; i++)
        {
            printf("%d                  %s                  %d\n", 
                i + 1,
                inet_ntoa(C_PEERS[i].address.sin_addr),
                ntohs(C_PEERS[i].address.sin_port));
        }
    }

    printf("--------------------------------------------------------\n");
}

void cmd_send_handler(int connect_id, const char *message)
{
    int index = 0, ret = 0;

    /* Kiểm tra connect_id có hợp lệ không */
    if ( (connect_id < 1) || (connect_id > connection_cnt) )
    {
        printf("Invalid connection ID.\n");
        return;
    }

    if (connect_id <= connection_cnt)
    {
        index = connect_id - 1;
        ret = send(C_PEERS[index].socket_fd, message, strlen(message), 0);

        if (0 > ret)
        {
            perror("Send failed");
        }
        else
        {
            printf("Message is sent to Chat Room %d [IP <%s> : Port <%d>] successfully.\n",
                connect_id,
                inet_ntoa(C_PEERS[index].address.sin_addr),
                ntohs(C_PEERS[index].address.sin_port));
        }
        
    }
}

void cmd_terminate_handler(int connect_id)
{
    int index = 0;

    /* Kiểm tra connect_id có hợp lệ không */
    if ( (connect_id < 1) || (connect_id > connection_cnt) )
    {
        printf("Invalid connection ID.\n");
        return;
    }

    if (connect_id <= connection_cnt)
    {
        index = connect_id - 1;

        /* Gửi thông báo đến socket được chỉ định bằng connect_id rằng sẽ hủy kết nối */
        notify_peer_disconnection(C_PEERS[index].socket_fd);
        
        printf("Connection with Chat Room %d [IP <%s> : Port <%d>] is terminated.\n",
            connect_id,
            inet_ntoa(C_PEERS[index].address.sin_addr),
            ntohs(C_PEERS[index].address.sin_port));
        
        remove_connection(index);
    }
}

void *ClientReceivedFromServer_fcn(void *arg)
{
    int index = *(int *)arg;
    free(arg);

    char recv_buff[BUFFER_SIZE] = {0};
    int bytes_read = 0;

    if (FALSE == C_PEERS[index].bIsClient) // is Server
    {
        while(1)
        {
            bytes_read = recv(C_PEERS[index].socket_fd, recv_buff, BUFFER_SIZE, 0);
            if (bytes_read > 0)
            {
                recv_buff[bytes_read] = '\0';

                /* Kiểm tra xem có phải Client bị ngắt kết nối bởi lệnh "exit"/"terminate" không */
                if (strcmp(recv_buff, "TERMINATE") == 0) 
                {
                    printf("The Chat Room at [IP <%s> : Port <%d>] has disconnected.\n",
                           inet_ntoa(C_PEERS[index].address.sin_addr),
                           ntohs(C_PEERS[index].address.sin_port));
                    remove_connection(index);
                    pthread_exit(NULL);
                    break;
                }
                else
                {
                    printf("|---------------------- NEW MESSAGE! -------------------\n");
                    printf("| @from: Chat Room IP <%s>\n", inet_ntoa(C_PEERS[index].address.sin_addr));
                    printf("|        Chat Room Port No. <%d>\n", ntohs(C_PEERS[index].address.sin_port));
                    printf("| @message: %s\n", recv_buff);
                    printf("*-------------------------------------------------------\n");
                }
            }
        }
    }
}

void cmd_connect_handler(const char *ip, int port)
{
    int server_fd;
    struct sockaddr_in serv_addr;

    /* Kiểm tra xem số lượng kết nối hiện tại có vượt quá giới hạn không */
    if (MAX_CONNECTIONS <= connection_cnt)
    {
        printf("Maximum connections reached. Cannot connect to more servers.\n");
        return;
    }

    /* Kiểm tra xem có bị trùng lặp kết nối 2 lần không */
    for (int i = 0; i < connection_cnt; i++) 
    {
        if ( (C_PEERS[i].address.sin_addr.s_addr == inet_addr(ip)) &&\
            (C_PEERS[i].address.sin_port == htons(port)) )
        {
            printf("Connection to Chat Room [IP <%s> : Port <%d>] already exists.\n", ip, port);
            return;
        }
    }

    memset(&serv_addr, '0',sizeof(serv_addr));

    /* Khởi tạo địa chỉ server */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) == -1) 
        handle_error("inet_pton()");
    
    /* Tạo socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        handle_error("socket()");
    
    /* Kết nối tới server*/
    if (connect(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        handle_error("connect()");

    C_PEERS[connection_cnt].socket_fd = server_fd;
    C_PEERS[connection_cnt].address = serv_addr;
    C_PEERS[connection_cnt].index = connection_cnt;
    C_PEERS[connection_cnt].bIsClient = FALSE;

    printf("Connected to Chat Room [IP <%s> : Port <%d>] successfully.\n", ip, port);

    int *index = malloc(sizeof(int));
    *index = connection_cnt;
    pthread_create(&C_PEERS[connection_cnt].thread_id, 
        NULL, 
        ClientReceivedFromServer_fcn, 
        index);
    connection_cnt++;
}

void cmd_exit_handler(void)
{

}

void cmd_help_handler(void)
{
    printf("%-40s : %s\n", "01. help", "display user interface options or command manual.");
    printf("%-40s : %s\n", "02. myip", "display IP address of your App.");
    printf("%-40s : %s\n", "03. myport", "display listening port of your App.");
    printf("%-40s : %s\n", "04. connect <destination> <port no>", "connect you to another peer's App to chat.");
    printf("%-40s : %s\n", "05. list", "list all the connected peers.");
    printf("%-40s : %s\n", "06. terminate <connection id.>", "terminate a connection with specified ID mentioned in the list.");
    printf("%-40s : %s\n", "07. send <connection id.> <message>", "send message to a connection with specified ID mentioned in the list.");
    printf("%-40s : %s\n", "08. exit", "close all connections and terminate this App.");
}

void print_menu(void)
{
#if 1
    printf("######################################## WELCOME TO CHAT APPLICATION - 2025 ############################################\n");
    printf(">> Main Menu:\n");
    cmd_help_handler();
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    printf("🛑 NOTE: You can use 'help' command to display this command manual again\n");
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    printf("########################################################################################################################\n");
#else
    printf("---WELCOME TO CHAT APPLICATION - 2025!---\n");
    cmd_help_handler();
#endif
}

void* ServerReceivedFromClient_fcn(void *arg)
{
    int index = *(int *)arg;
    free(arg);

    char recv_buff[BUFFER_SIZE] = {0};
    int bytes_read = 0;

    if (TRUE == C_PEERS[index].bIsClient)
    {
        while(1)
        {
            bytes_read = recv(C_PEERS[index].socket_fd, recv_buff, BUFFER_SIZE, 0);
            if (bytes_read > 0)
            {
                recv_buff[bytes_read] = '\0';

                /* Kiểm tra xem có phải Client bị ngắt kết nối bởi lệnh "exit" không */
                if (strcmp(recv_buff, "TERMINATE") == 0) 
                {
                    printf("The Chat Room at %s:%d has disconnected.\n",
                           inet_ntoa(C_PEERS[index].address.sin_addr),
                           ntohs(C_PEERS[index].address.sin_port));
                    remove_connection(index);
                    pthread_exit(NULL);
                    break;
                }
                else
                {
                    printf("*---------------------- NEW MESSAGE! -------------------\n");
                    printf("* @from: Chat Room IP <%s>\n", inet_ntoa(C_PEERS[index].address.sin_addr));
                    printf("*        Chat Room Port No. <%d>\n", ntohs(C_PEERS[index].address.sin_port));
                    printf("* @message: %s\n", recv_buff);
                    printf("*-------------------------------------------------------\n");
                }
            }
        }
    }
}

void* SeverAcceptClient_handler(void *arg)
{
    while(1)
    {
        struct sockaddr_in client_address;
        socklen_t addr_len = sizeof(client_address);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &addr_len);

        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        /* Kiểm tra xem số lượng kết nối đã bị vượt quá hay chưa */
        if (connection_cnt >= MAX_CONNECTIONS) {
            printf("Maximum connections reached. Connection rejected.\n");
            close(client_socket);
            continue;
        }

        C_PEERS[connection_cnt].socket_fd = client_socket;
        C_PEERS[connection_cnt].address = client_address;
        C_PEERS[connection_cnt].index = connection_cnt;
        C_PEERS[connection_cnt].bIsClient = TRUE; // đây là 1 Client kết nối đến Server

        printf("\nNew connection from Chat Room [IP <%s> : Port <%d>]\n",
            inet_ntoa(client_address.sin_addr),
            ntohs(client_address.sin_port));
        
        /* Tạo một thread riêng để Server nhận dữ liệu từ Client */
        int *index = malloc(sizeof(int));
        *index = connection_cnt;
        pthread_create(&C_PEERS[connection_cnt].thread_id, 
            NULL,
            ServerReceivedFromClient_fcn,
            index);
        
        /* Tăng biến đếm số lượng kết nối */
        connection_cnt += 1;
    }
}

/**************************************************************** MAIN FUNCTION ****************************************************************/

int main(int argc, char* argv[])
{
    int port_no = 0, port_connect_no = 0;
    char command[BUFFER_SIZE];
    char *cmd, *ip_connect, *port_connect, *id_str, *message;
    int connect_id = 0;
    struct sockaddr_in server_address;
    pthread_t SeverAcceptClient_thread;

    if (argc < 2)
    {
        printf("No port provided\ncommand: ./server <port number>\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        port_no = atoi(argv[1]);
    }

    memset(&server_address, 0, sizeof(server_address ));

    /* 01 - Create socket */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        handle_error("socket()");
    }

    /* 01.1 - Init server information */
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_no); // convert to network byte order (MSB)
    server_address.sin_addr.s_addr = INADDR_ANY; // inet_addr("192.168.49.128")

    /* 02. Binding */
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    /* 03. Listenning */
    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        return 1;
    }

    /* 04. In ra menu lần đầu tiên chạy chương trình */
    print_menu();
    printf("This Chat Room is listening on port %d...\n", port_no);

    /* 05. Tạo một thread riêng để chấp nhận kết nối từ Client */ 
    if (0 != pthread_create(&SeverAcceptClient_thread, NULL, &SeverAcceptClient_handler, NULL))
    {
        printf("Thread create() error\n");
        return -1;
    }

    /* 06. Xử lý lệnh do người dùng nhập vào */
    while(1)
    {
        printf("\n>> Enter your command: ");
        fgets(command, BUFFER_SIZE, stdin);

        // Dùng strtok() để lấy token đầu tiên trong command, phân tách bởi dấu cách " " hoặc xuống dòng "\n".
        cmd = strtok(command, " \n");
        if (NULL == cmd)
            continue;
        
        if (0 == strcmp(cmd, "list"))
        {
            cmd_list_handler();
        }
        else if (0 == strcmp(cmd, "help"))
        {
            printf("-------------------- COMMANDS MANUALS ------------------\n");
            cmd_help_handler();
            printf("--------------------------------------------------------\n");
        }
        else if (0 == strcmp(cmd, "connect")) // Kiểm tra xem token đầu tiên (cmd) có phải "connect" hay không.
        {
            /* Lấy tiếp hai token tiếp theo
                strtok: ở các lần tiếp theo, nếu truyền NULL, nó sẽ tiếp tục xử lý chuỗi cũ từ vị trí sau token trước đó.
            */
            ip_connect = strtok(NULL, " ");
            port_connect = strtok(NULL, " \n");

            if ((NULL == ip_connect) || (NULL == port_connect)) 
            {
                printf("Usage: connect <ip> <port>\n");
                continue;
            }
            port_connect_no = atoi(port_connect);
            cmd_connect_handler(ip_connect, port_connect_no);
        }
        else if (0 == strcmp(cmd, "send"))
        {
            id_str = strtok(NULL, " ");
            message = strtok(NULL, "\n");

            if ((NULL == id_str) || (NULL == message))
            {
                printf("Usage: send <connection_id> <message>\n");
                continue;
            }

            connect_id = atoi(id_str);
            cmd_send_handler(connect_id, message);
        }
        else if (0 == strcmp(cmd, "terminate"))
        {
            id_str = strtok(NULL, " ");

            if (NULL == id_str)
            {
                printf("Usage: terminate <connection_id>\n");
                continue;
            }
            
            connect_id = atoi(id_str);
            cmd_terminate_handler(connect_id);
        }
        else if (0 == strcmp(cmd, "exit"))
        {
            /* Gửi thông báo terminate đến toàn bộ các kết nối hiện có */
            for (int i = 0; i < connection_cnt; i++)
            {
                notify_peer_disconnection(C_PEERS[i].socket_fd);
                close(C_PEERS[i].socket_fd);
            }

            // Đóng socket server
            close(server_socket);

            printf("Exiting Chat App...\n");
            return 0;
        }
        else
        {
            printf("Invalid command. Type 'help' to see Commands Manual.\n");
        }
    }

    return 0;
}
/**************************************************************** THREAD HANDLER ****************************************************************/

