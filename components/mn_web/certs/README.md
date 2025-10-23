## 🔐 Generating HTTPS Certificates

Certificates and private keys are **not included** in the Git repository  
(to prevent key leakage).  

> 📍 All paths assume you are in `components/mn_web/certs`.

### 1️⃣ Generate an ECDSA private key (P-256)
```bash
openssl ecparam -genkey -name prime256v1 -noout -out key.pem
```

### 2️⃣ Create a self-signed certificate
```bash
openssl req -new -x509 -key key.pem -out cert.pem -days 365 -subj "/CN=MiNOS"
```

### 3️⃣ Embed the certificates in the firmware
The `mn_web` component’s CMake configuration automatically embeds them:

```cmake
idf_component_register(
    SRCS "MnWeb.cpp"
    EMBED_TXTFILES "certs/cert.pem" "certs/key.pem"
)
```