idf_component_register(SRCS "esp_http_client_example.c" "server.c" "ws.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES howsmyssl_com_root_cert.pem
                    EMBED_FILES root.html
                    postman_root_cert.pem)
