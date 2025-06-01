g++ -Iinclude -Ilibs/rpi-rgb-led-matrix/include main.c src/server.c src/client.c src/json.c src/game.c src/board.c libs/cJSON.c -Llibs/rpi-rgb-led-matrix/lib -lrgbmatrix -lpthread -lrt -o hw3 

sudo ./hw3 server -p 8080 --led-rows=64 --led-cols=64 --led-gpio-mapping=regular --led-brightness=75 --led-chain=1 --led-no-hardware-pulse

sudo ./hw3 client -i 127.0.0.1 -p 8080 -u user1 --led-rows=64 --led-cols=64 --led-brightness=75 --led-chain=1 --led-no-hardware-pulse --led-gpio-mapping=regular
sudo ./hw3 client -i 127.0.0.1 -p 8080 -u user2 --led-rows=64 --led-cols=64 --led-brightness=75 --led-chain=1 --led-no-hardware-pulse --led-gpio-mapping=regular

//board
g++ -DBOARD_STANDALONE src/board.c -Iinclude -Ilibs/rpi-rgb-led-matrix/include     -Llibs/rpi-rgb-led-matrix/lib -lrgbmatrix -lpthread -lrt -o board_standalone
sudo ./board_standalone --led-rows=64 --led-cols=64 --led-gpio-mapping=regular --led-brightness=75 --led-chain=1 --led-no-hardware-pulse
