CC = gcc 
MAINC = group_server.c user-group-generated.c 
EXEC = server
CFLAGS = `pkg-config --cflags --libs gtk+-3.0`
main:  
	$(CC)  -g $(MAINC)  -o $(EXEC) $(CFLAGS)
clean:
	rm $(EXEC) -rf
