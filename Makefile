#-----------------------------------------------
#Ryanul Haque, Aaron Mandel
#ryhaque, aamandel
#CSE130
#Makefile for httpserver.cpp
#------------------------------------------------

CC = g++
CFLAGS	= -std=gnu++11 -Wall -Wextra -Wpedantic -Wshadow -lpthread -pthread
EXEBIN 	= httpserver
SOURCES = $(EXEBIN).cpp
OBJECTS = $(EXEBIN).o


$(EXEBIN) : $(OBJECTS)
	$(CC) $(CFLAGS) -o $(EXEBIN) $(OBJECTS)
	

$(OBJECTS) : $(SOURCES)
	$(CC) $(CFLAGS) -c $(SOURCES)

clean :
	rm -f $(EXEBIN) $(OBJECTS)
