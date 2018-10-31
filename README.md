# Ubuntu-Task-Manager
A task manger for Ubuntu.

# Install Dependencies
sudo apt-get install libgtk-3-dev

# Compile
gcc `pkg-config --cflags gtk+-3.0` -o app app.c `pkg-config --libs gtk+-3.0`

# Run
./app
