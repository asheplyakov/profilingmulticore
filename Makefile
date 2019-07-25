

aligned_new := $(shell if $(CXX) -std=c++11 -faligned-new -o /dev/null src/test.cc 2>/dev/null; then echo OK; fi)
ifeq ($(strip $(aligned_new)),)
CXX := /opt/rh/devtoolset-7/root/usr/bin/g++
endif
CXXFLAGS ?= -O2 -g -Wall -pipe -fno-omit-frame-pointer
LDFLAGS ?=
WORKER_COUNT ?= $(shell nproc --all)

all: bin/falsesharing bin/nomorefalsesharing \
	bin/thunderingherd bin/nothunderingherd bin/nothunderingherd_clockfix


bin/falsesharing: .o/falsesharing.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/falsesharing.o: src/falsesharing.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread $<

bin/nomorefalsesharing: .o/nomorefalsesharing.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/nomorefalsesharing.o: src/nomorefalsesharing.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread -faligned-new $<

bin/thunderingherd: .o/thunderingherd.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/thunderingherd.o: src/thunderingherd.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread $<

bin/nothunderingherd: .o/nothunderingherd.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/nothunderingherd.o: src/nothunderingherd.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread $<

bin/nothunderingherd_clockfix: .o/nothunderingherd_clockfix.o
	@mkdir -p "$(dir $@)"
	$(CXX) -pthread -o $@ $(LDFLAGS) $^

.o/nothunderingherd_clockfix.o: src/nothunderingherd_clockfix.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) -c -o $@ $(CXXFLAGS) -std=c++11 -pthread $<


bench: bin/falsesharing
	./benchmark.sh

fixedbench: bin/falsesharing bin/nomorefalsesharing
	env SHOW_FIX=Y ./benchmark.sh

progs2profile := thunderingherd nothunderingherd nothunderingherd_clockfix
oncpuprofiles := $(progs2profile:%=.o/%.perf.data)
oncpustacks := $(progs2profile:%=dat/%.stacks.gz)
oncpuflamegra := $(progs2profile:%=img/%.svg)
thunderingherd_plot_title := Thundering herd,
nothunderingherd_plot_title := NO thundering herd,
nothunderingherd_clockfix_plot_title := NO thundering herd, sa::steady_clock,

oncpu: $(oncpustacks) $(oncpuflamegra) $(oncpuprofiles)

.NOTPARALLEL:
$(oncpuprofiles): .o/%.perf.data: ./bin/%
	@mkdir -p "$(dir $@)"
	sudo perf record -o $@.tmp -F 99 --call-graph=dwarf -a -- $< -t $(WORKER_COUNT)
	mv $@.tmp $@

$(oncpustacks): dat/%.stacks.gz: .o/%.perf.data Makefile
	@mkdir -p "$(dir $@)"
	set -e; \
	names=`seq -f 'tworker_%g' -s ',' 0 $(WORKER_COUNT)`; \
	sudo perf script -i $< --header --comms=tproducer,$$names > $(@:%.gz=%.tmp) && \
	sed -i $(@:%.gz=%.tmp) -r -e 's/^(tworker)(_[0-9]+)/\1/' && \
	gzip -9 < $(@:%.gz=%.tmp) > $@.tmp
	mv $@.tmp $@

$(oncpuflamegra): img/%.svg: dat/%.stacks.gz
	set -e; \
	zcat $< | ~/tools/FlameGraph/stackcollapse-perf.pl | \
	~/tools/FlameGraph/flamegraph.pl --title "$($*_plot_title) $(WORKER_COUNT) threads" > $@.tmp
	mv $@.tmp $@

wakestacks := $(progs2profile:%=dat/%.wakestacks.gz)
offcpuflamegr := $(progs2profile:%=img/%_wakestacks.svg)

offcpu: $(wakestacks) $(offcpuflamegr)

.NOTPARALLEL:
$(wakestacks): dat/%.wakestacks.gz: ./bin/%
	set -e; $< -t $(WORKER_COUNT) & pid=$$!; \
	sudo /usr/share/bcc/tools/offwaketime -f -p $$pid --stack-storage-size=32768 -M 9999999999 10 > "$(@:%.gz=%.tmp)"; \
	gzip -9 < "$(@:%.gz=%.tmp)" > $@.tmp; \
	wait $$pid
	mv $@.tmp $@
	
$(offcpuflamegr): img/%_wakestacks.svg: dat/%.wakestacks.gz Makefile
	set -e; \
	zcat $< | ~/tools/FlameGraph/flamegraph.pl --title "$($*_plot_title) $(WORKER_COUNT) threads" --colors wakeup --countname usec > $@.tmp
	mv $@.tmp $@

help:
	@echo Available targets
	@echo bench      compile and run false sharing demo
	@echo fixedbench compile and run fixed version
	@echo oncpu compile and run thundering herd demo
	@echo offcpu thundering herd demo -- off-CPU

clean:
	@rm -rf .o
	@rm -rf bin
