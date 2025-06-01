g++ -Iinclude -Ilibs/rpi-rgb-led-matrix/include main.c src/server.c src/client.c src/json.c src/game.c src/board.c libs/cJSON.c -Llibs/rpi-rgb-led-matrix/lib -lrgbmatrix -lpthread -lrt -o hw3 

sudo ./hw3 server -p 8080 --led-rows=64 --led-cols=64 --led-gpio-mapping=regular --led-brightness=75 --led-chain=1 --led-no-hardware-pulse
