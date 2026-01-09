#!/usr/bin/env python3
"""
PAP Client - Simple TUI for file transfer
"""
import os
import sys
import platform
# UI depends on the single functions module
from send_file_socket import download_from_server, upload_to_server, list_directory, get_default_download_dir


def clear_screen():
    os.system('clear' if os.name != 'nt' else 'cls')


def print_header():
    print("=" * 60)
    print("  PAP File Transfer Client".center(60))
    print("=" * 60)
    print()


def get_input(prompt, default=None):
    if default:
        user_input = input(f"{prompt} [{default}]: ").strip()
        return user_input if user_input else default
    return input(f"{prompt}: ").strip()


def main_menu():
    clear_screen()
    print_header()
    print("1. Download file from server")
    print("2. Upload file to server")
    print("3. List directory on server")
    print("4. Exit")
    print()
    return get_input("Select option (1-4)")


def download_menu():
    clear_screen()
    print_header()
    print("DOWNLOAD FILE FROM SERVER")
    print("-" * 60)
    print()
    
    host = get_input("Server IP/hostname", "10.0.0.1")
    port = get_input("Server port", "9001")
    username = get_input("Username (for ~ expansion)", "root")
    remote_path = get_input("Remote file path (on server)")
    
    # On Windows, default to Downloads; on other systems allow customization
    if platform.system() == "Windows":
        output_dir = get_default_download_dir()
        print(f"\n(On Windows, files are saved to: {output_dir})")
    else:
        output_dir = get_input("Local save directory", ".")
    
    if not remote_path:
        print("\nError: Remote path is required!")
        input("Press Enter to continue...")
        return
    
    try:
        port = int(port)
        print(f"\nConnecting to {host}:{port} as {username}...")
        print(f"Requesting: {remote_path}")
        
        saved_path = download_from_server(host, port, username, remote_path, output_dir)
        
        print(f"\n✓ Success! File saved to: {saved_path}")
    except FileNotFoundError as e:
        print(f"\n✗ Error: File not found - {e}")
    except ConnectionError as e:
        print(f"\n✗ Connection error: {e}")
    except ValueError as e:
        print(f"\n✗ Invalid response from server: {e}")
    except Exception as e:
        print(f"\n✗ Error: {e}")
    
    input("\nPress Enter to continue...")


def upload_menu():
    clear_screen()
    print_header()
    print("UPLOAD FILE TO SERVER")
    print("-" * 60)
    print()
    
    host = get_input("Server IP/hostname", "10.0.0.1")
    port = get_input("Server port", "9001")
    username = get_input("Username (for ~ expansion)", "root")
    local_file = get_input("Local file path")
    
    if not local_file:
        print("\nError: Local file path is required!")
        input("Press Enter to continue...")
        return
    
    if not os.path.isfile(local_file):
        print(f"\n✗ Error: File not found: {local_file}")
        input("Press Enter to continue...")
        return
    
    basename = os.path.basename(local_file)
    remote_target = get_input("Remote target path (with directories)", basename)
    
    try:
        port = int(port)
        print(f"\nConnecting to {host}:{port} as {username}...")
        print(f"Uploading: {local_file}")
        print(f"Target: {remote_target}")
        
        upload_to_server(host, port, username, local_file, remote_target)
        
        print(f"\n✓ Success! File uploaded to: {remote_target}")
    except FileNotFoundError as e:
        print(f"\n✗ Error: File not found - {e}")
    except ConnectionError as e:
        print(f"\n✗ Connection error: {e}")
    except Exception as e:
        print(f"\n✗ Error: {e}")
    
    input("\nPress Enter to continue...")


def list_menu():
    clear_screen()
    print_header()
    print("LIST DIRECTORY ON SERVER")
    print("-" * 60)
    print()
    
    host = get_input("Server IP/hostname", "10.0.0.1")
    port = get_input("Server port", "9001")
    username = get_input("Username (for ~ expansion)", "root")
    remote_path = get_input("Remote directory path (on server)", ".")
    
    try:
        port = int(port)
        print(f"\nConnecting to {host}:{port} as {username}...")
        print(f"Listing: {remote_path}\n")
        
        output = list_directory(host, port, username, remote_path)
        
        print(output)
    except ConnectionError as e:
        print(f"\n✗ Connection error: {e}")
    except RuntimeError as e:
        print(f"\n✗ Error: {e}")
    except Exception as e:
        print(f"\n✗ Error: {e}")
    
    input("\nPress Enter to continue...")


def main():
    while True:
        choice = main_menu()
        
        if choice == "1":
            download_menu()
        elif choice == "2":
            upload_menu()
        elif choice == "3":
            list_menu()
        elif choice == "4":
            clear_screen()
            print("Goodbye!")
            sys.exit(0)
        else:
            print("\nInvalid option. Please select 1-4.")
            input("Press Enter to continue...")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        clear_screen()
        print("\nInterrupted by user. Goodbye!")
        sys.exit(0)
