CC = gcc 
MAINC = group_server.c group.c user-group-generated.c -lgio-2.0 -lgobject-2.0 -lglib-2.0
EXEC = server
CFLAGS = `pkg-config --cflags --libs gtk+-3.0`
main:  
	$(CC)  -g $(MAINC)  -o $(EXEC) $(CFLAGS)
clean:
	rm $(EXEC) -rf
