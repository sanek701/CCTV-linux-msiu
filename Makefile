all:
	gcc server.c database.c control_socket.c recorder_thread.c screen.c l1-list.c json.c -std=c99 -pthread -lm -lavutil -lavformat -lavcodec -lswscale -I/usr/include/opencv -lopencv_core -lopencv_imgproc -lopencv_highgui -lconfig -lpq -I/usr/include/gstreamer-0.10 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include/ -I/usr/include/libxml2 -lgstreamer-0.10 -lgstrtspserver-0.10
