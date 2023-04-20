idf_component_register(SRCS "main.c" "server.c" "ws.c" "ota.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES howsmyssl_com_root_cert.pem
                    EMBED_FILES upload.html manifest.json index.html bundle.js
                    postman_root_cert.pem)
