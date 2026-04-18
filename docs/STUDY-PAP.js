<file> STUDY-PAP.js </file>


class session_c = { 

    //    Session handler for PAP server bidirectional file transfer
    //    This module implements authenticated file transfer sessions
    
    class supporting =
    {
        const "authentication via username"
        const "Download mode     (server -> client)"
        const "Upload mode       (client -> server)"
        const "Directory listing mode"
        const "Tilde (~) path expansion using system user database"
        const "Automatic parent directory creation for uploads"
        const "Status byte reporting ( 0x00=OK, 0x01=ERROR )"
    }
    
    class application_flow = 
    {
        // TOP-most level
        
        function handle_unlocked_session(client_file_descriptor)
        {
            // --- Step 1 ---
            // Recieve and store username for path expansion
            // . . .
            
            // Receive a length-prefixed string (path/username) from socket
            function recv_path_alloc(client_file_descriptor)
            {
                var len_be; // (uint32_t) Read the first 4 bytes as an unsigned integer in order and store them as the length of the string (be = big endian)
                
                // Recieve an exact number of bytes from a socket
                function recv_exact(socket_file_descriptor, data_buffer, number_of_bytes)
                {
                    var total = 0;  // store number of bytes recieved
                    var p = data_buffer; // cast the buffer to a byte pointer to allow pointer arithmetic
                    while (total < number_of_bytes) // while not all bytes have been read
                    {
                        // attempt to recieve up to the remaining number of bytes
                        var n = recv(
                            socket_file_descriptor,     // socket to recieve from
                            p + total,                  // write new data starting at offset 'total' inside the buffer
                            number_of_bytes - total,    // request only the remaining number of bytes needed
                            0                           // no flags
                        )
                        if (n <= 0) return n // Error or connection closed
                        total += n // Move the "cursor" to the next batch
                    }
                    
                    return total; // return the number of bytes recieved (expected to be equal to the requested size)
                }
                
                // Recieve the length of the string to recieve (first four Recieve and handle client commands (download, upload, list)bytes)
                var n = recv_exact(client_file_descriptor, addressof(len_be), sizeof(len_be))
                if (n <= 0) return NULL; // Error or connection closed
                
                // Convert from network byte order (big endian) to host byte order
                var len = ntohl(len_be); // length big endian to length small endian
                if (len == 0 || len > 4096) return NULL // sanity check
                
                // Allocate the necessary space for the recieved path
                var path = malloc(len + 1) // +1 for null terminator
                if (!path) return NULL // failure to allocate results in a crash, as it should
                
                // Recieve the path
                n = recv_exact(client_file_descriptor, path, len)
                if (n <= 0) { free(path); return NULL }
                path[len] = '\0' // null terminate it
                return path
            }
            
            // Recieve the username string, already in allocated memory
            var username = recv_path_alloc(client_file_descriptor)
            if (!username)
            {
                printf("Invalid or missing username.\n") // Print the errors on the server's real-time log (not automatically saved in anywhere)
                return -1
            }
            
            // Store username globally for expand_tilde() to use
            strncpy(current_username, username, sizeof(current_username) - 1)
            current_username[sizeof(current_username) - 1] = '\0' // Ensure null termination
            printf("Authenticated as user: %s\n", current_username) // Print the username on the server's real-time log (not automatically saved in anywhere)
            free(username)

            // --- Step 2 ---
            // Authenticate the password hash against system shadow entry
            // . . .

            // Authenticate current user with password hash response
            function authenticate_user(client_file_descriptor)
            {
                // get the current user details from /etc/shadow
                var shadow_password_entry = getspnam(current_username)

                // Check if the shadow password entry exists and is valid (not empty, not locked)
                if (!shadow_password_entry || !shadow_password_entry->sp_pwdp || shadow_password_entry->sp_pwdp[0] == '\0'
                    shadow_password_entry->sp_pwdp[0] == '!' || shadow_password_entry->sp_pwdp[0] == '*')
                {
                    var status = STATUS_ERROR
                    send_all(client_file_descriptor, dereference(status), 1)
                    return -1
                }

                char setting[512];

                // Extract crypt setting (algorithm+salt) from shadow hash
                function extract_crypt_settings(stored_hash, output_buffer, buffer_size)
                {
                    // make sure the recieved stored hash was valid
                    if (!stored_hash || stored_hash[0] == '\0') return -1

                    // Modern modular format: $id$[param$]salt$hash
                    if (stored_hash[0] == '$') {
                        // get a pointer to the last dollarsign character in the hash
                        const ptr_last_dollar = strrchr(stored_hash, '$');
                        if (!last_dollar || last_dollar == stored_hash) return -1

                        var setting_len = last_dollar - stored_hash + 1 // include trailing dollarsign
                        if (setting_len >= buffer_size) return -1
                        memcpy(output_buffer, stored_hash, setting_len) // copy only the setting to the output buffer
                        out[setting_len] = '\0';
                        return 0;
                    }
                }

                // Extract the cryptographic settings (salt, algorithm identifier) from the shadow password entry and send them to the client for hashing the password
                if (extract_crypt_settings(shadow_password_entry->sp_pwdp, setting, sizeof(setting)) != 0) {
                    var status = STATUS_ERROR
                    send_all(client_file_descriptor, dereference(status), 1)
                    return -1
                }

                // Send a length-prefixed string to socket (4-byte (Big Endian) length + bytes)
                function send_path_raw(file_descriptor, string)
                {
                    var len = strlen(string)
                    if (len == 0 || len > 4096) return -1

                    var len_be = htonl(len) // small endian -> big endian
                    if (send_all(file_descriptor, dereference(len_be), sizeof(len_be)) <= 0) return -1;
                    return 0;
                }

                // Send the settings to the client
                if (send_path_raw(client_file_descriptor, setting) != 0) {
                    return -1
                }

                var client_hash = recv_path_alloc(client_file_descriptor)
                if (!client_hash) {
                    var status = STATUS_ERROR
                    send_all(client_file_descriptor, dereference(status), 1)
                    return -1
                }

                /* Use constant-time comparison to prevent timing side-channel attacks.
                 * strcmp() short-circuits on the first mismatched byte, which leaks
                 * information about how many leading characters are correct */
                var stored_len = strlen(sp->sp_pwdp)
                var client_len = strlen(client_hash)
                var ok = 0
                if (stored_len == client_len) {
                    var diff = 0
                    for (var i = 0; i < stored_len; i++) {
                        diff |= client_hash[i] ^ sp->sp_pwdp[i]
                    }
                    ok = (diff == 0)
                }
                free(client_hash)

                var status = ok ? STATUS_OK : STATUS_ERROR
                if (send_all(client_file_descriptor, dereference(status), 1) <= 0) {
                    return -1
                }

                return ok ? 0 : -1
            }

            if (authenticate_user(client_file_descriptor) != 0)
            {
                printf("Authentication failed for user: %s\n", current_username); // Print the username on the server's real-time log (not automatically saved in anywhere)
                return -1;
            }

            // --- Step 3 ---
            // Receive mode byte to determine operation
            // . . .
            var mode
            var n = recv_exact(client_file_descriptor, dereference(mode), 1)
            if (n <= 0) {
                perror("recv mode")
                return -1
            }

            // --- Step 4 ---
            // Dispatch to appropriate handler
            // . . .
            if (mode == MODE_DOWNLOAD) {

                function handle_download (client_file_descriptor)
                {

                }

                return handle_download(client_fd);
            } else if (mode == MODE_UPLOAD) {

                function handle_upload (client_file_descriptor)
                {
                    
                }

                return handle_upload(client_fd);
            } else if (mode == MODE_LIST) {

                function handle_list (client_file_descriptor)
                {
                    
                }

                return handle_list(client_fd);
            }

            printf("Unknown mode byte: 0x%02x\n", mode)
            return -1
        }
    }
    
}