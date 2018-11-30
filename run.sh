#!/bin/bash

echo "┌──────────────────────────────┐"
echo "│  Load Kernel Module syscall  │"
echo "└──────────────────────────────┘"
echo "# insmod syscall.ko"
sudo insmod ./systemcall/syscall.ko
echo ""
echo "┌──────────────────────────────┐"
echo "│        Task Manager          │"
echo "└──────────────────────────────┘"
echo "$ ./task-manager $1"
./app/task-manager
echo ""
echo "┌──────────────────────────────┐"
echo "│ Unload Kernel Module syscall │"
echo "└──────────────────────────────┘"
echo "# rmmod syscall"
sudo rmmod syscall

