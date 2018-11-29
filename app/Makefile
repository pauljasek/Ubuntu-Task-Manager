all: app

app:
	gcc `pkg-config --cflags gtk+-3.0` -o task-manager app.c `pkg-config --libs gtk+-3.0` -lm

clean:
	rm task-manager
