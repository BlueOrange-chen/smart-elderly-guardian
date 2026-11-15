FROM eclipse-temurin:21-jre

WORKDIR /app
COPY target/esp32-camera-stream-server-0.0.1-SNAPSHOT.jar /app/server.jar

ENV SERVER_PORT=8090
EXPOSE 8090

ENTRYPOINT ["java", "-jar", "/app/server.jar"]
