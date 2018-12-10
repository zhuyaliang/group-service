CC = gcc 
MAINC = main.c 
EXEC = test-group-admin
CFLAGS = `pkg-config --cflags --libs gtk+-3.0` -lgroup-service
main:  
	$(CC)  -g $(MAINC)  -o $(EXEC) $(CFLAGS)
clean:
	rm $(EXEC) -rf
