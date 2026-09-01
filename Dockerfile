# ==========================================
# STAGE 1: Build Environment
# ==========================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    bison \
    flex \
    zlib1g-dev \
    libgecode-dev \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 1. Install OR-Tools
RUN wget -q https://github.com/google/or-tools/releases/download/v9.11/or-tools_amd64_ubuntu-22.04_cpp_v9.11.4210.tar.gz && \
    tar -xzf or-tools_amd64_ubuntu-22.04_cpp_v9.11.4210.tar.gz --strip-components=1 -C /usr/local && \
    rm or-tools_amd64_ubuntu-22.04_cpp_v9.11.4210.tar.gz

# 2. Build & Install CaDiCaL
WORKDIR /tmp/cadical
RUN git clone https://github.com/arminbiere/cadical.git . && \
    ./configure && \
    make -j$(nproc) && \
    cp build/libcadical.a /usr/local/lib/ && \
    cp src/cadical.hpp /usr/local/include/ && \
    cp src/config.hpp /usr/local/include/ && \
    rm -rf /tmp/cadical

# 3. Build Application
WORKDIR /app
COPY . .

RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_GECODE=ON \
    -DENABLE_CADICAL=ON \
    -DENABLE_ORTOOLS=ON && \
    cmake --build build -j$(nproc)

# ==========================================
# STAGE 2: Minimal Runtime Environment
# ==========================================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install standard base runtime libraries
RUN apt-get update && apt-get install -y --no-install-recommends \
    zlib1g \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy Gecode dynamic libraries directly from builder
COPY --from=builder /usr/lib/x86_64-linux-gnu/libgecode* /usr/lib/x86_64-linux-gnu/

# Copy OR-Tools shared libraries
COPY --from=builder /usr/local/lib/libortools* /usr/local/lib/

# Update dynamic linker cache
RUN ldconfig

# Copy the compiled binary
COPY --from=builder /app/build/nocq /app/nocq

ENTRYPOINT ["/app/nocq"]


# --------------------- Docker without size optimization --------------------------

# FROM ubuntu:22.04

# ENV DEBIAN_FRONTEND=noninteractive

# # 1. Install base build dependencies and Gecode
# RUN apt-get update && apt-get install -y --no-install-recommends \
#     build-essential \
#     cmake \
#     git \
#     bison \
#     flex \
#     zlib1g-dev \
#     libgecode-dev \
#     wget \
#     ca-certificates \
#     && rm -rf /var/lib/apt/lists/*

# # 2. Install OR-Tools (C++ Binary Distribution)
# # Installs headers and cmake configs into /usr/local
# RUN wget -q https://github.com/google/or-tools/releases/download/v9.11/or-tools_amd64_ubuntu-22.04_cpp_v9.11.4210.tar.gz && \
#     tar -xzf or-tools_amd64_ubuntu-22.04_cpp_v9.11.4210.tar.gz && \
#     cp -r or-tools_x86_64_Ubuntu-22.04_cpp_v9.11.4210/* /usr/local/ && \
#     rm -rf or-tools*

# # 3. Build & Install CaDiCaL
# WORKDIR /tmp/cadical
# RUN git clone https://github.com/arminbiere/cadical.git . && \
#     ./configure && \
#     make -j$(nproc) && \
#     cp build/libcadical.a /usr/local/lib/ && \
#     cp src/cadical.hpp /usr/local/include/ && \
#     cp src/config.hpp /usr/local/include/ && \
#     rm -rf /tmp/cadical

# # 4. Build Application
# WORKDIR /app
# COPY . .

# RUN cmake -B build \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DENABLE_GECODE=ON \
#     -DENABLE_CADICAL=ON \
#     -DENABLE_ORTOOLS=ON && \
#     cmake --build build -j$(nproc)

# ENTRYPOINT ["/app/build/nocq"]

