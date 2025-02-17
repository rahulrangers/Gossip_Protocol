#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>

struct PeerInfo
{
    std::string ip;
    int port;
    int degree;
};

class Seed
{
private:
    int port;
    int server_fd;
    std::vector<PeerInfo> peerList;
    std::mutex mtx;

public:
    Seed(int port) : port(port) {}

    void startServer()
    {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
        {
            perror("setsockopt");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 10) < 0)
        {
            perror("Listen");
            exit(EXIT_FAILURE);
        }
        std::cout << "Seed server listening on port " << port << std::endl;
        while (true)
        {
            socklen_t addrlen = sizeof(address);
            int new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
            if (new_socket < 0)
            {
                perror("Accept");
                continue;
            }
            std::thread(&Seed::handleClient, this, new_socket).detach();
        }
    }

    void handleClient(int client_socket)
    {
        std::string dataBuffer;
        char buffer[1024];

        while (true)
        {
            int valread = read(client_socket, buffer, sizeof(buffer));
            if (valread <= 0)
            {
                break;
            }
            dataBuffer.append(buffer, valread);

            size_t pos;
            while ((pos = dataBuffer.find('\n')) != std::string::npos)
            {
                std::string msg = dataBuffer.substr(0, pos);
                dataBuffer.erase(0, pos + 1);

                std::vector<std::string> tokens;
                std::istringstream tokenStream(msg);
                std::string token;
                while (std::getline(tokenStream, token, ':'))
                {
                    tokens.push_back(token);
                }

                if (!tokens.empty())
                {
                    if (tokens[0] == "Register")
                    {
                        if (tokens.size() >= 3)
                        {
                            std::string peerIP = tokens[1];
                            int peerPort = std::stoi(tokens[2]);

                            addPeer(peerIP, peerPort);
                            std::string peer_list = serializePeerList(this->peerList);
                            send(client_socket, peer_list.c_str(), peer_list.size(), 0);
                            std::cout << "Registered peer: " << peerIP << ":" << peerPort << std::endl;
                            writeToOutputFile("Registered peer: " + peerIP + ":" + std::to_string(peerPort));
                        }
                    }
                    else if (tokens[0] == "UpdateDegree")
                    {
                        if (tokens.size() >= 3)
                        {
                            std::unique_lock<std::mutex> lock(mtx);
                            int n = peerList.size();
                            for (int i = 0; i < n; i++)
                            {
                                if (peerList[i].ip == tokens[1] && peerList[i].port == std::stoi(tokens[2]))
                                {

                                    peerList[i].degree++;
                                    break;
                                }
                            }
                            lock.unlock();
                        }
                    }
                    else if (tokens[0] == "DeadNode")
                    {
                        if (tokens.size() >= 5)
                        {

                            int targetPort = std::stoi(tokens[2]);
                            std::lock_guard<std::mutex> lock(mtx);
                            peerList.erase(std::remove_if(peerList.begin(), peerList.end(),
                                                          [targetPort](const PeerInfo &node)
                                                          {
                                                              return node.port == targetPort;
                                                          }),
                                           peerList.end());
                            std::cout << msg << std::endl;
                            writeToOutputFile(msg.substr(0, msg.size()));
                        }
                    }
                }
            }
        }
        close(client_socket);
    }

    std::string getCurrentTimestamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm *localTime = std::localtime(&now);
        std::ostringstream oss;
        oss << "[" << (1900 + localTime->tm_year) << "-"
            << (1 + localTime->tm_mon) << "-"
            << localTime->tm_mday << " "
            << localTime->tm_hour << ":"
            << localTime->tm_min << ":"
            << localTime->tm_sec << "]";
        return oss.str();
    }

    void writeToOutputFile(const std::string &message)
    {
        std::string filename = "seed-" + std::to_string(port) + ".txt";

        std::ofstream outfile(filename, std::ios_base::app);
        if (!outfile.is_open())
        {
            std::cerr << "Error: Unable to open file " << filename << " for writing." << std::endl;
            return;
        }
        outfile << message << std::endl;
        outfile.close();
    }

    std::string serializePeerList(const std::vector<PeerInfo> &peerList)
    {
        std::ostringstream oss;
        for (size_t i = 0; i < peerList.size(); ++i)
        {
            oss << peerList[i].ip << ":" << peerList[i].port << ":" << peerList[i].degree;
            if (i != peerList.size() - 1)
                oss << ";";
        }
        return oss.str();
    }

    void addPeer(const std::string &ip, int port)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto &p : peerList)
        {
            if (p.ip == ip && p.port == port)
                return;
        }
        PeerInfo p;
        p.ip = ip;
        p.port = port;
        p.degree = 0;
        peerList.push_back(p);
    }

    void removePeer(const std::string &ip, int port)
    {
        std::lock_guard<std::mutex> lock(mtx);
        peerList.erase(std::remove_if(peerList.begin(), peerList.end(),
                                      [&](const PeerInfo &p)
                                      { return p.ip == ip && p.port == port; }),
                       peerList.end());
    }
};

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: ./seed <port>" << std::endl;
        return 1;
    }
    int port = std::stoi(argv[1]);
    Seed seed(port);
    seed.startServer();
    return 0;
}
