#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdlib>
#include <random>
#include <ctime>
#include <algorithm>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iomanip>
#include <unistd.h>
#include <map>
#include <set>
#include <sys/socket.h>

std::mt19937 rng(static_cast<unsigned int>(
    std::chrono::steady_clock::now().time_since_epoch().count()));

struct NodeInfo
{
    std::string ip;
    int port;
    int degree;
};
struct NodeSocket
{
    int sockfd;
    std::string ip;
    int port;
    NodeSocket(int sock, const std::string &ipAddr, int p)
        : sockfd(sock), ip(ipAddr), port(p) {}
};

class Peer
{
private:
    std::string selfIP;
    int selfPort;
    int peer_fd;
    int degree;
    std::vector<NodeInfo> TotalSeedList;
    std::vector<NodeInfo> seedList;
    std::vector<NodeInfo> UnionPeerList;
    std::vector<NodeInfo> peerList;
    std::vector<NodeSocket> seedsockets;
    std::vector<NodeSocket> peersockets;
    std::map<std::string, std::string> MessageList;
    std::mutex mtx;
    std::condition_variable cv;
    std::map<int, int> Misses;

public:
    int getPeerfd()
    {
        return peer_fd;
    }
    Peer(const std::string &ip, int port) : selfIP(ip), selfPort(port)
    {
        degree = 0;
    }

