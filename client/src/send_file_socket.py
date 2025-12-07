import socket
import sys
import os

def create_socket(host, port):
    return socket.create_connection((host, port), timeout=10)

def send_path_and_name(sock, filename, target_path=None):
    if target_path is None:
        target_path = "/root/" + os.path.basename(filename)

    # Send the length of the filename first (4 bytes, big-endian)
    filename_bytes = target_path.encode('utf-8')
    filename_length = len(filename_bytes)
    sock.sendall(filename_length.to_bytes(4, byteorder='big'))

    # Send the actual filename
    sock.sendall(filename_bytes)

def send_file(sock, filename):
    # Send unlock signal byte 0x01
    sock.sendall(b'\x01')

        # Send file in chunks
    with open(filename, 'rb') as f:
        while True:
            chunk = f.read(4096)
            if not chunk:
                break
            sock.sendall(chunk)
    # Close socket (server will detect EOF)
    print('File sent.')

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print('Usage: python send_file_socket.py <host> <port> <file>')
        sys.exit(1)
    host = sys.argv[1]
    port = int(sys.argv[2])
    filename = sys.argv[3]
    if not os.path.isfile(filename):
        print('File not found:', filename); sys.exit(1)
    
    with create_socket(host, port) as sock:
        send_path_and_name(sock, filename)
        send_file(sock, filename)