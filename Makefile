all:
	gcc server.c database.c control_socket.c rtsp_server.c recorder_thread.c screen.c file_reader.c l1-list.c json.c -Wall -std=c99 -pthread -lm `pkg-config --libs --cflags libavutil libavformat libavcodec libswscale opencv libconfig` -lpq
