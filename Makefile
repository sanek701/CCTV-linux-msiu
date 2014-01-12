all:
	gcc control_socket.c database.c event_loop.c file_reader.c json.c l1-list.c recorder_thread.c rtsp_server.c screen.c server.c string_utils.c -Wall -std=c99 -pthread -lm -g `pkg-config --libs --cflags libavutil libavformat libavcodec libswscale opencv libconfig` -lpq
