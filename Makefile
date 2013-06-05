all:
	gcc server.c database.c control_socket.c recorder_thread.c screen.c l1-list.c json.c -std=c99 -pthread -lm `pkg-config --libs --cflags libavutil libavformat libavcodec libswscale opencv libconfig gstreamer-0.10` -lpq -lgstrtspserver-0.10
