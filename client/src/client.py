import sys

from send_file_socket import (
    create_socket,
    send_unlock,
    send_mode,
    receive_file,
    upload_file,
    MODE_DOWNLOAD,
    MODE_UPLOAD,
)

def main():
    if len(sys.argv) < 4:
        print("Usage: python client.py <host> <port> <mode: download|upload> [path]")
        print("  download: optional [output_dir], default '.'")
        print("  upload:   required [file_to_send], optional target name")
        sys.exit(1)

    host = sys.argv[1]
    port = int(sys.argv[2])
    mode = sys.argv[3].lower()

    try:
        sock = create_socket(host, port)
        send_unlock(sock)

        if mode == "download":
            output_dir = sys.argv[4] if len(sys.argv) >= 5 else "."
            send_mode(sock, MODE_DOWNLOAD)
            saved_path = receive_file(sock, output_dir)
            print(f"File received and saved to {saved_path}")
        elif mode == "upload":
            if len(sys.argv) < 5:
                print("Upload mode requires a file path to send")
                sys.exit(1)
            filepath = sys.argv[4]
            target_name = sys.argv[5] if len(sys.argv) >= 6 else None
            send_mode(sock, MODE_UPLOAD)
            upload_file(sock, filepath, target_name)
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