    void loadConfig(const std::string &filename)
    {
        std::ifstream infile(filename);
        std::string line;
        while (std::getline(infile, line))
        {
            std::istringstream iss(line);
            std::string ip_port;
            if (!(iss >> ip_port))
                continue;
            size_t pos = ip_port.find(":");
            if (pos != std::string::npos)
            {
                std::string ip = ip_port.substr(0, pos);
                int port = std::stoi(ip_port.substr(pos + 1));
                NodeInfo p{ip, port};
                TotalSeedList.push_back(p);
            }
        }
    }
    int myRandom()
    {
        return 100 * peer_fd + peer_fd * 3 + 10 * peer_fd;
    }
    void writeToOutputFile(const std::string &message)
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::string filename = "peer-" + std::to_string(selfPort) + ".txt";
        std::ofstream outfile(filename, std::ios_base::app);
        if (!outfile.is_open())
        {
            std::cerr << "Error: Unable to open file " << filename << " for writing." << std::endl;
            return;
        }
        outfile << message << std::endl;
        outfile.close();
        outfile.flush();
    }
    void registerWithSeeds()
    {
        int n = TotalSeedList.size();
        int required = n / 2 + 1;
        std::vector<int> indices(n);
        for (size_t i = 0; i < n; i++)
            indices[i] = i;
        std::shuffle(indices.begin(), indices.end(), rng);
        for (int i = 0; i < required; i++)
        {
            NodeInfo seed = TotalSeedList[indices[i]];
            seedList.push_back(seed);
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0)
            {
                perror("Socket creation error");
                continue;
            }
            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(seed.port);
            if (inet_pton(AF_INET, seed.ip.c_str(), &serv_addr.sin_addr) <= 0)
            {
                std::cerr << "Invalid address: " << seed.ip << std::endl;
                close(sockfd);
                continue;
            }
            if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
            {
                std::cerr << "Connection failed to seed: " << seed.ip << ":" << seed.port << std::endl;
                close(sockfd);
                continue;
            }

            std::string regMsg = "Register:" + selfIP + ":" + std::to_string(selfPort) + "\n";
            NodeSocket newsocket(sockfd, seed.ip, seed.port);
            seedsockets.push_back(newsocket);
            send(sockfd, regMsg.c_str(), regMsg.size(), 0);
            char buffer[1024] = {0};
            int valread = read(sockfd, buffer, 1024);
            if (valread > 0)
            {
                std::cout << "Peer list from seed " << seed.ip << ":" << seed.port << ":\n"
                          << buffer << std::endl;
                writeToOutputFile("Peer list from seed " + seed.ip + ":" + std::to_string(seed.port) + buffer);
                std::istringstream iss(buffer);
                std::string peerInfo;
                while (std::getline(iss, peerInfo, ';'))
                {
                    size_t pos = peerInfo.find(":");
                    size_t second_pos = peerInfo.find(":", pos + 1);
                    if (pos == std::string::npos)
                    {
                        std::cerr << "Error: Malformed peer info '" << peerInfo << "'" << std::endl;
                        continue;
                    }

                    std::string ip = peerInfo.substr(0, pos);
                    int port = std::stoi(peerInfo.substr(pos + 1));
                    int degree = std::stoi(peerInfo.substr(second_pos + 1));
                    if (ip == selfIP && port == selfPort)
                        continue;

                    NodeInfo p{ip, port, degree};
                    addPeer(p);
                }
            }
        }
    }
    void addPeer(const NodeInfo &p)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto &peer : UnionPeerList)
        {
            if (peer.ip == p.ip && peer.port == p.port)
                return;
        }
        UnionPeerList.push_back(p);
    }
    double random_double(double min, double max)
    {
        static std::mt19937 rng(static_cast<unsigned int>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        std::uniform_real_distribution<double> dist(min, max);
        return dist(rng);
    }
    double random_double_0_1()
    {
        std::uniform_real_distribution<double> dist(1e-9, 1.0);
        return dist(rng);
    }
    void report_dead(int port, std::string ip)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            peerList.erase(std::remove_if(peerList.begin(), peerList.end(),
                                          [port](const NodeInfo &node)
                                          { return node.port == port; }),
                           peerList.end());
        }
        std::string time = timeformat();
        std::string dead_msg = "DeadNode:" + ip + ":" + std::to_string(port) + ":" + time + ":" + selfIP + ":" + std::to_string(selfPort);
        std::cout << dead_msg << std::endl;
        writeToOutputFile(dead_msg);
        broadcasttoSeeds(dead_msg + "\n");
    }

    void Liveliness()
    {
        while (true)
        {
            std::vector<NodeInfo> peersCopy;
            {
                std::lock_guard<std::mutex> lock(mtx);
                peersCopy = peerList;
            }
            for (const auto &peer : peersCopy)
            {
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0)
                {
                    std::cerr << "Socket creation error for peer " << peer.ip << std::endl;
                    continue;
                }
                struct timeval tv;
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
                {
                    perror("Error setting socket timeout");
                }

                sockaddr_in address;
                address.sin_family = AF_INET;
                address.sin_port = htons(peer.port);
                if (inet_pton(AF_INET, peer.ip.c_str(), &address.sin_addr) <= 0)
                {
                    std::cerr << "Invalid address for peer: " << peer.ip << std::endl;
                    close(sock);
                    continue;
                }
                if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
                {
                    std::cerr << "Connection failed for liveliness check to "
                              << peer.ip << ":" << peer.port << std::endl;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        Misses[peer.port]++;
                        bool shouldReport = (Misses[peer.port] >= 3);
                        int portToReport = peer.port;
                        std::string ipToReport = peer.ip;

                        lock.unlock();
                        if (shouldReport)
                            report_dead(portToReport, ipToReport);
                    }
                    close(sock);
                    continue;
                }

                std::string pingMsg = "Liveliness:Ping\n";
                if (send(sock, pingMsg.c_str(), pingMsg.size(), 0) < 0)
                {
                    std::cerr << "Error sending ping to " << peer.ip << ":" << peer.port << std::endl;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        Misses[peer.port]++;
                        bool shouldReport = (Misses[peer.port] >= 3);
                        int portToReport = peer.port;
                        std::string ipToReport = peer.ip;
                        lock.unlock();
                        if (shouldReport)
                            report_dead(portToReport, ipToReport);
                    }
                    close(sock);
                    continue;
                }

                char buffer[1024] = {0};
                int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
                bool pongReceived = false;
                if (valread > 0)
                {
                    buffer[valread] = '\0';
                    std::string response(buffer);
                    std::string expected = "Pong:" + std::to_string(peer.port);
                    if (response.find(expected) != std::string::npos)
                    {
                        pongReceived = true;
                    }
                }
                {
                    std::unique_lock<std::mutex> lock(mtx);
                    if (pongReceived)
                        Misses[peer.port] = 0;
                    else
                    {
                        Misses[peer.port]++;
                        bool shouldReport = (Misses[peer.port] >= 3);
                        int portToReport = peer.port;
                        std::string ipToReport = peer.ip;
                        lock.unlock();
                        if (shouldReport)
                            report_dead(portToReport, ipToReport);
                    }
                }
                close(sock);
            }
            sleep(13);
        }
    }

    int calculateTargetDegree(int networkSize)
    {
        const double alpha = 2.5;
        const double xmin = 3.0;
        double u = random_double(0.0, 1.0);
        int m = static_cast<int>(xmin * std::pow(1 - u, -1.0 / (alpha - 1.0)));
        return std::min(std::max(m, 2), networkSize - 1);
    }

    void PowerLaw(int m)
    {
        if (UnionPeerList.empty() || m <= 0)
            return;

        const double gamma = 2.5;
        const double epsilon = 2.0;

        std::vector<double> weights;
        weights.reserve(UnionPeerList.size());
        double totalWeight = 0.0;

        for (const auto &peer : UnionPeerList)
        {
            double w = std::pow(peer.degree + epsilon, gamma);
            weights.push_back(w);
            totalWeight += w;
        }

        m = std::min(m, static_cast<int>(UnionPeerList.size()));

        std::vector<NodeInfo> selectedPeers;
        std::vector<bool> selected(UnionPeerList.size(), false);

        for (int i = 0; i < m && totalWeight > 0; i++)
        {
            double r = random_double(0.0, totalWeight);
            double cumulative = 0.0;
            int chosenIndex = -1;

            for (size_t j = 0; j < UnionPeerList.size(); j++)
            {
                if (!selected[j])
                {
                    cumulative += weights[j];
                    if (r <= cumulative)
                    {
                        chosenIndex = j;
                        break;
                    }
                }
            }

            if (chosenIndex == -1)
                break;
            selected[chosenIndex] = true;
            selectedPeers.push_back(UnionPeerList[chosenIndex]);
            totalWeight -= weights[chosenIndex];
            weights[chosenIndex] = 0.0;
        }

        if (selectedPeers.empty() && !UnionPeerList.empty())
        {
            selectedPeers.push_back(UnionPeerList[0]);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            peerList = selectedPeers;
        }

        for (auto &i : selectedPeers)
        {
            writeToOutputFile("Connecting to " + i.ip + ":" + std::to_string(i.port));
        }
    }

    void SelectPeers()
    {
        if (UnionPeerList.empty())
            return;

        int n = UnionPeerList.size();
        int m = std::max(1, calculateTargetDegree(n));
        degree = m;
        PowerLaw(m);
    }
    void handleClient(int peer_socket)
    {
        std::string dataBuffer;
        char buffer[1024];

        while (true)
        {
            int valread = read(peer_socket, buffer, sizeof(buffer) - 1);
            if (valread <= 0)
            {
                std::cerr << "Peer socket " << peer_socket << " disconnected or error occurred." << std::endl;
                break;
            }
            buffer[valread] = '\0';
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
                if (tokens.size() >= 2)
                {
                    if (tokens[0] == "Register")
                    {
                        NodeInfo newNode;
                        newNode.ip = tokens[1];
                        newNode.port = std::stoi(tokens[2]);
                        newNode.degree = std::stoi(tokens[3]);
                        std::lock_guard<std::mutex> lock(mtx);
                        peerList.push_back(newNode);
                        NodeSocket newsocket(peer_socket, tokens[1], std::stoi(tokens[2]));
                        peersockets.push_back(newsocket);
                    }
                    else if (tokens[0] == "UpdateDegree")
                    {
                        degree++;
                        broadcasttoSeeds(msg + "\n");
                    }
                    else if (tokens[0] == "Liveliness")
                    {
                        std::string pongMsg = "Pong:" + std::to_string(selfPort) + "\n";
                        send(peer_socket, pongMsg.c_str(), pongMsg.size(), 0);
                        close(peer_socket);
                        return;
                    }
                    else
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        if (MessageList.find(tokens[3]) == MessageList.end())
                        {
                            std::cout << "Received message from "
                                      << tokens[1] << ":" << tokens[2]
                                      << " " << tokens[3] << std::endl;
                            MessageList[tokens[3]] = tokens[1];
                            lock.unlock();
                            std::string send_msg = timeformat() + ":" + tokens[1] + ":" + tokens[2] + ":" + tokens[3];
                            writeToOutputFile(send_msg);
                            broadcastMessage(send_msg + "\n");
                        }
                        else
                        {
                            std::cout << "Message Already Exists. Not Forwarding: " << tokens[3] << std::endl;
                        }
                    }
                }
            }
        }
        close(peer_socket);
    }

    void connectToPeers()
    {
        for (auto &p : peerList)
        {
            std::cout << "Connecting to peer: " << p.ip << ":" << p.port << std::endl;
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0)
            {
                perror("Socket creation error");
                continue;
            }
            struct sockaddr_in peerAddr;
            peerAddr.sin_family = AF_INET;
            peerAddr.sin_port = htons(p.port);
            if (inet_pton(AF_INET, p.ip.c_str(), &peerAddr.sin_addr) <= 0)
            {
                std::cerr << "Invalid peer address: " << p.ip << std::endl;
                close(sockfd);
                continue;
            }
            if (connect(sockfd, (struct sockaddr *)&peerAddr, sizeof(peerAddr)) < 0)
            {
                std::cerr << "Connection failed to peer: " << p.ip << ":" << p.port << std::endl;
                close(sockfd);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mtx);
                NodeSocket newsocket(sockfd, p.ip, p.port);
                peersockets.push_back(newsocket);
            }
            std::string new_conn = "Register:" + selfIP + ":" + std::to_string(selfPort) + ":" + std::to_string(degree) + "\n";
            std::string deg_inc_msg = "UpdateDegree:" + selfIP + ":" + std::to_string(selfPort) + "\n";
            std::string deg_inc_msg2 = "UpdateDegree:" + p.ip + ":" + std::to_string(p.port) + "\n";
            send(sockfd, new_conn.c_str(), new_conn.size(), 0);
            send(sockfd, deg_inc_msg2.c_str(), deg_inc_msg2.size(), 0);
            broadcasttoSeeds(deg_inc_msg);
            std::thread(&Peer::handleClient, this, sockfd).detach();
        }
    }
    void PeerAsServer()
    {
        peer_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (peer_fd < 0)
        {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        if (setsockopt(peer_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            perror("setsockopt failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(selfIP.c_str());
        address.sin_port = htons(selfPort);

        if (bind(peer_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("Bind failed");
            exit(EXIT_FAILURE);
        }

        if (listen(peer_fd, 10) < 0)
        {
            perror("Listen failed");
            exit(EXIT_FAILURE);
        }
        while (true)
        {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            int client_socket = accept(peer_fd, (struct sockaddr *)&clientAddr, &addrLen);
            if (client_socket < 0)
            {
                perror("Accept failed");
                continue;
            }
            std::thread(&Peer::handleClient, this, client_socket).detach();
        }
        close(peer_fd);
    }
    std::string generateGossipMessage()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 99999999);
        std::string randomMsg = "Msg" + std::to_string(dis(gen));
        return randomMsg;
    }

    void startGossip()
    {
        for (int i = 1; i <= 10; i++)
        {
            std::string msg = generateGossipMessage();
            MessageList[msg] = selfIP + ':' + std::to_string(selfPort);
            std::string formattedmsg = FormateGossipMessage(msg);
            writeToOutputFile(formattedmsg);
            broadcastMessage(formattedmsg + "\n");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    std::string timeformat()
    {
        std::time_t now = std::time(nullptr);
        std::tm *ltm = std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(ltm, "%d-%m-%Y %H-%M-%S");
        return oss.str();
    }
    std::string FormateGossipMessage(std::string msg)
    {
        std::string time = timeformat();
        return time + ":" + selfIP + ":" + std::to_string(selfPort) + ":" + msg;
    }

    void broadcastMessage(const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto it = peersockets.begin(); it != peersockets.end();)
        {
            int bytesSent = send(it->sockfd, msg.c_str(), msg.size(), 0);
            if (bytesSent < 0)
            {
                perror("Error sending message on broadcast");
                close(it->sockfd);
                it = peersockets.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    void broadcasttoSeeds(const std::string msg)
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto &s : seedsockets)
        {
            int bytesSent = send(s.sockfd, msg.c_str(), msg.size(), 0);
            if (bytesSent < 0)
            {
                perror("Error sending message");
            }
        }
    }
};

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        std::cout << "Usage: ./peer <selfIP> <selfPort> <config.txt>" << std::endl;
        return 1;
    }
    std::string selfIP = argv[1];
    int selfPort = std::stoi(argv[2]);
    std::string configFile = argv[3];
    Peer peer(selfIP, selfPort);
    peer.loadConfig(configFile);
    peer.registerWithSeeds();
    peer.SelectPeers();
    peer.connectToPeers();
    std::thread serverThread(&Peer::PeerAsServer, &peer);
    std::thread gossipThread(&Peer::startGossip, &peer);
    std::thread LivelinessThread(&Peer::Liveliness, &peer);
    serverThread.join();
    gossipThread.join();
    LivelinessThread.join();
    return 0;
}
