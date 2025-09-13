# Stage 1: Base image with ESP-IDF and common dependencies
FROM espressif/idf:v5.5.1 AS base

ARG DEBIAN_FRONTEND=nointeractive

# Install common dependencies
RUN apt-get update \
  && apt install -y -q \
  cmake \
  git \
  hwdata \
  libglib2.0-dev \
  libnuma1 \
  libpixman-1-dev \
  && rm -rf /var/lib/apt/lists/*

# Node.js and npm (v20 is required)
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
  && apt-get install -y nodejs

# Common environment setup
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

RUN echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> ~/.bashrc

ENTRYPOINT ["/opt/esp/entrypoint.sh"]
CMD ["/bin/bash"]

# Stage 2: QEMU tools
FROM base AS qemu

# Install QEMU dependencies
RUN apt-get update \
  && apt install -y -q \
  linux-tools-virtual \
  && rm -rf /var/lib/apt/lists/*

# Set up usbip for QEMU USB/IP functionality
RUN update-alternatives --install /usr/local/bin/usbip usbip `ls /usr/lib/linux-tools/*/usbip | tail -n1` 20

# QEMU
ENV QEMU_REL=esp-develop-20220919
ENV QEMU_SHA256=f6565d3f0d1e463a63a7f81aec94cce62df662bd42fc7606de4b4418ed55f870
ENV QEMU_DIST=qemu-${QEMU_REL}.tar.bz2
ENV QEMU_URL=https://github.com/espressif/qemu/releases/download/${QEMU_REL}/${QEMU_DIST}

RUN wget --no-verbose ${QEMU_URL} \
  && echo "${QEMU_SHA256} *${QEMU_DIST}" | sha256sum --check --strict - \
  && tar -xf $QEMU_DIST -C /opt \
  && rm ${QEMU_DIST}

ENV PATH=/opt/qemu/bin:${PATH}
