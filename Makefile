all:		exclusiu

exclusiu:	cache.cc exclusiu.cc replacement_state.cpp replacement_state.h trace.h
		g++ -DCACHE -O3 -Wall -g -o exclusiu cache.cc exclusiu.cc replacement_state.cpp -lz

clean:
	 	rm -f exclusiu
