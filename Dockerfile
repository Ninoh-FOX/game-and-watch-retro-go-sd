FROM debian:bookworm-slim

WORKDIR /opt

ENV ARM_COMPILER_VERSION=14.3.rel1
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        xz-utils \
        wget \
        make \
        python3 \
        sudo \
        xxd && \
        apt-get clean && \
        rm -rf /var/lib/apt/lists/*

RUN ARCH=$(uname -m) && \
    wget -O toolchain.tar.xz "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_COMPILER_VERSION}/binrel/arm-gnu-toolchain-${ARM_COMPILER_VERSION}-${ARCH}-arm-none-eabi.tar.xz" && \
    tar xf toolchain.tar.xz && \
    rm -f toolchain.tar.xz

RUN useradd -m docker && echo "docker:docker" | chpasswd && \
    chown docker:docker /opt && \
    echo "docker ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

RUN ARCH=$(uname -m) && \
    echo "export GCC_PATH=/opt/arm-gnu-toolchain-${ARM_COMPILER_VERSION}-${ARCH}-arm-none-eabi/bin" >> /etc/profile

USER docker

ENV PATH="/opt/venv/bin:$PATH"

RUN mkdir /opt/workdir
RUN sudo chown -R docker:docker /opt/workdir

WORKDIR /opt/workdir

CMD ["/bin/bash", "-l"]