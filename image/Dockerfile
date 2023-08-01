ARG ALPINE_VERSION=latest
FROM alpine:${ALPINE_VERSION} AS builder
RUN apk update && apk add --no-cache \
      git build-base cmake pkgconf boost-dev libmodbus-dev mosquitto-dev yaml-cpp-dev rapidjson-dev catch2

ARG REPO_URL=https://github.com/BlackZork/mqmgateway.git
ARG REPO_BRANCH
COPY . /opt/mqmgateway/source
RUN if [ "${REPO_URL-}" ]; then \
      rm -r /opt/mqmgateway/source && \
      git clone --single-branch -c advice.detachedHead=false \
        ${REPO_BRANCH:+--branch "${REPO_BRANCH}"} "${REPO_URL}" "/opt/mqmgateway/source"; \
    fi

ARG EXPRTK_URL=https://github.com/ArashPartow/exprtk/raw/master/exprtk.hpp
RUN if [ "${EXPRTK_URL-}" ]; then \
      mkdir -p /usr/local/include && \
      wget -O /usr/local/include/exprtk.hpp "${EXPRTK_URL}" || true; \
    fi

ARG MQM_TEST_SKIP
RUN cmake \
      -DCMAKE_INSTALL_PREFIX:PATH=/opt/mqmgateway/install \
      ${MQM_TEST_SKIP:+-DWITHOUT_TESTS=1} \
      -S /opt/mqmgateway/source \
      -B /opt/mqmgateway/build
WORKDIR /opt/mqmgateway/build
RUN make -j$(nproc)
ARG MQM_TEST_ALLOW_FAILURE=true
ARG MQM_TEST_DEFAULT_WAIT_MS
ARG MQM_TEST_LOGLEVEL
RUN if [ -z "${MQM_TEST_SKIP}" ]; then cd unittests; ./tests || [ "${MQM_TEST_ALLOW_FAILURE}" = "true" ]; fi
RUN make install
RUN cd /opt/mqmgateway/source && \
    mkdir -p /opt/mqmgateway/metadata && \
    echo >/opt/mqmgateway/metadata/commit_sha $(git rev-parse HEAD) && \
    echo >/opt/mqmgateway/metadata/commit_short_sha $(git rev-parse --short HEAD) && \
    echo >/opt/mqmgateway/metadata/commit_ref_name $(git rev-parse --abbrev-ref HEAD) && \
    echo >/opt/mqmgateway/metadata/commit_date $(git show -s --format=%ci HEAD)

FROM alpine:${ALPINE_VERSION} AS runtime
COPY --from=builder /opt/mqmgateway/install/ /usr/
COPY --from=builder /opt/mqmgateway/source/modmqttd/config.template.yaml /etc/modmqttd/config.yaml
COPY --from=builder /opt/mqmgateway/metadata/ /opt/metadata/
RUN apk update && apk add --no-cache \
  $(apk search -e boost*-log | grep -o '^boost.*-log') \
  $(apk search -e boost*-program_options | grep -o '^boost.*-program_options') \
  libmodbus mosquitto yaml-cpp && \
  apk cache purge
ENTRYPOINT [ "/usr/bin/modmqttd" ]
CMD [ "--config", "/etc/modmqttd/config.yaml" ]
