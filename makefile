comic-earthstar-dl: main.cpp
	g++ -o comic-earthstar-dl -std=c++11 main.cpp -I./ -lcurl -ljpeg -lpng
