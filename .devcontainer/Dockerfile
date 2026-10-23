FROM infotrainingdev/gcc-cmake-vcpkg:latest

WORKDIR /root

# [Optional] Uncomment this section to install additional packages.
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends liburing-dev