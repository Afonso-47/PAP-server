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


def send_path(sock, path):
    path_bytes = path.encode('utf-8')
    if len(path_bytes) == 0 or len(path_bytes) > 4096:
        raise ValueError("Path length invalid")
    sock.sendall(len(path_bytes).to_bytes(4, byteorder='big'))
    sock.sendall(path_bytes)


def receive_file(sock, output_dir="."):
    # Check status byte
    status_byte = recv_exact(sock, 1)
    if status_byte[0] != 0x00:
        raise RuntimeError("Server reported error - file may not exist or is not accessible")
    
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

    send_path(sock, target_path)
    
    # Check status byte
    status_byte = recv_exact(sock, 1)
    if status_byte[0] != 0x00:
        raise RuntimeError("Server reported error - cannot write to target path")

    with open(filepath, 'rb') as f:
        while True:
            chunk = f.read(BUFFER_SIZE)
            if not chunk:
                break
            sock.sendall(chunk)


def download_file(sock, remote_path, output_dir="."):
    send_path(sock, remote_path)
    return receive_file(sock, output_dir)


# High-level API functions
def download_from_server(host, port, username, remote_path, output_dir="."):
    """Download a file from the server.
    
    Args:
        host: Server hostname or IP
        port: Server port
        username: Username for tilde expansion on server
        remote_path: Path to file on server
        output_dir: Local directory to save file (default: current dir)
    
    Returns:
        Path to saved file
    """
    sock = create_socket(host, port)
    try:
        send_unlock(sock)
        send_path(sock, username)
        send_mode(sock, MODE_DOWNLOAD)
        return download_file(sock, remote_path, output_dir)
    finally:
        sock.close()


def upload_to_server(host, port, username, local_file, remote_target=None):
    """Upload a file to the server.
    
    Args:
        host: Server hostname or IP
        port: Server port
        username: Username for tilde expansion on server
        local_file: Path to local file to upload
        remote_target: Target path on server (default: basename of local file)
    
    Returns:
        None
    """
    sock = create_socket(host, port)
    try:
        send_unlock(sock)
        send_path(sock, username)
        send_mode(sock, MODE_UPLOAD)
        upload_file(sock, local_file, remote_target)
    finally:
        sock.close()


if __name__ == '__main__':
    print('Use main.py for TUI or client.py for CLI usage.')
