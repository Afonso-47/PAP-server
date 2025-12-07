import socket
import sys
import os

BUFFER_SIZE = 4096
UNLOCK_SIGNAL = b"\x01"
MODE_DOWNLOAD = b"D"
MODE_UPLOAD = b"U"

def create_socket(host, port):
    return socket.create_connection((host, port), timeout=10)

def recv_exact(sock, length):
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise ConnectionError("Connection closed while receiving data")
        data.extend(chunk)
    return bytes(data)

def send_unlock(sock):
    sock.sendall(UNLOCK_SIGNAL)

def send_mode(sock, mode_byte):
    sock.sendall(mode_byte)

def receive_file(sock, output_dir="."):
    name_len_bytes = recv_exact(sock, 4)
    name_len = int.from_bytes(name_len_bytes, byteorder='big')
    if name_len <= 0 or name_len > 4096:
        raise ValueError("Invalid filename length from server")

    name_bytes = recv_exact(sock, name_len)
    filename = name_bytes.decode('utf-8')

    os.makedirs(output_dir, exist_ok=True)
    out_path = os.path.join(output_dir, filename)

    with open(out_path, 'wb') as f:
        while True:
            chunk = sock.recv(BUFFER_SIZE)
            if not chunk:
                break
            f.write(chunk)

    return out_path

def upload_file(sock, filepath, target_path=None):
    if not os.path.isfile(filepath):
        raise FileNotFoundError(filepath)

    if target_path is None:
        target_path = os.path.basename(filepath)

    name_bytes = target_path.encode('utf-8')
    name_len = len(name_bytes)
    if name_len == 0 or name_len > 4096:
        raise ValueError("Target path length invalid")

    sock.sendall(name_len.to_bytes(4, byteorder='big'))
    sock.sendall(name_bytes)

    with open(filepath, 'rb') as f:
        while True:
            chunk = f.read(BUFFER_SIZE)
            if not chunk:
                break
            sock.sendall(chunk)

if __name__ == '__main__':
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print('Usage: python send_file_socket.py <host> <port> [output_dir]')
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    output_dir = sys.argv[3] if len(sys.argv) == 4 else "."

    with create_socket(host, port) as sock:
        send_unlock(sock)
        saved = receive_file(sock, output_dir)
        print(f"Received file saved to: {saved}")