FROM alpine:latest

# Install build tools and dependencies
RUN apk add --no-cache g++ make curl-dev curl

# Download ArduinoJson
RUN curl -L -o /usr/include/ArduinoJson.h https://github.com/bblanchon/ArduinoJson/releases/download/v6.21.3/ArduinoJson-v6.21.3.h

# Download cpp-httplib (single-header HTTP server/client)
RUN curl -L -o /usr/include/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h

# Create app directory
WORKDIR /app

# Copy the simulation framework
COPY . /app

# Compile command
CMD ["sh", "-c", "make && ./esp32-sim"]
