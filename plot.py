import socket
import random
import matplotlib.pyplot as plt
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
import struct
import time

class Peer:
    def __init__(self, self_ip, self_port):
        self.self_ip = self_ip
        self.self_port = self_port
        self.total_seed_list = []
        self.seed_list = []
        self.union_peer_list = []
        self.seed_responses = []
        self.socket_timeout = 2
        self.max_wait_time = 3
        self.buffer_size = 16384

    def receive_fast(self, sock):
        try:
            start_time = time.time()
            data = bytearray()
            
            while time.time() - start_time < self.socket_timeout:
                try:
                    chunk = sock.recv(self.buffer_size)
                    if not chunk:
                        break
                    data.extend(chunk)
                    if b'\n' in chunk: 
                        break
                except socket.timeout:
                    break
                    
            return data.decode(errors='ignore')
        except Exception as e:
            return ""

    def connect_to_seed(self, seed_info):
        seed_ip, seed_port = seed_info
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.socket_timeout)
            sock.connect((seed_ip, seed_port))
            
            reg_msg = f"Register:{self.self_ip}:{self.self_port}\n"
            sock.sendall(reg_msg.encode())
            
            response = self.receive_fast(sock)
            sock.close()
            
            if response:
                self.seed_responses.append((seed_ip, seed_port, response))
                self.process_peer_info(response)
            return True
        except Exception:
            return False
        finally:
            try:
                sock.close()
            except:
                pass

    def process_peer_info(self, response):
        for peer_info in response.strip().split(";"):
            if not peer_info or ":" not in peer_info:
                continue
            try:
                parts = peer_info.split(":")
                if len(parts) >= 3:
                    ip, port, degree = parts[0], int(parts[1]), int(parts[2])
                    if ip != self.self_ip or port != self.self_port:
                        self.add_peer(ip, port, degree)
            except:
                continue

    def register_with_seeds(self):
        n = len(self.total_seed_list)
        required = (n // 2) + 1
        random.shuffle(self.total_seed_list)
        selected_seeds = self.total_seed_list[:required]
        
        with ThreadPoolExecutor(max_workers=required) as executor:
            futures = {executor.submit(self.connect_to_seed, seed): seed 
                      for seed in selected_seeds}
            
            done, _ = wait(
                futures.keys(),
                timeout=self.max_wait_time,
                return_when=FIRST_COMPLETED
            )
            
            for future in done:
                if future.result():
                    self.seed_list.append(futures[future])

    def load_config(self, filename):
        try:
            with open(filename, "r") as infile:
                self.total_seed_list = [
                    (line.split(":")[0], int(line.split(":")[1]))
                    for line in infile
                    if ":" in line
                ]
        except Exception as e:
            print(f"Config loading error: {e}")
            sys.exit(1)

    def add_peer(self, ip, port, degree):
        if not any(p[0] == ip and p[1] == port for p in self.union_peer_list):
            self.union_peer_list.append((ip, port, degree))

    def plot_graph(self):
        if not self.union_peer_list:
            return
            
        degree_count = {}
        for _, _, degree in self.union_peer_list:
            degree_count[degree] = degree_count.get(degree, 0) + 1
            
        plt.figure(figsize=(8, 6))
        plt.scatter(
            list(degree_count.values()),
            list(degree_count.keys()),
            label="Peers vs Degree"
        )
        plt.xlabel("Number of Peers")
        plt.ylabel("Degree")
        plt.title("Peer Connectivity Distribution")
        plt.grid(True, linestyle="--", alpha=0.6)
        plt.legend()
        plt.show()

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 4:
        print("Usage: python peer.py <selfIP> <selfPort> <config.txt>")
        sys.exit(1)

    start_time = time.time()
    peer = Peer(sys.argv[1], int(sys.argv[2]))
    peer.load_config(sys.argv[3])
    peer.register_with_seeds()
    peer.plot_graph()