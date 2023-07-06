FROM alpine AS builder
RUN apk add --no-cache \
      git build-base cmake pkgconf \
      boost-dev libmodbus-dev mosquitto-dev yaml-cpp-dev rapidjson-dev catch2
ARG REPO_URL=https://github.com/BlackZork/mqmgateway.git
ARG REPO_BRANCH
RUN git clone --single-branch -c advice.detachedHead=false \
      ${REPO_BRANCH:+--branch "${REPO_BRANCH}"} "${REPO_URL}" "/opt/mqmgateway/source"
RUN sed -i 's/u_int16_t/uint16_t/g' /opt/mqmgateway/source/libmodmqttsrv/modbus_messages.hpp
ARG EXPRTK_URL=https://github.com/ArashPartow/exprtk/raw/master/exprtk.hpp
RUN mkdir -p /usr/local/include && wget -O /usr/local/include/exprtk.hpp "${EXPRTK_URL}" || true
RUN cmake -DCMAKE_INSTALL_PREFIX:PATH=/opt/mqmgateway/install \
      -S /opt/mqmgateway/source \
      -B /opt/mqmgateway/build && \
    cd /opt/mqmgateway/build && make -j$(($(nproc)-1)) && make install
RUN mkdir -p /opt/mqmgateway/metadata && \
    echo >/opt/mqmgateway/metadata/commit_sha $(git rev-parse HEAD) && \
    echo >/opt/mqmgateway/metadata/commit_short_sha $(git rev-parse --short HEAD) && \
    echo >/opt/mqmgateway/metadata/commit_ref_name $(git rev-parse --abbrev-ref HEAD) && \
    echo >/opt/mqmgateway/metadata/commit_date $(git show -s --format=%ci HEAD)

FROM alpine AS runtime
COPY --from=builder /opt/mqmgateway/install/ /usr/
COPY --from=builder /opt/mqmgateway/source/modmqttd/config.template.yaml /etc/modmqttd/config.yaml
COPY --from=builder /opt/mqmgateway/metadata/ /opt/metadata/
RUN apk add --no-cache \
  $(apk search -e boost*-log | grep -o '^boost.*-log') \
  $(apk search -e boost*-program_options | grep -o '^boost.*-program_options') \
  libmodbus mosquitto yaml-cpp
ENTRYPOINT [ "/usr/bin/modmqttd" ]
CMD [ "--config", "/etc/modmqttd/config.yaml" ]
