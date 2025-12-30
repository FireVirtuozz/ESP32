idf_component_register(
    SRCS "otaLib.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client app_update esp_https_ota mbedtls mqttLib
    # Embed the server root certificate into the final binary
    #EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
)
