idf_component_register(
    SRCS "main.c" "server.c" "ws.c" "ota.c" "const.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES howsmyssl_com_root_cert.pem
    EMBED_FILES ui/upload.html manifest.json index.html bundle.js sw.js
    postman_root_cert.pem
)
