## 🔐 Generating HTTPS Certificates

Certificates and private keys are NOT included in the Git repository  
(to prevent accidental key leakage).

All commands assume you are in:

components/mn_web/certs

------------------------------------------------------------

## 📄 Configuration Templates

This directory contains example configuration files:

ca.cnf.default  
server.cnf.defaults  
server_ext.cnf.defaults  

These are **templates only**.

Before generating certificates, rename them:

cp ca.cnf.default ca.cnf  
cp server.cnf.defaults server.cnf  
cp server_ext.cnf.defaults server_ext.cnf  

Edit them to match your environment.

------------------------------------------------------------

## ✅ Why This Method is Required

Modern browsers (including Chrome):

✔ Ignore the Common Name (CN)  
✔ Require Subject Alternative Name (SAN)  
✔ Reject self-signed leaf certificates  

Without a trusted CA you may see errors like:

mbedtls_ssl_handshake returned -0x7780

Correct approach:

✔ Create a local Certificate Authority (CA)  
✔ Trust the CA on your computer  
✔ Sign the ESP32 server certificate with this CA  

------------------------------------------------------------

# 1️⃣ Generate Local Certificate Authority (One-Time Setup)

Generate CA private key:

openssl ecparam -genkey -name prime256v1 -noout -out ca.key

Generate CA certificate:

openssl req -new -x509 \
    -key ca.key \
    -out ca.crt \
    -days 3650 \
    -config ca.cnf

------------------------------------------------------------

# 2️⃣ Generate Server Private Key

openssl ecparam -genkey -name prime256v1 -noout -out server.key

------------------------------------------------------------

# 3️⃣ Generate Certificate Signing Request (CSR)

openssl req -new \
    -key server.key \
    -out server.csr \
    -config server.cnf

------------------------------------------------------------

# 4️⃣ Sign Server Certificate With CA

openssl x509 -req \
    -in server.csr \
    -CA ca.crt \
    -CAkey ca.key \
    -CAcreateserial \
    -out server.crt \
    -days 825 \
    -sha256 \
    -extfile server_ext.cnf

------------------------------------------------------------

## ✅ SAN Rules (Very Important)

Your SAN entries MUST match exactly how you access the device.

Examples:

https://minos.local     → DNS SAN required  
https://192.168.4.1     → IP SAN required  

✔ Wildcards for IP are NOT supported  
✔ Missing SAN = browser rejection

------------------------------------------------------------

# 5️⃣ Trust Your Local CA (Required for Chrome)

Import:

ca.crt

NOT the server certificate.

Windows → Trusted Root Certification Authorities  
macOS   → Keychain Access → Always Trust  
Linux   → System store / NSS DB (depends on distro)

------------------------------------------------------------

# 6️⃣ Embed Certificates in Firmware

idf_component_register(
    SRCS "MnWeb.cpp"
    EMBED_TXTFILES
        "certs/server.crt"
        "certs/server.key"
)

------------------------------------------------------------

# ⚠️ Common Errors & Causes

mbedtls_ssl_handshake returned -0x7780

Usually caused by:

✔ Unknown CA  
✔ Missing SAN  
✔ Cipher suite mismatch  
✔ Incorrect mbedTLS configuration  

------------------------------------------------------------

# 🔒 Security Reminder

This setup provides:

✔ Proper encryption  
✔ Trusted identity (via CA)  
✔ No browser warnings  

The CA private key:

ca.key

MUST remain secret.
