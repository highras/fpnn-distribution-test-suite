dirs = DATControlCenter DATActor DATDeployer DATController DATMonitor

all:
	for x in $(dirs); do (cd $$x; make) || exit 1; done

clean:
	for x in $(dirs); do (cd $$x; make clean) || exit 1; done