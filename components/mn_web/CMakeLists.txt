# This file is part of MiNOS (MiNuS OS).
# Copyright (c) 2025 TheMiNuS
# Licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0).
# See LICENSE or https://creativecommons.org/licenses/by-nc/4.0/
#
#Generate your certs
# cd components/mn_web
# mkdir -p certs
# openssl req -x509 -newkey rsa:2048 -keyout certs/key.pem -out certs/cert.pem -days 3650 -nodes -subj "/CN=MiNOS"

# Plus léger pour la RAM
#cd components/mn_web/certs
#openssl ecparam -genkey -name prime256v1 -noout -out key.pem
#openssl req -new -x509 -key key.pem -out cert.pem -days 3650 -subj "/CN=MiNOS"

idf_component_register(SRCS "MnWeb.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_server esp_https_server mbedtls mn_cfg mn_wifi mn_time mn_ota esp_system esp_wifi examples_demo
                       EMBED_TXTFILES "certs/cert.pem""certs/key.pem"
)
