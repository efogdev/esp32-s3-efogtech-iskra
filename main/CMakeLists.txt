idf_component_register(
    SRCS "main.c" "server.c" "ws.c" "ota.c" "const.c"
    INCLUDE_DIRS "."
    EMBED_FILES ui/upload.html manifest.json index.html bundle.js sw.js
)
