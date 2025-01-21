FROM arm64v8/node:23-bookworm-slim AS base

# Update default packages
RUN apt-get update

# Get Ubuntu packages
RUN apt-get install -y \
    build-essential \
    curl

# Update new packages
RUN apt-get update
RUN apt-get install -y curl git

# Get Rust
RUN curl https://sh.rustup.rs -sSf | bash -s -- -y

RUN echo 'source $HOME/.cargo/env' >> $HOME/.bashrc

RUN corepack enable

# IMPORTANT CHANGE NAME
COPY . /node_modules/sqlite-vec
WORKDIR /node_modules/sqlite-vec

RUN yarn install

RUN yarn install --frozen-lockfile
ENV PATH="/root/.cargo/bin:${PATH}"
RUN yarn run build
RUN yarn run prepublishOnly
