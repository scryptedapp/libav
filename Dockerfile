FROM myoung34/github-runner:ubuntu-jammy

RUN apt-get update
RUN apt-get install -y ca-certificates curl gnupg sudo wget
RUN mkdir -p /etc/apt/keyrings
RUN curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg

ARG NODE_MAJOR=20
RUN echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_$NODE_MAJOR.x nodistro main" | sudo tee /etc/apt/sources.list.d/nodesource.list

RUN apt-get update
RUN apt-get install nodejs -y

RUN apt-get -y install dpkg fakeroot

RUN curl https://raw.githubusercontent.com/koush/scrypted/main/install/docker/install-nvidia-graphics.sh | bash

RUN wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
RUN wget -qO /etc/apt/sources.list.d/lunarg-vulkan-jammy.list http://packages.lunarg.com/vulkan/lunarg-vulkan-jammy.list
RUN lsb_release -a
RUN apt -y update
RUN apt -y install vulkan-sdk
RUN apt -y install clang
