CXXFLAGS=-W -Wall -Wextra

gds2vec: gds2vec.o
	$(CXX) -o $@ $^ -lGDSII

gds2vec.o : ps-template.ps.rawstring

%.rawstring : %
	echo "static constexpr char k$$(echo $^ | sed 's/[\.-]/_/g')[] = R\"(" > $@
	cat $^ >> $@
	echo ')";' >> $@

clean:
	rm -rf *.rawstring *.o gds2vec
