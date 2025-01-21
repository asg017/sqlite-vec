#!/bin/bash
cat Dockerfile.amd | docker buildx build -f - .
