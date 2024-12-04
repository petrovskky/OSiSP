#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <ws2tcpip.h>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

#define PORT 54000
#define BUFFER_SIZE 1024

void initWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Ошибка инициализации Winsock" << '\n';
        exit(EXIT_FAILURE);
    }
}

struct Client {
    SOCKET socket;
    int id;
};

struct Message {
    int sender_id;
    int recipient_id;
    std::string body;
};

std::vector<Client> clients;
CRITICAL_SECTION clientsCriticalSection;
int nextClientId = 1;

bool sendMessageToClient(int recipientId, const std::string& message) {
    EnterCriticalSection(&clientsCriticalSection);
    for (const Client& client : clients) {
        if (client.id == recipientId) {
            send(client.socket, message.c_str(), message.size() + 1, 0);
            LeaveCriticalSection(&clientsCriticalSection);
            return true;
        }
    }
    LeaveCriticalSection(&clientsCriticalSection);
    return false;
}

void broadcastMessage(const std::string& message, SOCKET sender) {
    EnterCriticalSection(&clientsCriticalSection);
    for (const Client& client : clients) {
        if (client.socket != sender) {
            send(client.socket, message.c_str(), message.size() + 1, 0);
        }
    }
    LeaveCriticalSection(&clientsCriticalSection);
}

DWORD WINAPI handleClient(LPVOID clientSocketPtr) {
    Client client = *(Client*)clientSocketPtr;
    char buffer[BUFFER_SIZE];
    int bytesReceived;

    std::cerr << "Клиент " << client.id << " подключен." << '\n';
    broadcastMessage("Клиент " + std::to_string(client.id) + " подключен.", client.socket);

    while ((bytesReceived = recv(client.socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::string message(buffer);

        if (message.substr(0, 2) == "/m") {
            std::istringstream iss(message);
            std::string command, recipientStr, msg;
            iss >> command >> recipientStr;
            std::getline(iss, msg);
            if (msg.empty()) {
                std::cout << "Ошибка: введен неверный формат сообщения. ID отправителя: " << client.id << ". Сообщение: " << message << '\n';
                send(client.socket, "Ошибка: введен неверный формат сообщения. Верный формат: /m <id_получателя> <тело_сообщения>", 93, 0);
                continue;
            }

            msg = msg.substr(1);

            try {
                int recipientId = std::stoi(recipientStr);

                Message newMessage = { client.id, recipientId, msg };

                if (sendMessageToClient(newMessage.recipient_id, "Клиент " + std::to_string(newMessage.sender_id) + ": " + newMessage.body)) {
                    std::cout << "Сообщение клиенту " << newMessage.recipient_id << " от клиента " << newMessage.sender_id << ": " << newMessage.body << '\n';
                }
                else {
                    std::cout << "Ошибка: клиент с ID " << newMessage.recipient_id << " не найден. ID отправителя: " << client.id << ". Тело сообщения: " << newMessage.body << '\n';
                    send(client.socket, "Ошибка: клиент не найден.", 26, 0);
                }
            }
            catch (const std::invalid_argument& e) {
                std::cout << "Ошибка: введен некорректный ID получателя. ID отправителя: " << client.id << ". Сообщение: " << message << '\n';
                send(client.socket, "Ошибка: введен некорректный ID получателя.", 43, 0);
            }
            catch (const std::out_of_range& e) {
                std::cout << "Ошибка: введен некорректный ID получателя (выходит за пределы диапазона). ID отправителя: " << client.id << ". Сообщение: " << message << '\n';
                send(client.socket, "Ошибка: введен некорректный ID получателя (выходит за пределы диапазона).", 74, 0);
            }
        }
        else {
            std::cout << "Сообщение от клиента " << client.id << ": " << message << '\n';
            broadcastMessage("Клиент " + std::to_string(client.id) + ": " + message, client.socket);
        }
    }

    std::cout << "Клиент " << client.id << " отключен." << '\n';
    broadcastMessage("Клиент " + std::to_string(client.id) + " отключен.", client.socket);
    EnterCriticalSection(&clientsCriticalSection);
    clients.erase(std::remove_if(clients.begin(), clients.end(),
        [client](const Client& c) { return c.socket == client.socket; }),
        clients.end());
    LeaveCriticalSection(&clientsCriticalSection);

    closesocket(client.socket);
    return 0;
}

void startServer() {
    InitializeCriticalSection(&clientsCriticalSection);

    SOCKET listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket == INVALID_SOCKET) {
        std::cerr << "Не удалось создать сокет" << '\n';
        WSACleanup();
        return;
    }

    sockaddr_in serverHint;
    ZeroMemory(&serverHint, sizeof(serverHint));
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverHint.sin_addr);

    if (bind(listeningSocket, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
        std::cerr << "Ошибка привязки сокета" << '\n';
        closesocket(listeningSocket);
        WSACleanup();
        return;
    }

    listen(listeningSocket, SOMAXCONN);
    std::cout << "Сервер запущен, ожидание клиентов..." << '\n';

    while (true) {
        SOCKET clientSocket = accept(listeningSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Ошибка при подключении клиента" << '\n';
            continue;
        }

        EnterCriticalSection(&clientsCriticalSection);
        Client newClient = { clientSocket, nextClientId++ };
        clients.push_back(newClient);
        LeaveCriticalSection(&clientsCriticalSection);

        HANDLE clientThread = CreateThread(nullptr, 0, handleClient, &newClient, 0, nullptr);
        if (clientThread == nullptr) {
            std::cerr << "Ошибка при создании потока для клиента" << '\n';
            closesocket(clientSocket);
        }
        else {
            CloseHandle(clientThread);
        }
    }

    DeleteCriticalSection(&clientsCriticalSection);
    closesocket(listeningSocket);
    WSACleanup();
}

DWORD WINAPI receiveMessages(LPVOID clientSocketPtr) {
    SOCKET clientSocket = *(SOCKET*)clientSocketPtr;
    char buffer[BUFFER_SIZE];
    int bytesReceived;

    while ((bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        std::cout << buffer << '\n';
    }

    return 0;
}

void startClient(const std::string& serverIp) {
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Не удалось создать сокет" << '\n';
        WSACleanup();
        return;
    }
        
    sockaddr_in serverHint;
    ZeroMemory(&serverHint, sizeof(serverHint));
    serverHint.sin_family = AF_INET;
    serverHint.sin_port = htons(PORT);
    inet_pton(AF_INET, serverIp.c_str(), &serverHint.sin_addr);

    if (connect(clientSocket, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
        std::cerr << "Не удалось подключиться к серверу" << '\n';
        closesocket(clientSocket);
        WSACleanup();
        return;
    }

    HANDLE receiveThread = CreateThread(nullptr, 0, receiveMessages, &clientSocket, 0, nullptr);
    if (receiveThread == nullptr) {
        std::cerr << "Ошибка при создании потока для получения сообщений" << '\n';
        closesocket(clientSocket);
        return;
    }
    CloseHandle(receiveThread);

    std::string userInput;
    while (true) {
        std::getline(std::cin, userInput);
        if (userInput == "/exit") {
            break;
        }
        send(clientSocket, userInput.c_str(), userInput.size() + 1, 0);
    }

    closesocket(clientSocket);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru");
    initWinsock();

    if (argc == 2 && std::string(argv[1]) == "server") {
        startServer();
    }
    else if (argc == 3 && std::string(argv[1]) == "client") {
        std::string serverIp = argv[2];
        startClient(serverIp);
    }
    else {
        std::cerr << "Необходимо указать параметры запуска: \"server\" или \"client <IP>\"" << '\n';
    }

    return 0;
}