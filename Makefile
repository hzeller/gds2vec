CXXFLAGS=-W -Wall -Wextra -std=c++17

gds2vec: gds2vec.o
	$(CXX) -o $@ $^ -lGDSII

gds2vec.o : ps-template.ps.rawstring gds-query.h

%.rawstring : %
	echo "static constexpr char k$$(echo $^ | sed 's/[\.-]/_/g')[] = R\"(" > $@
	cat $^ >> $@
	echo ')";' >> $@

# Useful target to create DXF files for laser cutting
%-1.dxf : %.ps
	pstoedit -psarg "-r600x600" -nb -f "dxf_s:-mm -ctl" $^ $*-%PAGENUMBER%.dxf

clean:
	rm -rf *.rawstring *.o gds2vec
