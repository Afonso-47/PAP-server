import os
import sys

from send_file_socket import *

def main():
    hostname = "10.0.0.1"
    port = 9001

    file_to_send = "./testfile.txt"

    if not os.path.isfile(file_to_send):
        print('File not found:', file_to_send); sys.exit(1)

    try:
        # create socket
        sock = create_socket(hostname, port)

        # send the file path and name before sending the file
        send_path_and_name(sock, file_to_send)

        # send the file
        send_file(sock, file_to_send)

        print("File transfer completed successfully.")

    except Exception as e:
        print("An error occurred:", e)
        sys.exit(1)
    
    finally:
        sock.close()
        print("Transfer process finished.")

if __name__ == "__main__":
    main()
