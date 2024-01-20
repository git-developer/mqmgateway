ARG BASE_IMAGE=alpine
FROM ${BASE_IMAGE} AS builder
RUN case "$(sed -nE 's/^ID=(.+)$/\1/p' /etc/os-release)" in \
      debian) \
        apt-get update && apt-get install -y \
          git build-essential cmake pkgconf wget \
          libboost-dev libboost-log-dev libboost-program-options-dev \
          libmodbus-dev mosquitto-dev libmosquitto-dev libmosquittopp-dev \
          libyaml-cpp-dev rapidjson-dev catch2 \
      ;; \
      alpine) \
        apk update && apk add --no-cache \
          git build-base cmake pkgconf boost-dev libmodbus-dev mosquitto-dev yaml-cpp-dev rapidjson-dev catch2 \
      ;; \
    esac

ARG EXPRTK_URL=https://github.com/ArashPartow/exprtk/raw/master/exprtk.hpp
RUN if [ "${EXPRTK_URL-}" ]; then \
      mkdir -p /usr/local/include && \
      wget -O /usr/local/include/exprtk.hpp "${EXPRTK_URL}" || true; \
    fi

COPY . /opt/mqmgateway/source
ARG MQM_TEST_SKIP
RUN cmake \
      -DCMAKE_INSTALL_PREFIX:PATH=/opt/mqmgateway/install \
      ${MQM_TEST_SKIP:+-DWITHOUT_TESTS=1} \
      -S /opt/mqmgateway/source \
      -B /opt/mqmgateway/build
WORKDIR /opt/mqmgateway/build
RUN make -j$(nproc)
ARG MQM_TEST_ALLOW_FAILURE=false
ARG MQM_TEST_DEFAULT_WAIT_MS
ARG MQM_TEST_LOGLEVEL=3
RUN if [ -z "${MQM_TEST_SKIP}" ]; then cd unittests; ./tests || [ "${MQM_TEST_ALLOW_FAILURE}" = "true" ]; fi
RUN make install

FROM ${BASE_IMAGE} AS runtime
COPY --from=builder /opt/mqmgateway/install/ /usr/
COPY --from=builder /opt/mqmgateway/source/modmqttd/config.template.yaml /etc/modmqttd/config.yaml

RUN case "$(sed -nE 's/^ID=(.+)$/\1/p' /etc/os-release)" in \
      debian) \
        apt-get update && apt-get install -y --no-install-recommends \
          ^libboost-log[0-9.]+$ \
          ^libboost-program-options[0-9.]+$ \
          libmodbus[0-9.]+ mosquitto libyaml-cpp[0-9.]+ && \
        apt-get clean \
      ;; \
      alpine) \
        apk update && apk add --no-cache \
          $(apk search -e boost*-log | grep -o '^boost.*-log') \
          $(apk search -e boost*-program_options | grep -o '^boost.*-program_options') \
          libmodbus mosquitto yaml-cpp && \
        apk cache purge \
      ;; \
    esac
ENTRYPOINT [ "/usr/bin/modmqttd" ]
CMD [ "--config", "/etc/modmqttd/config.yaml" ]
