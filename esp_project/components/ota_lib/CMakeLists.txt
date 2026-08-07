idf_component_register(
    SRCS "ota_lib.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_http_client app_update esp_https_ota mbedtls log_lib
    # Embed the server root certificate into the final binary
    #EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
)
