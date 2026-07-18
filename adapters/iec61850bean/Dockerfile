# Multi-stage Dockerfile for the iec61850bean IED adapter.
#
# Build from repository root:
#   docker buildx build --platform=linux/amd64 \
#       -f adapters/iec61850bean/Dockerfile \
#       -t mms-interop-iec61850bean:local .
#
# The image contains two named entry points:
#   iec61850bean-ied-server  --port <port> [--icd <path>] [--values <path>]
#   iec61850bean-ied-client  --host <host> --port <port>
#
# Both programs are dispatched via Main.java using a subcommand argument.
# Fixtures are baked in at /fixtures/iec61850/ (interop.icd, values.json).

ARG MAVEN_DIGEST=sha256:3a4ab3276a087bf276f79cae96b1af04f53731bec53fb2e651aca79e4b10211e
ARG JRE_DIGEST=sha256:c4e6542e774de504da9b4729ff8d761287c965c1d788528ca78da30024efdb23

# ---------------------------------------------------------------------------
# Stage 1: Maven build
# ---------------------------------------------------------------------------
FROM maven:3.9.9-eclipse-temurin-21@${MAVEN_DIGEST} AS build

WORKDIR /build

# Resolve dependencies before copying sources so Docker caches the layer.
COPY adapters/iec61850bean/pom.xml .
RUN mvn dependency:go-offline -q

COPY adapters/iec61850bean/src ./src
RUN mvn package -q -DskipTests

# ---------------------------------------------------------------------------
# Stage 2: Minimal JRE runtime
# ---------------------------------------------------------------------------
FROM eclipse-temurin:21.0.7_6-jre-jammy@${JRE_DIGEST} AS runtime

RUN addgroup --system --gid 10001 adapter \
    && adduser --system --uid 10001 --ingroup adapter --no-create-home adapter

WORKDIR /app
COPY --from=build /build/target/iec61850bean-adapter.jar .

# Bake the IEC 61850 fixture files so the adapter can load them by default.
COPY fixtures/iec61850/interop.icd  /fixtures/iec61850/interop.icd
COPY fixtures/iec61850/values.json  /fixtures/iec61850/values.json

# Entry point scripts — each passes its subcommand to Main.java.
RUN printf '#!/bin/sh\nexec java -jar /app/iec61850bean-adapter.jar server "$@"\n' \
      > /usr/local/bin/iec61850bean-ied-server \
 && printf '#!/bin/sh\nexec java -jar /app/iec61850bean-adapter.jar client "$@"\n' \
      > /usr/local/bin/iec61850bean-ied-client \
 && chmod +x /usr/local/bin/iec61850bean-ied-server \
             /usr/local/bin/iec61850bean-ied-client

USER adapter

# Bake the image version into the container; readable at runtime via $ADAPTER_VERSION.
ARG ADAPTER_VERSION=dev
ENV ADAPTER_VERSION=${ADAPTER_VERSION}

# Standard MMS port.  Override with --port at runtime.
EXPOSE 1102

ENTRYPOINT ["iec61850bean-ied-server"]
CMD ["--port", "1102"]
