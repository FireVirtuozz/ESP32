# =============================
# Dockerfile ESP32 + ESP-IDF
# =============================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# -----------------------------
# Install base dependencies
# -----------------------------
RUN apt-get update && apt-get install -y \
    git wget flex bison gperf python3 python3-pip python3-venv python-is-python3 \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
    unzip tar build-essential doxygen graphviz curl locales \
    && rm -rf /var/lib/apt/lists/*

# -----------------------------
# Configure locales
# -----------------------------
RUN locale-gen en_US.UTF-8 \
    && update-locale LANG=en_US.UTF-8

ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8

# -----------------------------
# Install ESP-IDF
# -----------------------------
ENV IDF_PATH=/opt/esp-idf
RUN git clone --recursive https://github.com/espressif/esp-idf.git $IDF_PATH
WORKDIR $IDF_PATH
RUN ./install.sh

# Add ESP-IDF tools to PATH
ENV PATH=$IDF_PATH/tools:$PATH

# -----------------------------
# Create workspace
# -----------------------------
WORKDIR /workspace

# -----------------------------
# Auto sourcing ESP-IDF at startup
# -----------------------------
SHELL ["/bin/bash", "-c"]
RUN echo "source $IDF_PATH/export.sh" >> /etc/bash.bashrc

# -----------------------------
# Default entry
# -----------------------------
CMD ["bash"]
