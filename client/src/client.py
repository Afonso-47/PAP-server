import sys

from send_file_socket import (
    create_socket,
    send_unlock,
    send_mode,
    send_path,
    receive_file,
    upload_file,
    download_file,
    MODE_DOWNLOAD,
    MODE_UPLOAD,
)


def main():
    if len(sys.argv) < 6:
        print("Usage: python client.py <host> <port> <username> <download|upload> <path> [extra]")
        print("  download: <remote_path> [output_dir]")
        print("  upload:   <local_file> [remote_target_path]")
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    username = sys.argv[3]
    mode = sys.argv[4].lower()

    try:
        sock = create_socket(host, port)
        send_unlock(sock)
        send_path(sock, username)

        if mode == "download":
            remote_path = sys.argv[5]
            output_dir = sys.argv[6] if len(sys.argv) >= 7 else "."
            send_mode(sock, MODE_DOWNLOAD)
            saved_path = download_file(sock, remote_path, output_dir)
            print(f"File received and saved to {saved_path}")
        elif mode == "upload":
            local_file = sys.argv[5]
            remote_target = sys.argv[6] if len(sys.argv) >= 7 else None
            send_mode(sock, MODE_UPLOAD)
            upload_file(sock, local_file, remote_target)
            print("File uploaded successfully")
        else:
            print("Mode must be 'download' or 'upload'")
            sys.exit(1)
    except Exception as e:
        print("An error occurred:", e)
        sys.exit(1)
    finally:
        sock.close()
        print("Transfer process finished.")


if __name__ == "__main__":
    main()
