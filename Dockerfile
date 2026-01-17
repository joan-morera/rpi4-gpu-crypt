FROM debian:sid-slim

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies + System OpenSSL
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libvulkan-dev \
    vulkan-tools \
    glslang-tools \
    libssl-dev \
    openssl \
    mesa-vulkan-drivers \
    bsdextrautils \
    && rm -rf /var/lib/apt/lists/*

# Copy Project Source
WORKDIR /app
COPY CMakeLists.txt .
COPY src src
COPY tests tests

# Build the Vulkan Provider
# We use Release build for performance
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    cp libvc6_crypto.so /usr/local/lib/ && \
    cp aes_ctr.spv /usr/local/lib/ && \
    cp chacha20.spv /usr/local/lib/

# Config OpenSSL to use the provider by default
# Debian's OpenSSL config is usually at /etc/ssl/openssl.cnf
RUN echo "openssl_conf = openssl_init" >> /etc/ssl/openssl.cnf && \
    echo "[openssl_init]" >> /etc/ssl/openssl.cnf && \
    echo "providers = provider_sect" >> /etc/ssl/openssl.cnf && \
    echo "[provider_sect]" >> /etc/ssl/openssl.cnf && \
    echo "default = default_sect" >> /etc/ssl/openssl.cnf && \
    echo "vc6 = vc6_sect" >> /etc/ssl/openssl.cnf && \
    echo "[default_sect]" >> /etc/ssl/openssl.cnf && \
    echo "activate = 1" >> /etc/ssl/openssl.cnf && \
    echo "[vc6_sect]" >> /etc/ssl/openssl.cnf && \
    echo "module = /usr/local/lib/libvc6_crypto.so" >> /etc/ssl/openssl.cnf && \
    echo "activate = 1" >> /etc/ssl/openssl.cnf

# Entrypoint - Default to bash
CMD ["/bin/bash"]
